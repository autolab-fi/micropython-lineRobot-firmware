#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "config_manager.h"
#include "my_mqtt_client.h"


static const char *TAG = "MQTT_CLIENT";

static esp_mqtt_client_handle_t mqtt_client;
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

// Объявление очереди для Python кода (должно быть определено в другом месте)
extern QueueHandle_t python_code_queue;

// Обработчик событий WiFi
// Обработчик событий WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
    printf("WIFI EVENT: base=%s, id=%ld\n", event_base, event_id);
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            printf("WiFi station started, connecting...\n");
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* disconn = (wifi_event_sta_disconnected_t*) event_data;
            printf("Disconnected. Reason: %d\n", disconn->reason);
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_ERROR_CHECK(esp_wifi_connect());
            printf("Retrying to connect to the AP\n");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Дополнительная проверка через 2 секунды
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            printf("Final IP Check: " IPSTR "\n", IP2STR(&ip_info.ip));
        } else {
            printf("Failed to get IP info!\n");
        }
    }
}

// Инициализация WiFi
static void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    // Инициализация сетевого интерфейса
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Создание STA интерфейса
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // Явная настройка DHCP
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif)); // Сначала останавливаем
    ESP_ERROR_CHECK(esp_netif_dhcpc_start(sta_netif)); // Затем перезапускаем

    // Альтернатива: статический IP (раскомментировать при необходимости)
    
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton("192.168.41.160");
    ip_info.gw.addr = esp_ip4addr_aton("192.168.41.114");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));
    

    // Конфигурация WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Регистрация обработчиков
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Настройка WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "POCO M4 Pro",
            .password = "123456789f",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Запуск WiFi с задержкой
    ESP_ERROR_CHECK(esp_wifi_start());
    vTaskDelay(pdMS_TO_TICKS(3000)); // Даем время на инициализацию
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// Обработка входящих MQTT сообщений
static void process_mqtt_message(const char* topic, const char* data, int data_len) {
    char* message = malloc(data_len + 1);
    if (message == NULL) {
        printf("Failed to allocate memory for message");
        return;
    }
    
    strncpy(message, data, data_len);
    message[data_len] = '\0';
    
    printf("Received message: %s", message);
    
    // Парсим JSON
    cJSON *json = cJSON_Parse(message);
    if (json == NULL) {
        printf("Invalid JSON format");
        free(message);
        return;
    }
    
    cJSON *command = cJSON_GetObjectItem(json, "command");
    if (!cJSON_IsString(command)) {
        printf("No command field found");
        cJSON_Delete(json);
        free(message);
        return;
    }
    
    // Обработка команды ping
    if (strcmp(command->valuestring, "ping") == 0) {
        // Отправляем ответ pong
        cJSON *response = cJSON_CreateObject();
        cJSON *msg = cJSON_CreateString("pong");
        cJSON_AddItemToObject(response, "msg", msg);
        
        char *response_string = cJSON_Print(response);
        esp_mqtt_client_publish(mqtt_client, "response", response_string, 0, 1, 0);
        
        printf("Sent pong response");
        
        cJSON_Delete(response);
        free(response_string);
    }
    // Обработка команды выполнения Python кода
    else if (strcmp(command->valuestring, "py") == 0) {
        cJSON *value = cJSON_GetObjectItem(json, "value");
        if (cJSON_IsString(value)) {
            // Создаем копию кода для передачи в очередь
            char *python_code = malloc(strlen(value->valuestring) + 1);
            if (python_code != NULL) {
                strcpy(python_code, value->valuestring);
                
                // Отправляем код в очередь для выполнения MicroPython
                if (python_code_queue != NULL && 
                    xQueueSend(python_code_queue, &python_code, pdMS_TO_TICKS(1000)) != pdTRUE) {
                    printf("Failed to send Python code to queue");
                    free(python_code);
                } else {
                    printf("Python code sent to execution queue");
                }
                
                // Отправляем подтверждение
                cJSON *response = cJSON_CreateObject();
                cJSON *msg = cJSON_CreateString("accepted");
                cJSON_AddItemToObject(response, "msg", msg);
                
                char *response_string = cJSON_Print(response);
                esp_mqtt_client_publish(mqtt_client, "response", response_string, 0, 1, 0);
                
                cJSON_Delete(response);
                free(response_string);
            } else {
                printf("Failed to allocate memory for Python code");
            }
        } else {
            printf("No value field found for py command");
        }
    } else {
        printf("Unknown command: %s", command->valuestring);
    }
    
    cJSON_Delete(json);
    free(message);
}

// Обработчик событий MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(client, "commands", 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT_EVENT_DISCONNECTED");
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            printf("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            printf("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            printf("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            printf("MQTT_EVENT_DATA");
            process_mqtt_message(event->topic, event->data, event->data_len);
            break;
            
        case MQTT_EVENT_ERROR:
            printf("MQTT_EVENT_ERROR");
            break;
            
        default:
            printf("Other event id:%d", (int)event_id);
            break;
    }
}

// Инициализация MQTT клиента
static void mqtt_client_init(void) {
    device_config_t config;
    esp_mqtt_client_config_t mqtt_cfg = {0};
    
    if (config_manager_load(&config) == ESP_OK) {
        mqtt_cfg.broker.address.uri = config.mqtt_broker;
        mqtt_cfg.credentials.username = config.mqtt_username;
        mqtt_cfg.credentials.authentication.password = config.mqtt_password;
    } else {
        // Значения по умолчанию
        mqtt_cfg.broker.address.uri = "mqtt://api.ondroid.org:1883";
        printf("Using default MQTT settings");
    }
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        printf("Failed to initialize MQTT client");
        return;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// Основная задача MQTT клиента
void mqtt_client_task(void *pvParameters) {
    printf("Starting MQTT client task on core %d", xPortGetCoreID());
    
    // Инициализируем WiFi
    wifi_init_sta();
    
    // Ждем подключения к WiFi
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    printf("Connected to WiFi");
    
    // Инициализируем MQTT клиент
    mqtt_client_init();
    
    // Основной цикл задачи
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}