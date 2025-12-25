/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2024 LibrePods Contributors
 *
 * D-Bus service interface for GNOME extension communication
 */

#ifndef DBUS_SERVICE_H
#define DBUS_SERVICE_H

#include <glib.h>
#include <gio/gio.h>
#include "airpods_state.h"

/* D-Bus service constants */
#define DBUS_SERVICE_NAME       "org.librepods.Daemon"
#define DBUS_OBJECT_PATH        "/org/librepods/AirPods"
#define DBUS_INTERFACE_NAME     "org.librepods.AirPods1"

/* Callback for noise control mode change request */
typedef void (*DbusNoiseControlCallback)(NoiseControlMode mode, void *user_data);

/* Callback for conversational awareness change request */
typedef void (*DbusConvAwarenessCallback)(bool enabled, void *user_data);

/* Callback for adaptive noise level change request */
typedef void (*DbusAdaptiveLevelCallback)(int level, void *user_data);

/* D-Bus service context */
typedef struct DbusService DbusService;

/**
 * Create a new D-Bus service
 *
 * @param state Pointer to AirPods state (must remain valid)
 * @return New service or NULL on error
 */
DbusService *dbus_service_new(AirPodsState *state);

/**
 * Free D-Bus service
 */
void dbus_service_free(DbusService *service);

/**
 * Start the D-Bus service (acquire bus name)
 *
 * @return true on success
 */
bool dbus_service_start(DbusService *service);

/**
 * Stop the D-Bus service
 */
void dbus_service_stop(DbusService *service);

/**
 * Set callback for noise control mode change requests
 */
void dbus_service_set_noise_control_callback(DbusService *service,
                                              DbusNoiseControlCallback callback,
                                              void *user_data);

/**
 * Set callback for conversational awareness change requests
 */
void dbus_service_set_conv_awareness_callback(DbusService *service,
                                               DbusConvAwarenessCallback callback,
                                               void *user_data);

/**
 * Set callback for adaptive noise level change requests
 */
void dbus_service_set_adaptive_level_callback(DbusService *service,
                                               DbusAdaptiveLevelCallback callback,
                                               void *user_data);

/**
 * Emit DeviceConnected signal
 */
void dbus_service_emit_device_connected(DbusService *service,
                                         const char *address,
                                         const char *name);

/**
 * Emit DeviceDisconnected signal
 */
void dbus_service_emit_device_disconnected(DbusService *service,
                                            const char *address,
                                            const char *name);

/**
 * Emit BatteryChanged signal
 */
void dbus_service_emit_battery_changed(DbusService *service,
                                        int left, int right, int case_battery);

/**
 * Emit NoiseControlModeChanged signal
 */
void dbus_service_emit_noise_control_changed(DbusService *service,
                                              NoiseControlMode mode);

/**
 * Emit EarDetectionChanged signal
 */
void dbus_service_emit_ear_detection_changed(DbusService *service,
                                              bool left_in_ear,
                                              bool right_in_ear);

/**
 * Notify that a property has changed (emits PropertiesChanged)
 */
void dbus_service_emit_properties_changed(DbusService *service,
                                           const char *property_name);

#endif /* DBUS_SERVICE_H */
