#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include "esp_err.h"
#include "cJSON.h"

#define SETTINGS_FILE "/spiffs/settings.json"
#define MAX_STR_LEN 64

// Initialize settings system
esp_err_t settings_init(void);

// Read functions
esp_err_t get_string_setting(const char *key, char *buffer, size_t buf_size);
int get_int_setting(const char *key, int default_value);
float get_float_setting(const char *key, float default_value);

// Write function
esp_err_t set_setting(const char *key, cJSON *value);

// Utility functions
void print_all_settings(void);
void write_settings_to_micropython(void);

#endif // SETTINGS_MANAGER_H