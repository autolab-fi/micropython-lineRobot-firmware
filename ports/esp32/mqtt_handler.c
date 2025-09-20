#include "mqtt_handler.h"
#include "settings_manager.h"
#include "uart_handler.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_partition.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "driver/uart.h"
#include "cJSON.h"

// WiFi configuration
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define OTA_BUFFER_SIZE 4096
#define OTA_MAX_RETRIES 3
#define OTA_RETRY_DELAY 5000  // 5 seconds

// Topic buffers
#define MAX_STR_LEN 64
static char MQTT_SYSTEM_INPUT_TOPIC[MAX_STR_LEN*2];
static char MQTT_SYSTEM_OUTPUT_TOPIC[MAX_STR_LEN*2];
static char MQTT_PYTHON_OUTPUT_TOPIC[MAX_STR_LEN*2];

// WiFi and MQTT variables
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static const char *TAG = "mqtt_handler";

// External flag for ADC measurement
extern bool get_measure_adc_flag(void);
extern void set_measure_adc_flag(bool value);



// OTA status variables
static bool ota_in_progress = false;
static int ota_progress = 0;

// Watchdog variables
#define WDT_TIMEOUT_SEC 20  // 20 seconds watchdog timeout


char* escape_json_string(const char* input) {
    if (!input) return NULL;
    
    size_t input_len = strlen(input);
    // Worst case: every character needs escaping
    char* output = malloc(input_len * 2 + 1);
    if (!output) return NULL;
    
    size_t out_pos = 0;
    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        switch (c) {
            case '"':
                output[out_pos++] = '\\';
                output[out_pos++] = '"';
                break;
            case '\\':
                output[out_pos++] = '\\';
                output[out_pos++] = '\\';
                break;
            case '\n':
                output[out_pos++] = '\\';
                output[out_pos++] = 'n';
                break;
            case '\r':
                output[out_pos++] = '\\';
                output[out_pos++] = 'r';
                break;
            case '\t':
                output[out_pos++] = '\\';
                output[out_pos++] = 't';
                break;
            default:
                output[out_pos++] = c;
                break;
        }
    }
    output[out_pos] = '\0';
    return output;
}


// Initialize watchdog timer
static void init_watchdog() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Watch all cores
        .trigger_panic = true
    };
    ESP_LOGI(TAG, "Watchdog 1 initialized with %d second timeout", WDT_TIMEOUT_SEC);
    esp_task_wdt_reconfigure(&wdt_config);

    esp_task_wdt_add(NULL); // Add current task to watchdog
    ESP_LOGI(TAG, "Watchdog 2 initialized with %d second timeout", WDT_TIMEOUT_SEC);
}
// Reset watchdog timer
static void reset_watchdog() {
   // ESP_LOGI(TAG, "Watchdog reset");
    esp_task_wdt_reset();
}

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// OTA update function
// Improved OTA update function with retry logic and state validation
static void perform_ota_update(const char *url)
{
    if (ota_in_progress) {
        ESP_LOGE(TAG, "OTA update already in progress");
        return;
    }

    ota_in_progress = true;
    ESP_LOGI(TAG, "Starting OTA update from URL: %s", url);

    // Check and fix OTA state before starting
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Current firmware in pending verify state, marking as valid");
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to mark current firmware as valid: %s", esp_err_to_name(err));
                esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                       "{\"status\":\"ota_failed\",\"error\":\"cannot_validate_current_firmware\"}", 0, 1, 0);
                goto ota_end;
            }
            ESP_LOGI(TAG, "Current firmware marked as valid, proceeding with OTA");
        }
    }

    // Publish status
    esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                           "{\"status\":\"ota_started\"}", 0, 1, 0);

    int retry_count = 0;
    bool ota_success = false;

    while (retry_count < OTA_MAX_RETRIES && !ota_success) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "OTA retry attempt %d/%d", retry_count + 1, OTA_MAX_RETRIES);
            char retry_msg[128];
            snprintf(retry_msg, sizeof(retry_msg), 
                    "{\"status\":\"ota_retry\",\"attempt\":%d}", retry_count + 1);
            esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, retry_msg, 0, 1, 0);
            vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
        }

        // Enhanced HTTP client configuration
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = ota_http_event_handler,
            .timeout_ms = 120000,  // Increased timeout to 2 minutes
            .buffer_size = OTA_BUFFER_SIZE,
            .buffer_size_tx = OTA_BUFFER_SIZE,
            .keep_alive_enable = true,
            .keep_alive_idle = 5,
            .keep_alive_interval = 5,
            .keep_alive_count = 3,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to initialize HTTP connection");
            retry_count++;
            continue;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        int content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "Failed to fetch headers");
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        ESP_LOGI(TAG, "Content-Length: %d bytes", content_length);

        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Failed to find update partition");
            esp_http_client_cleanup(client);
            esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                   "{\"status\":\"ota_failed\",\"error\":\"no_update_partition\"}", 0, 1, 0);
            goto ota_end;
        }

        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx, size: %lu",
                 update_partition->subtype, update_partition->address, update_partition->size);

        // Check if partition is large enough
        if (content_length > 0 && (size_t)content_length > update_partition->size) {
            ESP_LOGE(TAG, "Firmware size (%d) exceeds partition size (%lu)", 
                     content_length, update_partition->size);
            esp_http_client_cleanup(client);
            esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                   "{\"status\":\"ota_failed\",\"error\":\"firmware_too_large\"}", 0, 1, 0);
            goto ota_end;
        }

        esp_ota_handle_t update_handle = 0;
        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        int data_read;
        char ota_write_data[OTA_BUFFER_SIZE];
        int total_bytes = 0;
        int last_progress_report = 0;
        bool download_success = true;
        
        while ((data_read = esp_http_client_read(client, ota_write_data, OTA_BUFFER_SIZE)) > 0) {
            err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                download_success = false;
                break;
            }
            total_bytes += data_read;
            
            // Publish progress every 64KB
            if (total_bytes - last_progress_report >= 65536) {
                char progress_msg[128];
                int progress_percent = content_length > 0 ? (total_bytes * 100) / content_length : 0;
                snprintf(progress_msg, sizeof(progress_msg), 
                        "{\"status\":\"ota_progress\",\"bytes\":%d,\"percent\":%d}", 
                        total_bytes, progress_percent);
                esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, progress_msg, 0, 1, 0);
                last_progress_report = total_bytes;
            }
            
            // Watchdog reset
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: HTTP data read error, errno: %d", errno);
            download_success = false;
        }

        if (!download_success) {
            esp_ota_end(update_handle);
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        // Validate downloaded size
        if (content_length > 0 && total_bytes != content_length) {
            ESP_LOGE(TAG, "Downloaded size (%d) doesn't match content length (%d)", 
                     total_bytes, content_length);
            esp_ota_end(update_handle);
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        ESP_LOGI(TAG, "Total bytes downloaded: %d", total_bytes);

        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            retry_count++;
            continue;
        }

        ESP_LOGI(TAG, "OTA update successful, total bytes: %d", total_bytes);
        esp_http_client_cleanup(client);
        ota_success = true;
    }

    if (ota_success) {
        // Publish success and restart
        esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                               "{\"status\":\"ota_success\",\"restarting\":true}", 0, 1, 0);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for message to be sent
        esp_restart();
    } else {
        // All retries failed
        ESP_LOGE(TAG, "OTA update failed after %d attempts", OTA_MAX_RETRIES);
        esp_mqtt_client_publish(mqtt_client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                               "{\"status\":\"ota_failed\",\"error\":\"max_retries_exceeded\"}", 0, 1, 0);
    }

ota_end:
    ota_in_progress = false;
}

// OTA task wrapper
static void ota_task(void *pvParameter)
{
    char *url = (char *)pvParameter;
    perform_ota_update(url);
    free(url);
    vTaskDelete(NULL);
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "WiFi EVENT type %s id %d", event_base, (int)event_id);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        
        // Subscribe to topic
        msg_id = esp_mqtt_client_subscribe(client, MQTT_SYSTEM_INPUT_TOPIC, 1);
        ESP_LOGI(TAG, "sent subscribe to command topic, msg_id=%d", msg_id);
        
        // Publish status
        char response[64];
        snprintf(response, sizeof(response), "{\"type\":\"hello\", \"msg\":\"version 20.09.2025\"}");
        msg_id = esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
        ESP_LOGI(TAG, "sent status publish, msg_id=%d", msg_id);
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        
        // Process JSON commands
        if (strncmp(event->topic, MQTT_SYSTEM_INPUT_TOPIC, event->topic_len) == 0) {
            cJSON *json = cJSON_ParseWithLength(event->data, event->data_len);
            if (json == NULL) {
                const char *error_ptr = cJSON_GetErrorPtr();
                if (error_ptr != NULL) {
                    ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
                }
                break;
            }
            
            // Handle ping command
            cJSON *command = cJSON_GetObjectItemCaseSensitive(json, "command");
            if (cJSON_IsString(command) && (command->valuestring != NULL)) {
                if (strcmp(command->valuestring, "ping") == 0) {
                    // Send pong response
                    char response[64];
                    snprintf(response, sizeof(response), "{\"msg\":\"pong\"}");
                    esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
                    ESP_LOGI(TAG, "Responded to ping command");
                }
                else if (strcmp(command->valuestring, "py") == 0) {
                    // Handle Python code
                    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "value");
                    if (cJSON_IsString(value) && (value->valuestring != NULL)) {
                        char *code_copy = strdup(value->valuestring);
                        if (code_copy != NULL) {
                            if (xQueueSend(python_code_queue, &code_copy, pdMS_TO_TICKS(1000)) != pdTRUE) {
                                ESP_LOGE(TAG, "Failed to send Python code to queue");
                                free(code_copy);
                            } else {
                                ESP_LOGI(TAG, "Python code sent to execution queue: %s", code_copy);
                            }
                        }
                    }
                }                    
                else if (strcmp(command->valuestring, "ota-update") == 0) {
                    if (ota_in_progress) {
                        esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                               "{\"status\":\"error\",\"message\":\"OTA already in progress\"}", 0, 1, 0);
                        ESP_LOGE(TAG, "OTA update already in progress");
                    } else {
                        // Handle OTA update
                        cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "url");
                        if (cJSON_IsString(url) && (url->valuestring != NULL)) {
                            char *url_copy = strdup(url->valuestring);
                            if (url_copy != NULL) {
                                // Start OTA in a separate task
                                BaseType_t result = xTaskCreate(
                                    ota_task,
                                    "ota_task",
                                    16384,  // Increased stack size
                                    url_copy,
                                    5,
                                    NULL
                                );
                                if (result == pdPASS) {
                                    ESP_LOGI(TAG, "OTA update task created for URL: %s", url_copy);
                                } else {
                                    ESP_LOGE(TAG, "Failed to create OTA task");
                                    free(url_copy);
                                    esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                                           "{\"status\":\"error\",\"message\":\"Failed to create OTA task\"}", 0, 1, 0);
                                }
                            }
                        }
                    }
                }
                else if (strcmp(command->valuestring, "restart") == 0) {
                    // Reset ESP32
                    esp_restart();
                }
                else if (strcmp(command->valuestring, "set-coeff") == 0) {
                    cJSON *value_f = cJSON_GetObjectItemCaseSensitive(json, "value");
                    cJSON *name_f = cJSON_GetObjectItemCaseSensitive(json, "name");
                    cJSON *type_f = cJSON_GetObjectItemCaseSensitive(json, "type");
                    char response[128];
                    
                    
                    if (cJSON_IsString(name_f) && (name_f->valuestring != NULL) && cJSON_IsString(type_f) && (type_f->valuestring != NULL)) {
                        char *name = strdup(name_f->valuestring);
                        char *type = strdup(type_f->valuestring);
                        
                        if (strcmp(type, "float") == 0 && cJSON_IsNumber(value_f)) {
                            float value = value_f->valuedouble;
                            set_setting(name, cJSON_CreateNumber(value));
                            ESP_LOGI(TAG, "Set %s to %f",name, value);
                            snprintf(response, sizeof(response), "{\"msg\":\"Set %s to %f\"}", name, value);
                        }
                        else if (cJSON_IsString(value_f) && (value_f->valuestring != NULL) && strcmp(type, "string") == 0) {
                            char *value = value_f->valuestring;
                            set_setting(name, cJSON_CreateString(value));
                            ESP_LOGI(TAG, "Set %s to %s", name, value);
                            snprintf(response, sizeof(response), "{\"msg\":\"Set %s to %s\"}", name, value);
                        } 
                        else if (cJSON_IsNumber(value_f) && strcmp(type, "int") == 0) {
                            int value = value_f->valueint;
                            set_setting(name, cJSON_CreateNumber(value));
                            ESP_LOGI(TAG, "Set %s to %d", name, value);
                            snprintf(response, sizeof(response), "{\"msg\":\"Set %s to %d\"}", name, value);
                        }else{
                            ESP_LOGE(TAG, "Invalid value type");
                            snprintf(response, sizeof(response), "{\"msg\":\"Invalid value type\"}");
                        }
                        
                        free(name);
                        free(type);
                    } else {
                        snprintf(response, sizeof(response), "{\"msg\":\"error\"}");
                    }
                    esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
                }
                else if (strcmp(command->valuestring, "get-coeff") == 0) {
                    cJSON *type_f = cJSON_GetObjectItemCaseSensitive(json, "type");
                    cJSON *name_f = cJSON_GetObjectItemCaseSensitive(json, "name");
                    char response[128];
                    if (cJSON_IsString(name_f) && (name_f->valuestring != NULL) && cJSON_IsString(type_f) && (type_f->valuestring != NULL)) {
                        char *name = strdup(name_f->valuestring);
                        char *type = strdup(type_f->valuestring);
                        if (strcmp(type, "float") == 0) {
                            float value = get_float_setting(name, -1.0f);
                            ESP_LOGI(TAG, "Value of %s: %f\n", name, value);
                            snprintf(response, sizeof(response), "{\"msg\":\"Value of %s: %f\"}", name, value);
                        }
                        else if (strcmp(type, "string") == 0) {
                            char value[MAX_STR_LEN];
                            if (get_string_setting(name, value, sizeof(value)) == ESP_OK) {
                                ESP_LOGI(TAG, "Value of %s: %s\n", name, value);
                                snprintf(response, sizeof(response), "{\"msg\":\"Value of %s: %s\"}", name, value);
                            } else {
                                ESP_LOGE(TAG, "Failed to get string setting %s", name);
                                snprintf(response, sizeof(response), "{\"msg\":\"Failed to get string setting %s\"}", name);
                            }
                        } 
                        else if (strcmp(type, "int") == 0) {
                            int value = get_int_setting(name, -1);
                            ESP_LOGI(TAG, "Value of %s: %d\n", name, value);
                            snprintf(response, sizeof(response), "{\"msg\":\"Value of %s: %d\"}", name, value);
                        } else {
                            snprintf(response, sizeof(response), "{\"msg\":\"Invalid value type\"}");
                        }
                        free(name);
                        free(type);
                    } else {
                        snprintf(response, sizeof(response), "{\"msg\":\"error\"}");
                    }
                    esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
                }
                else if (strcmp(command->valuestring, "test-movement") == 0) {
                    char *code = strdup("from test_robot_lib import test\ntest()");
                    if (code != NULL) {
                        if (xQueueSend(python_code_queue, &code, pdMS_TO_TICKS(1000)) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to send Python code to queue");
                            free(code);
                        } else {
                            ESP_LOGI(TAG, "Python code sent to execution queue: %s", code);
                        }
                    }
                }
                else if (strcmp(command->valuestring, "test-line-sensor") == 0) {
                    char *code = strdup("from test_octoliner import test\ntest()");
                    if (code != NULL) {
                        if (xQueueSend(python_code_queue, &code, pdMS_TO_TICKS(1000)) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to send Python code to queue");
                            free(code);
                        } else {
                            ESP_LOGI(TAG, "Python code sent to execution queue: %s", code);
                        }
                    }
                }
                else if (strcmp(command->valuestring, "test-color-sensor") == 0) {
                    char *code = strdup("from test_tcs import test\ntest()");
                    if (code != NULL) {
                        if (xQueueSend(python_code_queue, &code, pdMS_TO_TICKS(1000)) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to send Python code to queue");
                            free(code);
                        } else {
                            ESP_LOGI(TAG, "Python code sent to execution queue: %s", code);
                        }
                    }
                }
                else if (strcmp(command->valuestring, "test-scan-i2c") == 0) {
                    char *code = strdup("from scan import scan\nscan()");
                    if (code != NULL) {
                        if (xQueueSend(python_code_queue, &code, pdMS_TO_TICKS(1000)) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to send Python code to queue");
                            free(code);
                        } else {
                            ESP_LOGI(TAG, "Python code sent to execution queue: %s", code);
                        }
                    }
                }
                else if (strcmp(command->valuestring, "battery-status") == 0) {
                    set_measure_adc_flag(true);
                }
                else if (strcmp(command->valuestring, "mark-valid") == 0) {
                    // Mark current firmware as valid
                    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                    if (err == ESP_OK) {
                        esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, 
                                               "{\"status\":\"success\",\"message\":\"Firmware marked as valid\"}", 0, 1, 0);
                        ESP_LOGI(TAG, "Firmware marked as valid");
                    } else {
                        char error_msg[128];
                        snprintf(error_msg, sizeof(error_msg), 
                                "{\"status\":\"error\",\"message\":\"Failed to mark valid: %s\"}", 
                                esp_err_to_name(err));
                        esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, error_msg, 0, 1, 0);
                        ESP_LOGE(TAG, "Failed to mark firmware as valid: %s", esp_err_to_name(err));
                    }
                }
                
            }
            
            cJSON_Delete(json);
        }
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
        
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// WiFi initialization
static void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    &instance_got_ip));

    char wssid[MAX_STR_LEN];
    char wpass[MAX_STR_LEN];
    
    get_string_setting("wifi_ssid", wssid, sizeof(wssid));
    get_string_setting("wifi_pass", wpass, sizeof(wpass));

    wifi_config_t wifi_config = {
       .sta = {
           .threshold = {
               .authmode = WIFI_AUTH_WPA2_PSK
           },
       },
    };

    // Copy the strings into the wifi_config structure
    strncpy((char*)wifi_config.sta.ssid, wssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, wpass, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 wssid, wpass);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                wssid, wpass);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

// MQTT task running on core 1
void mqtt_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting MQTT task on core %d", xPortGetCoreID());

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s at 0x%lx", running->label, running->address);

    // Check OTA state and mark as valid if needed
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        ESP_LOGI(TAG, "Current OTA state: %d", ota_state);
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Pending verification detected, marking current firmware as valid");
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Current firmware marked as valid");
            } else {
                ESP_LOGE(TAG, "Failed to mark firmware as valid: %s", esp_err_to_name(err));
            }
        }
    }
    
    // Initialize WiFi
    wifi_init_sta();
    
    // Initialize MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {};
    char broker_uri[MAX_STR_LEN];
    char mqtt_username[MAX_STR_LEN];
    char mqtt_password[MAX_STR_LEN];
    char client_id[MAX_STR_LEN];
    get_string_setting("broker_uri", broker_uri, sizeof(broker_uri));
    get_string_setting("mqtt_username", mqtt_username, sizeof(mqtt_username));
    get_string_setting("mqtt_password", mqtt_password, sizeof(mqtt_password));
    get_string_setting("client_id", client_id, sizeof(client_id));
    mqtt_cfg.broker.address.uri = broker_uri;
    mqtt_cfg.credentials.username = mqtt_username;
    mqtt_cfg.credentials.authentication.password = mqtt_password;
    mqtt_cfg.credentials.client_id = client_id;
    
    char topic_system[MAX_STR_LEN];
    char topic_python[MAX_STR_LEN];
    get_string_setting("topic_system", topic_system, sizeof(topic_system));
    get_string_setting("topic_python", topic_python, sizeof(topic_python));
    snprintf(MQTT_PYTHON_OUTPUT_TOPIC, MAX_STR_LEN*2, "%s/output", topic_python);
    snprintf(MQTT_SYSTEM_OUTPUT_TOPIC, MAX_STR_LEN*2, "%s/output", topic_system);
    snprintf(MQTT_SYSTEM_INPUT_TOPIC, MAX_STR_LEN*2, "%s/input", topic_system);
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    
    init_watchdog();
    // Initialize UART handler
    // uart_handler_init();
    
    char buffer[256];
    size_t received_len;
    
    while (1) {
        reset_watchdog();
        // check watchdog
        //vTaskDelay(pdMS_TO_TICKS(15000));
        // Handle MQTT print stream
        received_len = xStreamBufferReceive(
            mqtt_print_stream,
            buffer,
            sizeof(buffer)-1,
            pdMS_TO_TICKS(10)
        );

        if (received_len > 3 && strncmp(buffer, "SYS", 3) == 0) {
            // Создаем буфер с учетом нуль-терминатора
            char bat[received_len - 3 + 1]; // +1 для '\0'
            strncpy(bat, buffer + 3, received_len - 3);
            bat[received_len - 3] = '\0'; // Явно добавляем нуль-терминатор
            
            printf("%s\n", bat);
            esp_mqtt_client_publish(
                mqtt_client,
                MQTT_SYSTEM_OUTPUT_TOPIC,
                bat,
                0, 1, 0
            );
            received_len = 0;
            continue;
        }

        if (received_len > 0) {
            buffer[received_len] = '\0';
            
            // Check for "non-empty" message
            for (size_t i = 0; i < received_len; i++) {
                if (buffer[i] > ' ' && buffer[i] != '\n' && buffer[i] != '\r') {
                    esp_mqtt_client_publish(
                        mqtt_client,
                        MQTT_PYTHON_OUTPUT_TOPIC,
                        buffer,
                        0, 1, 0
                    );
                    break; // Sent and exit check
                }
            }
        }
    }
}