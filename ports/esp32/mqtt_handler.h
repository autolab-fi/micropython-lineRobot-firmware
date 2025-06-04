#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
//#include "esp_mqtt_types.h"

// External variables
extern StreamBufferHandle_t mqtt_print_stream;
extern QueueHandle_t python_code_queue;

// Function declarations
void mqtt_task(void *pvParameter);
char* escape_json_string(const char* input);

#endif // MQTT_HANDLER_H