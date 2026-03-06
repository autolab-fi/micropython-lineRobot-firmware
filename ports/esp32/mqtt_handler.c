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
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "driver/uart.h"
#include "modmachine.h"
#include "cJSON.h"

// Recovery configuration
#define WIFI_RECONNECT_BACKOFF_STEPS           5
#define WIFI_RECONNECT_BACKOFF_MAX_MS          30000
#define WIFI_IP_RECOVERY_TIMEOUT_MS            (5 * 60 * 1000)
#define MQTT_RECOVERY_TIMEOUT_MS               (3 * 60 * 1000)
#define OFFLINE_RESTART_TIMEOUT_MS             (15 * 60 * 1000)
#define RECOVERY_GUARD_COOLDOWN_MS             5000
#define RECOVERY_STATUS_LOG_INTERVAL_MS         5000
#define ENABLE_OFFLINE_FALLBACK_RESTART        1
#define WIFI_INITIAL_CONNECT_WAIT_MS            15000

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0

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
static esp_mqtt_client_handle_t mqtt_client = NULL;
static const char *TAG = "mqtt_handler";

static const uint32_t WIFI_RECONNECT_BACKOFF_MS[WIFI_RECONNECT_BACKOFF_STEPS] = {1000, 2000, 5000, 10000, WIFI_RECONNECT_BACKOFF_MAX_MS};

typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool recovery_guard;
    TickType_t boot_tick;
    TickType_t last_wifi_disconnect_tick;
    TickType_t last_ip_tick;
    TickType_t last_mqtt_disconnect_tick;
    TickType_t next_wifi_reconnect_tick;
    TickType_t last_recovery_action_tick;
    TickType_t last_status_log_tick;
    uint32_t wifi_reconnect_attempt;
    int32_t last_disconnect_reason;
} connection_recovery_state_t;

static connection_recovery_state_t s_recovery = {0};

static char s_broker_uri[MAX_STR_LEN] = {0};
static char s_mqtt_username[MAX_STR_LEN] = {0};
static char s_mqtt_password[MAX_STR_LEN] = {0};
static char s_client_id[MAX_STR_LEN] = {0};

static uint32_t elapsed_ms_since(TickType_t from_tick) {
    return (uint32_t)(pdTICKS_TO_MS(xTaskGetTickCount() - from_tick));
}

static uint32_t elapsed_ms_between(TickType_t now, TickType_t from_tick)
{
    return (uint32_t)pdTICKS_TO_MS(now - from_tick);
}

static void log_recovery_timers(TickType_t now)
{
    if (elapsed_ms_between(now, s_recovery.last_status_log_tick) < RECOVERY_STATUS_LOG_INTERVAL_MS) {
        return;
    }
    s_recovery.last_status_log_tick = now;

    uint32_t to_l1_ms = 0;
    if (s_recovery.next_wifi_reconnect_tick > now) {
        to_l1_ms = (uint32_t)pdTICKS_TO_MS(s_recovery.next_wifi_reconnect_tick - now);
    }

    uint32_t elapsed_ip_ms = elapsed_ms_between(now, s_recovery.last_ip_tick);
    uint32_t to_l2_ms = 0;
    if (elapsed_ip_ms < WIFI_IP_RECOVERY_TIMEOUT_MS) {
        to_l2_ms = WIFI_IP_RECOVERY_TIMEOUT_MS - elapsed_ip_ms;
    }

    bool l3_active = s_recovery.wifi_connected && !s_recovery.mqtt_connected;
    uint32_t to_l3_ms = 0;
    if (l3_active) {
        uint32_t elapsed_mqtt_ms = elapsed_ms_between(now, s_recovery.last_mqtt_disconnect_tick);
        if (elapsed_mqtt_ms < MQTT_RECOVERY_TIMEOUT_MS) {
            to_l3_ms = MQTT_RECOVERY_TIMEOUT_MS - elapsed_mqtt_ms;
        }
    }

    uint32_t offline_elapsed_ms = elapsed_ip_ms;
    uint32_t to_l4_ms = 0;
    if (offline_elapsed_ms < OFFLINE_RESTART_TIMEOUT_MS) {
        to_l4_ms = OFFLINE_RESTART_TIMEOUT_MS - offline_elapsed_ms;
    }

    if (l3_active) {
        ESP_LOGI(TAG,
                 "RecoveryStatus: wifi=%d mqtt=%d guard=%d attempt=%u reason=%ld toL1=%ums toL2=%ums toL3=%ums toL4=%ums",
                 s_recovery.wifi_connected,
                 s_recovery.mqtt_connected,
                 s_recovery.recovery_guard,
                 (unsigned)s_recovery.wifi_reconnect_attempt,
                 (long)s_recovery.last_disconnect_reason,
                 (unsigned)to_l1_ms,
                 (unsigned)to_l2_ms,
                 (unsigned)to_l3_ms,
                 (unsigned)to_l4_ms);
    } else {
        ESP_LOGI(TAG,
                 "RecoveryStatus: wifi=%d mqtt=%d guard=%d attempt=%u reason=%ld toL1=%ums toL2=%ums toL3=n/a toL4=%ums",
                 s_recovery.wifi_connected,
                 s_recovery.mqtt_connected,
                 s_recovery.recovery_guard,
                 (unsigned)s_recovery.wifi_reconnect_attempt,
                 (long)s_recovery.last_disconnect_reason,
                 (unsigned)to_l1_ms,
                 (unsigned)to_l2_ms,
                 (unsigned)to_l4_ms);
    }
}

static TickType_t backoff_to_ticks(uint32_t backoff_ms)
{
    return pdMS_TO_TICKS(backoff_ms);
}

static bool broker_uses_domain(const char *uri, char *host, size_t host_size)
{
    if (!uri || !host || host_size == 0) {
        return false;
    }

    const char *scheme = strstr(uri, "://");
    const char *start = scheme ? scheme + 3 : uri;
    const char *end = start;
    while (*end && *end != ':' && *end != '/' && *end != '?') {
        end++;
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= host_size) {
        return false;
    }

    memcpy(host, start, len);
    host[len] = '\0';

    for (size_t i = 0; i < len; i++) {
        if ((host[i] < '0' || host[i] > '9') && host[i] != '.') {
            return true;
        }
    }

    return false;
}

static void log_broker_dns_resolution(void)
{
    char host[128] = {0};
    if (!broker_uses_domain(s_broker_uri, host, sizeof(host))) {
        return;
    }

    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, NULL, &hints, &result);
    if (err != 0) {
        ESP_LOGW(TAG, "DNS lookup failed for broker host %s: %d", host, err);
        return;
    }

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        char ipstr[INET6_ADDRSTRLEN] = {0};
        void *addr_ptr = NULL;
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr_ptr = &ipv4->sin_addr;
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr_ptr = &ipv6->sin6_addr;
        }

        if (addr_ptr && inet_ntop(rp->ai_family, addr_ptr, ipstr, sizeof(ipstr))) {
            ESP_LOGI(TAG, "Broker DNS resolved: host=%s family=%d addr=%s", host, rp->ai_family, ipstr);
        }
    }

    freeaddrinfo(result);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

static void restart_mqtt_client(void)
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = s_broker_uri;
    mqtt_cfg.credentials.username = s_mqtt_username;
    mqtt_cfg.credentials.authentication.password = s_mqtt_password;
    mqtt_cfg.credentials.client_id = s_client_id;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    s_recovery.last_mqtt_disconnect_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "MQTT client restarted");
}

// External flag for ADC measurement
extern bool get_measure_adc_flag(void);
extern void set_measure_adc_flag(bool value);



// OTA status variables
static bool ota_in_progress = false;
static int ota_progress = 0;

// Watchdog variables
#define WDT_TIMEOUT_SEC 20  // 20 seconds watchdog timeout


#define MAX_PARTIAL_MESSAGES 4

typedef struct {
    bool in_use;
    int msg_id;
    char *topic;
    size_t topic_len;
    char *buffer;
    size_t total_len;
    size_t received_len;
} partial_message_t;

static partial_message_t partial_messages[MAX_PARTIAL_MESSAGES];

static void ota_task(void *pvParameter);

static void reset_partial_message(partial_message_t *message) {
    if (!message) {
        return;
    }

    if (message->topic) {
        free(message->topic);
    }
    if (message->buffer) {
        free(message->buffer);
    }
    memset(message, 0, sizeof(*message));
}

static void reset_all_partial_messages(void) {
    for (size_t i = 0; i < MAX_PARTIAL_MESSAGES; i++) {
        if (partial_messages[i].in_use) {
            reset_partial_message(&partial_messages[i]);
        }
    }
}

static partial_message_t *find_partial_message(int msg_id, const char *topic, size_t topic_len) {
    for (size_t i = 0; i < MAX_PARTIAL_MESSAGES; i++) {
        partial_message_t *message = &partial_messages[i];
        if (!message->in_use) {
            continue;
        }
        if (message->msg_id == msg_id) {
            return message;
        }
        if (topic && message->topic && message->topic_len == topic_len && strncmp(message->topic, topic, topic_len) == 0) {
            return message;
        }
    }
    return NULL;
}

static partial_message_t *start_partial_message(int msg_id, const char *topic, size_t topic_len, size_t total_len) {
    partial_message_t *message = find_partial_message(msg_id, topic, topic_len);
    if (message) {
        reset_partial_message(message);
    } else {
        for (size_t i = 0; i < MAX_PARTIAL_MESSAGES; i++) {
            if (!partial_messages[i].in_use) {
                message = &partial_messages[i];
                break;
            }
        }
    }

    if (!message) {
        ESP_LOGE(TAG, "No available slots for multipart MQTT message");
        return NULL;
    }

    message->buffer = malloc(total_len + 1);
    if (!message->buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for multipart MQTT message");
        return NULL;
    }

    if (topic && topic_len > 0) {
        message->topic = malloc(topic_len + 1);
        if (!message->topic) {
            ESP_LOGE(TAG, "Failed to allocate topic buffer for multipart MQTT message");
            free(message->buffer);
            message->buffer = NULL;
            return NULL;
        }
        memcpy(message->topic, topic, topic_len);
        message->topic[topic_len] = '\0';
        message->topic_len = topic_len;
    } else {
        message->topic = NULL;
        message->topic_len = 0;
    }

    message->total_len = total_len;
    message->received_len = 0;
    message->msg_id = msg_id;
    message->in_use = true;
    memset(message->buffer, 0, total_len + 1);

    return message;
}

static void complete_partial_message(partial_message_t *message) {
    if (!message) {
        return;
    }
    reset_partial_message(message);
}

static bool topic_matches(const char *topic, int topic_len, const char *expected) {
    if (!topic || topic_len <= 0 || !expected) {
        return false;
    }

    size_t expected_len = strlen(expected);
    if ((size_t)topic_len != expected_len) {
        return false;
    }

    return strncmp(topic, expected, expected_len) == 0;
}

static void stop_motors_for_reset(void) {
    // Ensure all PWM outputs are silenced before performing a restart/reset.
    machine_pwm_deinit_all();
}

static void process_system_input_message(esp_mqtt_client_handle_t client, const char *data, int data_len) {
    if (!data || data_len <= 0) {
        ESP_LOGW(TAG, "Empty MQTT payload on system input topic");
        return;
    }

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        return;
    }

    cJSON *command = cJSON_GetObjectItemCaseSensitive(json, "command");
    if (cJSON_IsString(command) && (command->valuestring != NULL)) {
        if (strcmp(command->valuestring, "ping") == 0) {
            char response[64];
            snprintf(response, sizeof(response), "{\"msg\":\"pong\"}");
            esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
            ESP_LOGI(TAG, "Responded to ping command");
        } else if (strcmp(command->valuestring, "py") == 0) {
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
        } else if (strcmp(command->valuestring, "ota-update") == 0) {
            if (ota_in_progress) {
                esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC,
                                       "{\"status\":\"error\",\"message\":\"OTA already in progress\"}", 0, 1, 0);
                ESP_LOGE(TAG, "OTA update already in progress");
            } else {
                cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "url");
                if (cJSON_IsString(url) && (url->valuestring != NULL)) {
                    char *url_copy = strdup(url->valuestring);
                    if (url_copy != NULL) {
                        BaseType_t result = xTaskCreate(
                            ota_task,
                            "ota_task",
                            16384,
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
        } else if (strcmp(command->valuestring, "restart") == 0 || strcmp(command->valuestring, "reset") == 0) {
            stop_motors_for_reset();
            esp_restart();
        } else if (strcmp(command->valuestring, "set-coeff") == 0) {
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
                    ESP_LOGI(TAG, "Set %s to %f", name, value);
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
                } else {
                    ESP_LOGE(TAG, "Invalid value type");
                    snprintf(response, sizeof(response), "{\"msg\":\"Invalid value type\"}");
                }

                free(name);
                free(type);
            } else {
                snprintf(response, sizeof(response), "{\"msg\":\"error\"}");
            }
            esp_mqtt_client_publish(client, MQTT_SYSTEM_OUTPUT_TOPIC, response, 0, 1, 0);
        } else if (strcmp(command->valuestring, "get-coeff") == 0) {
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

static void process_incoming_mqtt_message(esp_mqtt_client_handle_t client, const char *topic, int topic_len, const char *data, int data_len) {
    if (!topic || topic_len <= 0) {
        ESP_LOGW(TAG, "Received MQTT data without topic");
        return;
    }
    if (!data || data_len <= 0) {
        ESP_LOGW(TAG, "Received MQTT data without payload");
        return;
    }

    printf("TOPIC=%.*s\r\n", topic_len, topic);
    printf("DATA=%.*s\r\n", data_len, data);

    if (topic_matches(topic, topic_len, MQTT_SYSTEM_INPUT_TOPIC)) {
        process_system_input_message(client, data, data_len);
    }
}


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
    TickType_t now = xTaskGetTickCount();

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_recovery.boot_tick = now;
        s_recovery.last_wifi_disconnect_tick = now;
        s_recovery.last_ip_tick = now;
        s_recovery.last_mqtt_disconnect_tick = now;
        s_recovery.next_wifi_reconnect_tick = now;
        s_recovery.wifi_reconnect_attempt = 0;
        s_recovery.last_status_log_tick = now;
        s_recovery.last_disconnect_reason = -1;
        s_recovery.wifi_connected = false;
        s_recovery.mqtt_connected = false;
        s_recovery.recovery_guard = false;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        s_recovery.wifi_connected = false;
        s_recovery.mqtt_connected = false;
        s_recovery.last_wifi_disconnect_tick = now;
        s_recovery.last_mqtt_disconnect_tick = now;
        s_recovery.last_disconnect_reason = disconn ? disconn->reason : -1;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        uint32_t idx = s_recovery.wifi_reconnect_attempt;
        if (idx >= WIFI_RECONNECT_BACKOFF_STEPS) {
            idx = WIFI_RECONNECT_BACKOFF_STEPS - 1;
        }
        uint32_t backoff_ms = WIFI_RECONNECT_BACKOFF_MS[idx];
        if (backoff_ms > WIFI_RECONNECT_BACKOFF_MAX_MS) {
            backoff_ms = WIFI_RECONNECT_BACKOFF_MAX_MS;
        }
        s_recovery.next_wifi_reconnect_tick = now + backoff_to_ticks(backoff_ms);
        s_recovery.wifi_reconnect_attempt++;

        ESP_LOGW(TAG, "WiFi disconnected, reason=%d, reconnect in %u ms (attempt=%u)",
                 disconn ? disconn->reason : -1,
                 (unsigned)backoff_ms,
                 (unsigned)s_recovery.wifi_reconnect_attempt);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR " gateway:" IPSTR " mask:" IPSTR,
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.gw),
                 IP2STR(&event->ip_info.netmask));
        s_recovery.wifi_connected = true;
        s_recovery.last_ip_tick = now;
        s_recovery.last_wifi_disconnect_tick = now;
        s_recovery.wifi_reconnect_attempt = 0;
        s_recovery.next_wifi_reconnect_tick = now;
        s_recovery.recovery_guard = false;
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
        s_recovery.mqtt_connected = true;
        s_recovery.last_mqtt_disconnect_tick = xTaskGetTickCount();
        s_recovery.recovery_guard = false;

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
        s_recovery.mqtt_connected = false;
        s_recovery.last_mqtt_disconnect_tick = xTaskGetTickCount();
        reset_all_partial_messages();
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
        
    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");

        partial_message_t *partial = find_partial_message(event->msg_id, event->topic, event->topic_len);
        const char *topic_ptr = event->topic;
        int topic_len = event->topic_len;
        const char *data_ptr = event->data;
        int data_len = event->data_len;
        bool should_process = true;
        partial_message_t *partial_to_release = NULL;
        bool is_multipart = event->total_data_len > event->data_len;

        if (is_multipart) {
            should_process = false;
            if (!partial && event->current_data_offset == 0) {
                partial = start_partial_message(event->msg_id, event->topic, event->topic_len, event->total_data_len);
                if (!partial) {
                    ESP_LOGE(TAG, "Failed to start multipart MQTT message (msg_id=%d)", event->msg_id);
                    goto mqtt_event_data_end;
                }
                ESP_LOGI(TAG, "Receiving multipart MQTT message (msg_id=%d, total=%d)", event->msg_id, event->total_data_len);
            } else if (!partial) {
                ESP_LOGW(TAG, "Received MQTT multipart fragment without context (msg_id=%d, offset=%d)", event->msg_id, event->current_data_offset);
                goto mqtt_event_data_end;
            }

            if (!partial->topic && event->topic && event->topic_len > 0) {
                char *topic_copy = malloc(event->topic_len + 1);
                if (!topic_copy) {
                    ESP_LOGE(TAG, "Failed to copy topic for multipart MQTT message");
                } else {
                    memcpy(topic_copy, event->topic, event->topic_len);
                    topic_copy[event->topic_len] = '\0';
                    partial->topic = topic_copy;
                    partial->topic_len = event->topic_len;
                }
            }

            size_t offset = event->current_data_offset;
            size_t chunk_len = event->data_len;
            if (offset + chunk_len > partial->total_len) {
                ESP_LOGE(TAG, "MQTT multipart fragment exceeds buffer (offset=%d len=%d total=%zu)", event->current_data_offset, event->data_len, partial->total_len);
                complete_partial_message(partial);
                goto mqtt_event_data_end;
            }

            memcpy(partial->buffer + offset, event->data, chunk_len);
            size_t new_received = offset + chunk_len;
            if (new_received > partial->received_len) {
                partial->received_len = new_received;
            }

            if (partial->received_len >= partial->total_len) {
                partial->buffer[partial->total_len] = '\0';
                topic_ptr = partial->topic;
                topic_len = (int)partial->topic_len;
                data_ptr = partial->buffer;
                data_len = partial->total_len;
                partial_to_release = partial;
                should_process = true;
                ESP_LOGI(TAG, "Completed multipart MQTT message (msg_id=%d, total=%zu)", partial->msg_id, partial->total_len);
            } else {
                goto mqtt_event_data_end;
            }
        }

        if (should_process) {
            if (!topic_ptr && partial && partial->topic) {
                topic_ptr = partial->topic;
                topic_len = (int)partial->topic_len;
            }
            process_incoming_mqtt_message(client, topic_ptr, topic_len, data_ptr, data_len);
        }

    mqtt_event_data_end:
        if (partial_to_release) {
            complete_partial_message(partial_to_release);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        s_recovery.mqtt_connected = false;
        s_recovery.last_mqtt_disconnect_tick = xTaskGetTickCount();
        if (event->error_handle) {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR_TYPE=%d esp_tls_last_esp_err=0x%x tls_stack_err=0x%x sock_errno=%d connect_return_code=0x%x",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->esp_transport_sock_errno,
                     event->error_handle->connect_return_code);
        }
        log_broker_dns_resolution();
        reset_all_partial_messages();
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

    // Wait only briefly for initial connection; recovery logic continues in mqtt_task.
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(WIFI_INITIAL_CONNECT_WAIT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 wssid, wpass);
    } else {
        ESP_LOGW(TAG, "Initial WiFi connect timed out after %d ms; continuing with recovery loop", WIFI_INITIAL_CONNECT_WAIT_MS);
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
    get_string_setting("broker_uri", s_broker_uri, sizeof(s_broker_uri));
    get_string_setting("mqtt_username", s_mqtt_username, sizeof(s_mqtt_username));
    get_string_setting("mqtt_password", s_mqtt_password, sizeof(s_mqtt_password));
    get_string_setting("client_id", s_client_id, sizeof(s_client_id));
    mqtt_cfg.broker.address.uri = s_broker_uri;
    mqtt_cfg.credentials.username = s_mqtt_username;
    mqtt_cfg.credentials.authentication.password = s_mqtt_password;
    mqtt_cfg.credentials.client_id = s_client_id;
    
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

        TickType_t now = xTaskGetTickCount();
        log_recovery_timers(now);

        if (!s_recovery.wifi_connected && now >= s_recovery.next_wifi_reconnect_tick) {
            ESP_LOGI(TAG, "Recovery L1: WiFi reconnect attempt=%u", (unsigned)s_recovery.wifi_reconnect_attempt);
            esp_wifi_connect();
            uint32_t idx = s_recovery.wifi_reconnect_attempt;
            if (idx >= WIFI_RECONNECT_BACKOFF_STEPS) {
                idx = WIFI_RECONNECT_BACKOFF_STEPS - 1;
            }
            uint32_t backoff_ms = WIFI_RECONNECT_BACKOFF_MS[idx];
            if (backoff_ms > WIFI_RECONNECT_BACKOFF_MAX_MS) {
                backoff_ms = WIFI_RECONNECT_BACKOFF_MAX_MS;
            }
            s_recovery.next_wifi_reconnect_tick = now + backoff_to_ticks(backoff_ms);
            s_recovery.wifi_reconnect_attempt++;
        }

        if (!s_recovery.recovery_guard && !s_recovery.wifi_connected && elapsed_ms_since(s_recovery.last_ip_tick) >= WIFI_IP_RECOVERY_TIMEOUT_MS) {
            s_recovery.recovery_guard = true;
            s_recovery.last_recovery_action_tick = now;
            ESP_LOGW(TAG, "Recovery L2: WiFi reinit after %u ms without IP", (unsigned)elapsed_ms_since(s_recovery.last_ip_tick));
            esp_wifi_disconnect();
            esp_wifi_stop();
            esp_wifi_start();
            esp_wifi_connect();
            s_recovery.wifi_reconnect_attempt = 0;
            s_recovery.next_wifi_reconnect_tick = now;
        }

        if (!s_recovery.recovery_guard && s_recovery.wifi_connected && !s_recovery.mqtt_connected && elapsed_ms_since(s_recovery.last_mqtt_disconnect_tick) >= MQTT_RECOVERY_TIMEOUT_MS) {
            s_recovery.recovery_guard = true;
            s_recovery.last_recovery_action_tick = now;
            ESP_LOGW(TAG, "Recovery L3: MQTT client restart after %u ms disconnected", (unsigned)elapsed_ms_since(s_recovery.last_mqtt_disconnect_tick));
            restart_mqtt_client();
        }

        if (s_recovery.recovery_guard && elapsed_ms_since(s_recovery.last_recovery_action_tick) >= RECOVERY_GUARD_COOLDOWN_MS) {
            s_recovery.recovery_guard = false;
        }

#if ENABLE_OFFLINE_FALLBACK_RESTART
        if (!s_recovery.wifi_connected && !s_recovery.mqtt_connected && elapsed_ms_since(s_recovery.last_ip_tick) >= OFFLINE_RESTART_TIMEOUT_MS) {
            ESP_LOGE(TAG, "Recovery L4 fallback: restart after %u ms offline", (unsigned)elapsed_ms_since(s_recovery.last_ip_tick));
            stop_motors_for_reset();
            esp_restart();
        }
#endif
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
