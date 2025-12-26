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
