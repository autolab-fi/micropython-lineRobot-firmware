#ifndef MICROPYTHON_TASK_H
#define MICROPYTHON_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"

// Task handle for MicroPython main task
extern TaskHandle_t mp_main_task_handle;

// Global queues and streams
extern QueueHandle_t python_code_queue;
extern StreamBufferHandle_t mqtt_print_stream;

// Native code management structure
typedef struct _native_code_node_t {
    struct _native_code_node_t *next;
    uint32_t data[];
} native_code_node_t;

// ADC measurement flag functions
bool get_measure_adc_flag(void);
void set_measure_adc_flag(bool value);
void clear_measure_adc_flag(void);

// MicroPython task function
void mp_task(void *pvParameter);

// Native code management functions
void *esp_native_code_commit(void *buf, size_t len, void *reloc);
void esp_native_code_free_all(void);
void  execute_python_code(const char* code);


// NLR jump failure handler
void nlr_jump_fail(void *val);

#endif // MICROPYTHON_TASK_H