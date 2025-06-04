#include "settings_manager.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "settings";
static SemaphoreHandle_t settings_mutex = NULL;

// Forward declarations
static cJSON* read_settings_file(void);
static esp_err_t write_settings_file(cJSON *settings);

esp_err_t settings_init(void) {
    // Initialize SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Create mutex
    settings_mutex = xSemaphoreCreateMutex();
    if (settings_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create settings mutex");
        return ESP_FAIL;
    }
    
    // Check if settings file exists
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Settings file not found, creating default");
        
        // Create default settings structure
        cJSON *root = cJSON_CreateObject();
        if (root == NULL) {
            ESP_LOGE(TAG, "Failed to create JSON object");
            return ESP_FAIL;
        }
        
        cJSON_AddStringToObject(root, "broker_uri", "mqtt://138.68.88.247:1883");
        cJSON_AddStringToObject(root, "client_id", "lfmp1");
        cJSON_AddStringToObject(root, "mqtt_username", "ondroid-iot");
        cJSON_AddStringToObject(root, "mqtt_password", "pQT1#TCeeWulV2PL");
        cJSON_AddStringToObject(root, "wifi_pass", "12345678");
        cJSON_AddStringToObject(root, "wifi_ssid", "ssid");
        cJSON_AddStringToObject(root, "topic_system", "lfmp_init/system");
        cJSON_AddStringToObject(root, "topic_python", "lfmp_init/python");
        
        // Save to file
        ret = write_settings_file(root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write default settings");
            return ret;
        }
    } else {
        fclose(f);
        ESP_LOGI(TAG, "Settings file found");
    }
    
    return ESP_OK;
}

static cJSON* read_settings_file(void) {
    if (settings_mutex == NULL) {
        ESP_LOGE(TAG, "Settings mutex not initialized");
        return NULL;
    }
    
    if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take settings mutex");
        return NULL;
    }
    
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open settings file for reading");
        xSemaphoreGive(settings_mutex);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld", size);
        fclose(f);
        xSemaphoreGive(settings_mutex);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    
    char *json_str = malloc(size + 1);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON string");
        fclose(f);
        xSemaphoreGive(settings_mutex);
        return NULL;
    }
    
    size_t bytes_read = fread(json_str, 1, size, f);
    json_str[bytes_read] = '\0';
    
    fclose(f);
    xSemaphoreGive(settings_mutex);
    
    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error: %s", error_ptr ? error_ptr : "unknown");
    }
    
    free(json_str);
    return json;
}

static esp_err_t write_settings_file(cJSON *settings) {
    if (settings_mutex == NULL) {
        ESP_LOGE(TAG, "Settings mutex not initialized");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take settings mutex");
        return ESP_FAIL;
    }
    
    char *json = cJSON_Print(settings);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        xSemaphoreGive(settings_mutex);
        return ESP_FAIL;
    }
    
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open settings file for writing");
        free(json);
        xSemaphoreGive(settings_mutex);
        return ESP_FAIL;
    }
    
    size_t json_len = strlen(json);
    size_t written = fwrite(json, 1, json_len, f);
    fclose(f);
    free(json);
    xSemaphoreGive(settings_mutex);
    
    if (written != json_len) {
        ESP_LOGE(TAG, "Failed to write complete JSON to file");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t set_setting(const char *key, cJSON *value) {
    if (key == NULL || value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for set_setting");
        if (value) cJSON_Delete(value);
        return ESP_FAIL;
    }
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to read settings file");
        cJSON_Delete(value);
        return ESP_FAIL;
    }
    
    // Remove existing key if present
    cJSON_DeleteItemFromObject(root, key);
    
    // Add new value
    cJSON_AddItemToObject(root, key, value);
    
    esp_err_t ret = write_settings_file(root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Successfully set setting: %s", key);
    } else {
        ESP_LOGE(TAG, "Failed to write setting: %s", key);
    }
    
    return ret;
}

esp_err_t get_string_setting(const char *key, char *buffer, size_t buf_size) {
    if (key == NULL || buffer == NULL || buf_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for get_string_setting");
        return ESP_FAIL;
    }
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to read settings file");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item)) {
        ESP_LOGW(TAG, "Setting '%s' not found or not a string", key);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    strncpy(buffer, item->valuestring, buf_size - 1);
    buffer[buf_size - 1] = '\0';
    
    cJSON_Delete(root);
    return ESP_OK;
}

int get_int_setting(const char *key, int default_value) {
    if (key == NULL) {
        ESP_LOGE(TAG, "Invalid key for get_int_setting");
        return default_value;
    }
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGW(TAG, "Failed to read settings file, returning default");
        return default_value;
    }
    
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "Setting '%s' not found or not a number", key);
        cJSON_Delete(root);
        return default_value;
    }
    
    int val = item->valueint;
    cJSON_Delete(root);
    return val;
}

float get_float_setting(const char *key, float default_value) {
    if (key == NULL) {
        ESP_LOGE(TAG, "Invalid key for get_float_setting");
        return default_value;
    }
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGW(TAG, "Failed to read settings file, returning default");
        return default_value;
    }
    
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "Setting '%s' not found or not a number", key);
        cJSON_Delete(root);
        return default_value;
    }
    
    float val = (float)item->valuedouble;
    cJSON_Delete(root);
    return val;
}

void print_all_settings(void) {
    ESP_LOGI(TAG, "=== CURRENT SETTINGS ===");
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGE(TAG, "SETTINGS FILE NOT FOUND OR CORRUPTED");
        return;
    }
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (item->string == NULL) continue;
        
        if (cJSON_IsString(item)) {
            ESP_LOGI(TAG, "%-20s = \"%s\" (string)", item->string, item->valuestring);
        } else if (cJSON_IsNumber(item)) {
            if (item->valuedouble == (double)item->valueint) {
                ESP_LOGI(TAG, "%-20s = %d (int)", item->string, item->valueint);
            } else {
                ESP_LOGI(TAG, "%-20s = %.6f (float)", item->string, item->valuedouble);
            }
        } else if (cJSON_IsBool(item)) {
            ESP_LOGI(TAG, "%-20s = %s (bool)", item->string, cJSON_IsTrue(item) ? "true" : "false");
        } else {
            ESP_LOGI(TAG, "%-20s = [complex type]", item->string);
        }
    }
    
    ESP_LOGI(TAG, "========================");
    cJSON_Delete(root);
}

void write_settings_to_micropython(void) {
    extern QueueHandle_t python_code_queue;
    
    cJSON *root = read_settings_file();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to read settings for MicroPython");
        return;
    }
    
    char *json_compact = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_compact == NULL) {
        ESP_LOGE(TAG, "Failed to serialize settings to JSON");
        return;
    }
    
    // Escape the JSON string for Python
    size_t escaped_len = strlen(json_compact) * 2 + 1; // Worst case
    char *escaped_json = malloc(escaped_len);
    if (escaped_json == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for escaped JSON");
        free(json_compact);
        return;
    }
    
    size_t out_pos = 0;
    for (size_t i = 0; json_compact[i] != '\0' && out_pos < escaped_len - 1; i++) {
        char c = json_compact[i];
        switch (c) {
            case '\'':
                if (out_pos < escaped_len - 2) {
                    escaped_json[out_pos++] = '\\';
                    escaped_json[out_pos++] = '\'';
                }
                break;
            case '\\':
                if (out_pos < escaped_len - 2) {
                    escaped_json[out_pos++] = '\\';
                    escaped_json[out_pos++] = '\\';
                }
                break;
            default:
                escaped_json[out_pos++] = c;
                break;
        }
    }
    escaped_json[out_pos] = '\0';
    
    // Create Python code to write settings file
    size_t py_code_len = strlen(escaped_json) + 200;
    char *py_code = malloc(py_code_len);
    if (py_code == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for Python code");
        free(json_compact);
        free(escaped_json);
        return;
    }
    
    snprintf(py_code, py_code_len,
        "try:\n"
        "    with open('settings.json', 'w') as f:\n"
        "        f.write('%s')\n"
        "    print('Settings file written successfully')\n"
        "except Exception as e:\n"
        "    print('Error writing settings file:', e)\n",
        escaped_json);
    
    free(json_compact);
    free(escaped_json);
    
    // Send to MicroPython execution queue
    if (python_code_queue != NULL) {
        if (xQueueSend(python_code_queue, &py_code, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send settings write code to Python queue");
            free(py_code);
        } else {
            ESP_LOGI(TAG, "Settings write code sent to MicroPython execution queue");
        }
    } else {
        ESP_LOGE(TAG, "Python code queue not initialized");
        free(py_code);
    }
}