/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2024 LibrePods Contributors
 *
 * Configuration file management
 */

#include "config.h"
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#define CONFIG_DIR_NAME "librepods"
#define CONFIG_FILE_NAME "daemon.conf"
#define CONFIG_GROUP "Settings"

static gchar *get_config_dir(void)
{
    const gchar *config_home = g_get_user_config_dir();
    return g_build_filename(config_home, CONFIG_DIR_NAME, NULL);
}

static gchar *get_config_path(void)
{
    gchar *config_dir = get_config_dir();
    gchar *config_path = g_build_filename(config_dir, CONFIG_FILE_NAME, NULL);
    g_free(config_dir);
    return config_path;
}

static bool ensure_config_dir(void)
{
    gchar *config_dir = get_config_dir();
    int result = g_mkdir_with_parents(config_dir, 0755);
    g_free(config_dir);

    if (result != 0 && errno != EEXIST) {
        g_warning("Failed to create config directory: %s", g_strerror(errno));
        return false;
    }

    return true;
}

void config_get_defaults(LibrePodsConfig *config)
{
    config->ear_pause_mode = 1;  /* EAR_PAUSE_ONE_OUT */
}

bool config_load(LibrePodsConfig *config)
{
    /* Start with defaults */
    config_get_defaults(config);

    gchar *config_path = get_config_path();
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_NONE, &error)) {
        if (error->code != G_FILE_ERROR_NOENT) {
            g_warning("Failed to load config file: %s", error->message);
        }
        g_error_free(error);
        g_key_file_free(keyfile);
        g_free(config_path);

        /* Create default config file */
        config_save(config);
        return true;
    }

    /* Read settings */
    if (g_key_file_has_key(keyfile, CONFIG_GROUP, "ear_pause_mode", NULL)) {
        config->ear_pause_mode = g_key_file_get_integer(keyfile, CONFIG_GROUP, "ear_pause_mode", NULL);

        /* Validate range */
        if (config->ear_pause_mode < 0 || config->ear_pause_mode > 2) {
            config->ear_pause_mode = 1;
        }
    }

    g_message("Config loaded: ear_pause_mode=%d", config->ear_pause_mode);

    g_key_file_free(keyfile);
    g_free(config_path);
    return true;
}

bool config_save(const LibrePodsConfig *config)
{
    if (!ensure_config_dir()) {
        return false;
    }

    GKeyFile *keyfile = g_key_file_new();

    /* Write settings */
    g_key_file_set_integer(keyfile, CONFIG_GROUP, "ear_pause_mode", config->ear_pause_mode);

    /* Add comment */
    g_key_file_set_comment(keyfile, CONFIG_GROUP, NULL,
                           "LibrePods daemon configuration\n"
                           "ear_pause_mode: 0=disabled, 1=pause when one removed, 2=pause when both removed",
                           NULL);

    gchar *config_path = get_config_path();
    GError *error = NULL;

    if (!g_key_file_save_to_file(keyfile, config_path, &error)) {
        g_warning("Failed to save config file: %s", error->message);
        g_error_free(error);
        g_key_file_free(keyfile);
        g_free(config_path);
        return false;
    }

    g_message("Config saved: ear_pause_mode=%d", config->ear_pause_mode);

    g_key_file_free(keyfile);
    g_free(config_path);
    return true;
}

/* ============================================================================
 * Per-device listening modes configuration
 * ========================================================================== */

#define DEVICES_FILE_NAME "devices.conf"

static gchar *get_devices_config_path(void)
{
    gchar *config_dir = get_config_dir();
    gchar *config_path = g_build_filename(config_dir, DEVICES_FILE_NAME, NULL);
    g_free(config_dir);
    return config_path;
}

/* Convert MAC address to group name (replace : with _) */
static gchar *address_to_group(const char *address)
{
    gchar *group = g_strdup(address);
    for (gchar *p = group; *p; p++) {
        if (*p == ':') *p = '_';
    }
    return group;
}

void config_get_default_listening_modes(ListeningModesConfig *modes)
{
    /* Default: ANC and Transparency enabled (like Apple defaults) */
    modes->off_enabled = false;
    modes->transparency_enabled = true;
    modes->anc_enabled = true;
    modes->adaptive_enabled = false;
}

bool config_load_device_listening_modes(const char *device_address, ListeningModesConfig *modes)
{
    /* Start with defaults */
    config_get_default_listening_modes(modes);

    if (device_address == NULL || device_address[0] == '\0') {
        return false;
    }

    gchar *config_path = get_devices_config_path();
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_NONE, &error)) {
        if (error->code != G_FILE_ERROR_NOENT) {
            g_warning("Failed to load devices config: %s", error->message);
        }
        g_error_free(error);
        g_key_file_free(keyfile);
        g_free(config_path);
        return false;
    }

    gchar *group = address_to_group(device_address);

    if (!g_key_file_has_group(keyfile, group)) {
        g_key_file_free(keyfile);
        g_free(config_path);
        g_free(group);
        return false;
    }

    /* Read listening modes */
    if (g_key_file_has_key(keyfile, group, "listening_mode_off", NULL)) {
        modes->off_enabled = g_key_file_get_boolean(keyfile, group, "listening_mode_off", NULL);
    }
    if (g_key_file_has_key(keyfile, group, "listening_mode_transparency", NULL)) {
        modes->transparency_enabled = g_key_file_get_boolean(keyfile, group, "listening_mode_transparency", NULL);
    }
    if (g_key_file_has_key(keyfile, group, "listening_mode_anc", NULL)) {
        modes->anc_enabled = g_key_file_get_boolean(keyfile, group, "listening_mode_anc", NULL);
    }
    if (g_key_file_has_key(keyfile, group, "listening_mode_adaptive", NULL)) {
        modes->adaptive_enabled = g_key_file_get_boolean(keyfile, group, "listening_mode_adaptive", NULL);
    }

    g_message("Loaded listening modes for %s: off=%d, transparency=%d, anc=%d, adaptive=%d",
              device_address,
              modes->off_enabled, modes->transparency_enabled,
              modes->anc_enabled, modes->adaptive_enabled);

    g_key_file_free(keyfile);
    g_free(config_path);
    g_free(group);
    return true;
}

bool config_save_device_listening_modes(const char *device_address, const ListeningModesConfig *modes)
{
    if (device_address == NULL || device_address[0] == '\0') {
        g_warning("Cannot save listening modes: no device address");
        return false;
    }

    if (!ensure_config_dir()) {
        return false;
    }

    gchar *config_path = get_devices_config_path();
    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;

    /* Load existing file if present */
    g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_KEEP_COMMENTS, NULL);

    gchar *group = address_to_group(device_address);

    /* Write listening modes */
    g_key_file_set_boolean(keyfile, group, "listening_mode_off", modes->off_enabled);
    g_key_file_set_boolean(keyfile, group, "listening_mode_transparency", modes->transparency_enabled);
    g_key_file_set_boolean(keyfile, group, "listening_mode_anc", modes->anc_enabled);
    g_key_file_set_boolean(keyfile, group, "listening_mode_adaptive", modes->adaptive_enabled);

    if (!g_key_file_save_to_file(keyfile, config_path, &error)) {
        g_warning("Failed to save devices config: %s", error->message);
        g_error_free(error);
        g_key_file_free(keyfile);
        g_free(config_path);
        g_free(group);
        return false;
    }

    g_message("Saved listening modes for %s: off=%d, transparency=%d, anc=%d, adaptive=%d",
              device_address,
              modes->off_enabled, modes->transparency_enabled,
              modes->anc_enabled, modes->adaptive_enabled);

    g_key_file_free(keyfile);
    g_free(config_path);
    g_free(group);
    return true;
}
