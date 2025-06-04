#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_task_wdt.h"

#include "py/cstack.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/persistentcode.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "shared/timeutils/timeutils.h"
#include "shared/tinyusb/mp_usbd.h"
#include "mbedtls/platform_time.h"

#include "uart.h"
#include "usb.h"
#include "usb_serial_jtag.h"
#include "modmachine.h"
#include "modnetwork.h"

// Include our custom modules
#include "settings_manager.h"
#include "mqtt_handler.h"
#include "uart_handler.h"
#include "micropython_task.h"
//#include "wifi_handler.h"
// #include "watchdog_handler.h"

#if MICROPY_BLUETOOTH_NIMBLE
#include "extmod/modbluetooth.h"
#endif

#if MICROPY_PY_ESPNOW
#include "modespnow.h"
#endif

// Task priorities
#define MP_TASK_PRIORITY        (ESP_TASK_PRIO_MIN + 1)
#define MQTT_TASK_PRIORITY      (ESP_TASK_PRIO_MIN + 2)
#define WATCHDOG_TASK_PRIORITY  (ESP_TASK_PRIO_MIN + 3)

static const char *TAG = "main";

// Global variables
// QueueHandle_t python_code_queue;
// StreamBufferHandle_t mqtt_print_stream = NULL;

int vprintf_null(const char *format, va_list ap) {
    // Do nothing: this is used as a log target during raw repl mode
    return 0;
}

time_t platform_mbedtls_time(time_t *timer) {
    // mbedtls_time requires time in seconds from EPOCH 1970
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + TIMEUTILS_SECONDS_1970_TO_2000;
}

void boardctrl_startup(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void MICROPY_ESP_IDF_ENTRY(void) {
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // Initialize settings first
    if (settings_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize settings");
        esp_restart();
    }
    
    

    // Set log levels
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("micropython_mqtt", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    print_all_settings();

    // Hook for a board to run code at start up.
    MICROPY_BOARD_STARTUP();
    
    // Create stream buffer for MQTT print communication
    mqtt_print_stream = xStreamBufferCreate(1024, 1);
    if (mqtt_print_stream == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT print stream");
        esp_restart();
    }
    
    // Create queue for Python code communication between tasks
    python_code_queue = xQueueCreate(10, sizeof(char*));
    if (python_code_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create Python code queue");
        esp_restart();
    }

    // Initialize watchdog
   // watchdog_init();
   uart_handler_init();

    // Create MicroPython task on core 0
    xTaskCreatePinnedToCore(mp_task, "mp_task", 
        MICROPY_TASK_STACK_SIZE / sizeof(StackType_t), 
        NULL, MP_TASK_PRIORITY, &mp_main_task_handle, 0);
    
    // Create MQTT task on core 1
    xTaskCreatePinnedToCore(mqtt_task, "mqtt_task", 
        8192, NULL, MQTT_TASK_PRIORITY, NULL, 1);
    
    xTaskCreatePinnedToCore(uart_handler_task, "uart_task", 
        4096, NULL, WATCHDOG_TASK_PRIORITY, NULL, 1);
    
    // Create watchdog task
    // xTaskCreatePinnedToCore(watchdog_task, "watchdog_task", 
    //     4096, NULL, WATCHDOG_TASK_PRIORITY, NULL, 1);

    // Write settings to MicroPython file system
    write_settings_to_micropython();
}