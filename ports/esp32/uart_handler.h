#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

// UART configuration constants
#define UART_PORT_NUM      UART_NUM_0
#define UART_BAUD_RATE     115200
#define UART_RX_BUF_SIZE   1024
#define UART_TX_BUF_SIZE   1024
#define UART_RX_PIN        GPIO_NUM_3
#define UART_TX_PIN        GPIO_NUM_1
#define UART_EVENT_QUEUE_LEN 20

// Function declarations
esp_err_t uart_handler_init(void);
void process_uart_command(const char* command);
void uart_handler_task(void* pvParameter);

// External queue handle
extern QueueHandle_t uart_event_queue;

#endif // UART_HANDLER_H