/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2024 LibrePods Contributors
 *
 * Configuration file management
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>
#include <stdbool.h>

/* Configuration data structure */
typedef struct {
    int ear_pause_mode;   /* 0=disabled, 1=one_out, 2=both_out */
} LibrePodsConfig;

/**
 * Load configuration from file
 * Creates default config if file doesn't exist
 *
 * @param config Pointer to config structure to fill
 * @return true on success
 */
bool config_load(LibrePodsConfig *config);

/**
 * Save configuration to file
 *
 * @param config Pointer to config structure to save
 * @return true on success
 */
bool config_save(const LibrePodsConfig *config);

/**
 * Get default configuration values
 *
 * @param config Pointer to config structure to fill with defaults
 */
void config_get_defaults(LibrePodsConfig *config);

#endif /* CONFIG_H */
