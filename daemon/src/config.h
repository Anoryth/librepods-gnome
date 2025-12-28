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
#include "airpods_state.h"

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

/**
 * Load listening modes for a specific device
 *
 * @param device_address Bluetooth MAC address of the device
 * @param modes Pointer to structure to fill with listening modes
 * @return true if found and loaded, false if not found (defaults used)
 */
bool config_load_device_listening_modes(const char *device_address, ListeningModesConfig *modes);

/**
 * Save listening modes for a specific device
 *
 * @param device_address Bluetooth MAC address of the device
 * @param modes Pointer to listening modes to save
 * @return true on success
 */
bool config_save_device_listening_modes(const char *device_address, const ListeningModesConfig *modes);

/**
 * Get default listening modes
 *
 * @param modes Pointer to structure to fill with defaults
 */
void config_get_default_listening_modes(ListeningModesConfig *modes);

#endif /* CONFIG_H */
