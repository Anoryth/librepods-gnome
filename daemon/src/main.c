/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2024 LibrePods Contributors
 *
 * LibrePods Daemon - AirPods integration for Linux
 */

#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "airpods_state.h"
#include "aap_protocol.h"
#include "bluetooth.h"
#include "bluez_monitor.h"
#include "dbus_service.h"
#include "media_control.h"

/* Global application state */
typedef struct {
    GMainLoop *main_loop;
    AirPodsState state;
    BluetoothConnection *bt_conn;
    BluezMonitor *bluez_monitor;
    DbusService *dbus_service;
    MediaControl *media_control;

    /* Pending connect info */
    char *pending_address;
    char *pending_name;

    /* Reconnection */
    guint reconnect_timeout_id;
    int reconnect_attempts;
} AppContext;

static AppContext app = {0};

/* Forward declarations */
static void connect_to_airpods(const char *address, const char *name);
static void disconnect_from_airpods(void);

/* ============================================================================
 * Bluetooth data handling
 * ========================================================================== */

static void on_bt_data_received(const uint8_t *data, size_t len, void *user_data)
{
    AapParsedPacket packet;
    AapParseResult result = aap_parse_packet(data, len, &packet);

    if (result != AAP_PARSE_OK) {
        if (result != AAP_PARSE_UNKNOWN_OPCODE) {
            g_debug("Failed to parse packet: %d", result);
        }
        return;
    }

    switch (packet.type) {
    case AAP_PKT_TYPE_BATTERY:
        g_message("Battery: L=%d%% R=%d%% Case=%d%%",
                  packet.data.battery.left_level,
                  packet.data.battery.right_level,
                  packet.data.battery.case_level);

        airpods_state_set_battery(&app.state,
                                   packet.data.battery.left_level,
                                   packet.data.battery.left_status,
                                   packet.data.battery.right_level,
                                   packet.data.battery.right_status,
                                   packet.data.battery.case_level,
                                   packet.data.battery.case_status);

        dbus_service_emit_battery_changed(app.dbus_service,
                                           packet.data.battery.left_level,
                                           packet.data.battery.right_level,
                                           packet.data.battery.case_level);
        dbus_service_emit_properties_changed(app.dbus_service, "BatteryLeft");
        dbus_service_emit_properties_changed(app.dbus_service, "BatteryRight");
        dbus_service_emit_properties_changed(app.dbus_service, "BatteryCase");
        break;

    case AAP_PKT_TYPE_EAR_DETECTION:
        g_message("Ear detection: primary=%s secondary=%s",
                  packet.data.ear_detection.primary_in_ear ? "in" : "out",
                  packet.data.ear_detection.secondary_in_ear ? "in" : "out");

        airpods_state_set_ear_detection(&app.state,
                                         packet.data.ear_detection.primary_in_ear,
                                         packet.data.ear_detection.secondary_in_ear,
                                         packet.data.ear_detection.primary_left);

        dbus_service_emit_ear_detection_changed(app.dbus_service,
                                                 app.state.ear_detection.left_in_ear,
                                                 app.state.ear_detection.right_in_ear);
        dbus_service_emit_properties_changed(app.dbus_service, "LeftInEar");
        dbus_service_emit_properties_changed(app.dbus_service, "RightInEar");

        /* Trigger media pause/resume based on ear detection */
        if (app.media_control) {
            media_control_on_ear_detection_changed(app.media_control,
                                                    app.state.ear_detection.left_in_ear,
                                                    app.state.ear_detection.right_in_ear);
        }
        break;

    case AAP_PKT_TYPE_NOISE_CONTROL:
        g_message("Noise control mode: %s",
                  noise_control_mode_to_string(packet.data.noise_control));

        airpods_state_set_noise_control(&app.state, packet.data.noise_control);

        dbus_service_emit_noise_control_changed(app.dbus_service,
                                                 packet.data.noise_control);
        dbus_service_emit_properties_changed(app.dbus_service, "NoiseControlMode");
        break;

    case AAP_PKT_TYPE_CONV_AWARENESS:
        g_message("Conversational awareness: %s",
                  packet.data.conversational_awareness ? "enabled" : "disabled");

        airpods_state_set_conversational_awareness(&app.state,
                                                    packet.data.conversational_awareness);

        dbus_service_emit_properties_changed(app.dbus_service, "ConversationalAwareness");
        break;

    case AAP_PKT_TYPE_CA_DETECTION:
        g_debug("CA detection event: volume_level=%d", packet.data.ca_volume_level);
        break;

    case AAP_PKT_TYPE_METADATA:
        g_message("Metadata received: device='%s' model='%s' manufacturer='%s'",
                  packet.data.metadata.device_name,
                  packet.data.metadata.model_number,
                  packet.data.metadata.manufacturer);

        /* Update model from model number */
        {
            AirPodsModel detected_model = airpods_model_from_number(packet.data.metadata.model_number);
            if (detected_model != AIRPODS_MODEL_UNKNOWN) {
                g_mutex_lock(&app.state.lock);
                app.state.model = detected_model;
                g_mutex_unlock(&app.state.lock);

                g_message("Detected AirPods model: %s", airpods_model_to_string(detected_model));
                dbus_service_emit_properties_changed(app.dbus_service, "DeviceModel");
            }
        }
        break;

    default:
        break;
    }
}

static void on_bt_state_changed(BluetoothState state, const char *error, void *user_data)
{
    switch (state) {
    case BT_STATE_CONNECTED:
        g_message("Bluetooth connected, sending handshake...");
        app.reconnect_attempts = 0;

        /* Attach to main loop for data reception */
        bt_connection_attach_to_mainloop(app.bt_conn, NULL);

        /* Send initialization sequence */
        g_usleep(100000);  /* 100ms delay */
        bt_connection_send_handshake(app.bt_conn);

        g_usleep(50000);  /* 50ms delay */
        bt_connection_send_set_features(app.bt_conn);

        g_usleep(50000);
        bt_connection_send_request_notifications(app.bt_conn);

        /* Update state */
        airpods_state_set_device(&app.state,
                                  app.pending_name,
                                  app.pending_address,
                                  AIRPODS_MODEL_UNKNOWN);  /* TODO: detect model */

        dbus_service_emit_device_connected(app.dbus_service,
                                            app.pending_address,
                                            app.pending_name);
        dbus_service_emit_properties_changed(app.dbus_service, "Connected");
        dbus_service_emit_properties_changed(app.dbus_service, "DeviceName");
        dbus_service_emit_properties_changed(app.dbus_service, "DeviceAddress");
        break;

    case BT_STATE_DISCONNECTED:
        g_message("Bluetooth disconnected");

        if (app.state.connected) {
            dbus_service_emit_device_disconnected(app.dbus_service,
                                                   app.state.device_address,
                                                   app.state.device_name);
        }

        airpods_state_reset(&app.state);
        dbus_service_emit_properties_changed(app.dbus_service, "Connected");
        break;

    case BT_STATE_ERROR:
        g_warning("Bluetooth error: %s", error ? error : "unknown");
        break;

    default:
        break;
    }
}

/* ============================================================================
 * Connection management
 * ========================================================================== */

static void connect_to_airpods(const char *address, const char *name)
{
    if (app.bt_conn && bt_connection_is_connected(app.bt_conn)) {
        g_message("Already connected, ignoring connect request");
        return;
    }

    /* Store pending info */
    g_free(app.pending_address);
    g_free(app.pending_name);
    app.pending_address = g_strdup(address);
    app.pending_name = g_strdup(name);

    /* Create new connection if needed */
    if (app.bt_conn == NULL) {
        app.bt_conn = bt_connection_new();
        bt_connection_set_data_callback(app.bt_conn, on_bt_data_received, NULL);
        bt_connection_set_state_callback(app.bt_conn, on_bt_state_changed, NULL);
    }

    g_message("Connecting to AirPods: %s (%s)", name, address);

    if (!bt_connection_connect(app.bt_conn, address)) {
        g_warning("Failed to initiate connection");
    }
}

static void disconnect_from_airpods(void)
{
    if (app.bt_conn) {
        bt_connection_disconnect(app.bt_conn);
    }
}

/* ============================================================================
 * BlueZ callbacks
 * ========================================================================== */

static void on_bluez_device_connected(const BluezDeviceInfo *device, void *user_data)
{
    g_message("BlueZ: AirPods connected - %s (%s)", device->name, device->address);
    connect_to_airpods(device->address, device->name);
}

static void on_bluez_device_disconnected(const BluezDeviceInfo *device, void *user_data)
{
    g_message("BlueZ: AirPods disconnected - %s (%s)", device->name, device->address);
    disconnect_from_airpods();
}

/* ============================================================================
 * D-Bus method callbacks
 * ========================================================================== */

static void on_set_noise_control(NoiseControlMode mode, void *user_data)
{
    if (!app.bt_conn || !bt_connection_is_connected(app.bt_conn)) {
        g_warning("Cannot set noise control: not connected");
        return;
    }

    uint8_t packet[AAP_CONTROL_CMD_SIZE];
    aap_build_noise_control_cmd(mode, packet);
    bt_connection_send(app.bt_conn, packet, AAP_CONTROL_CMD_SIZE);
}

static void on_set_conv_awareness(bool enabled, void *user_data)
{
    if (!app.bt_conn || !bt_connection_is_connected(app.bt_conn)) {
        g_warning("Cannot set conversational awareness: not connected");
        return;
    }

    uint8_t packet[AAP_CONTROL_CMD_SIZE];
    aap_build_conv_awareness_cmd(enabled, packet);
    bt_connection_send(app.bt_conn, packet, AAP_CONTROL_CMD_SIZE);
}

static void on_set_adaptive_level(int level, void *user_data)
{
    if (!app.bt_conn || !bt_connection_is_connected(app.bt_conn)) {
        g_warning("Cannot set adaptive level: not connected");
        return;
    }

    uint8_t packet[AAP_CONTROL_CMD_SIZE];
    aap_build_adaptive_level_cmd(level, packet);
    bt_connection_send(app.bt_conn, packet, AAP_CONTROL_CMD_SIZE);
}

/* ============================================================================
 * Signal handlers
 * ========================================================================== */

static gboolean on_sigint(gpointer user_data)
{
    g_message("Received SIGINT, shutting down...");
    g_main_loop_quit(app.main_loop);
    return G_SOURCE_REMOVE;
}

static gboolean on_sigterm(gpointer user_data)
{
    g_message("Received SIGTERM, shutting down...");
    g_main_loop_quit(app.main_loop);
    return G_SOURCE_REMOVE;
}

/* ============================================================================
 * Main
 * ========================================================================== */

static void cleanup(void)
{
    g_message("Cleaning up...");

    if (app.bt_conn) {
        bt_connection_free(app.bt_conn);
        app.bt_conn = NULL;
    }

    if (app.bluez_monitor) {
        bluez_monitor_free(app.bluez_monitor);
        app.bluez_monitor = NULL;
    }

    if (app.dbus_service) {
        dbus_service_free(app.dbus_service);
        app.dbus_service = NULL;
    }

    if (app.media_control) {
        media_control_free(app.media_control);
        app.media_control = NULL;
    }

    g_free(app.pending_address);
    g_free(app.pending_name);

    airpods_state_cleanup(&app.state);

    if (app.main_loop) {
        g_main_loop_unref(app.main_loop);
        app.main_loop = NULL;
    }
}

int main(int argc, char *argv[])
{
    g_message("LibrePods Daemon starting...");

    /* Initialize state */
    airpods_state_init(&app.state);

    /* Create main loop */
    app.main_loop = g_main_loop_new(NULL, FALSE);

    /* Set up signal handlers */
    g_unix_signal_add(SIGINT, on_sigint, NULL);
    g_unix_signal_add(SIGTERM, on_sigterm, NULL);

    /* Create D-Bus service */
    app.dbus_service = dbus_service_new(&app.state);
    if (app.dbus_service == NULL) {
        g_error("Failed to create D-Bus service");
        cleanup();
        return 1;
    }

    dbus_service_set_noise_control_callback(app.dbus_service, on_set_noise_control, NULL);
    dbus_service_set_conv_awareness_callback(app.dbus_service, on_set_conv_awareness, NULL);
    dbus_service_set_adaptive_level_callback(app.dbus_service, on_set_adaptive_level, NULL);

    if (!dbus_service_start(app.dbus_service)) {
        g_error("Failed to start D-Bus service");
        cleanup();
        return 1;
    }

    /* Create media control for MPRIS integration */
    app.media_control = media_control_new();
    if (app.media_control == NULL) {
        g_warning("Failed to create media control (MPRIS pause/resume disabled)");
    } else {
        /* Default: pause when one pod is removed */
        media_control_set_ear_pause_mode(app.media_control, EAR_PAUSE_ONE_OUT);
        g_message("Media control enabled (ear detection pause/resume)");
    }

    /* Create BlueZ monitor */
    app.bluez_monitor = bluez_monitor_new();
    if (app.bluez_monitor == NULL) {
        g_error("Failed to create BlueZ monitor");
        cleanup();
        return 1;
    }

    bluez_monitor_set_connected_callback(app.bluez_monitor, on_bluez_device_connected, NULL);
    bluez_monitor_set_disconnected_callback(app.bluez_monitor, on_bluez_device_disconnected, NULL);

    if (!bluez_monitor_start(app.bluez_monitor)) {
        g_error("Failed to start BlueZ monitor");
        cleanup();
        return 1;
    }

    /* Check for already connected devices */
    bluez_monitor_check_existing_devices(app.bluez_monitor);

    g_message("LibrePods Daemon running. Press Ctrl+C to quit.");

    /* Run main loop */
    g_main_loop_run(app.main_loop);

    /* Cleanup */
    cleanup();

    g_message("LibrePods Daemon stopped.");
    return 0;
}
