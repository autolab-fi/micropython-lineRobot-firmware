/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "py/runtime.h"
#include "py/mphal.h"
#include "uart.h"

#if MICROPY_HW_ENABLE_UART_REPL

#include <stdio.h>
#include "driver/uart.h" // For uart_get_sclk_freq()
#include "hal/uart_hal.h"
#include "soc/uart_periph.h"
#include "freertos/stream_buffer.h"
extern StreamBufferHandle_t mqtt_print_stream; // Объявлен в C-коде ядра

static void uart_irq_handler(void *arg);

// Declaring the HAL structure on the stack saves a tiny amount of static RAM
#define REPL_HAL_DEFN() { .dev = UART_LL_GET_HW(MICROPY_HW_UART_REPL) }

// RXFIFO Full interrupt threshold. Set the same as the ESP-IDF UART driver
#define RXFIFO_FULL_THR (SOC_UART_FIFO_LEN - 8)

// RXFIFO RX timeout threshold. This is in bit periods, so 10==one byte. Same as ESP-IDF UART driver.
#define RXFIFO_RX_TIMEOUT (10)

void uart_stdout_init(void) {
    uart_hal_context_t repl_hal = REPL_HAL_DEFN();
    soc_module_clk_t sclk;
    uint32_t sclk_freq;

    uart_hal_get_sclk(&repl_hal, &sclk); // To restore SCLK after uart_hal_init() resets it
    ESP_ERROR_CHECK(uart_get_sclk_freq(sclk, &sclk_freq));

    uart_hal_init(&repl_hal, MICROPY_HW_UART_REPL); // Sets defaults: 8n1, no flow control
    uart_hal_set_sclk(&repl_hal, sclk);
    uart_hal_set_baudrate(&repl_hal, MICROPY_HW_UART_REPL_BAUD, sclk_freq);
    uart_hal_rxfifo_rst(&repl_hal);
    uart_hal_txfifo_rst(&repl_hal);

    ESP_ERROR_CHECK(
        esp_intr_alloc(uart_periph_signal[MICROPY_HW_UART_REPL].irq,
            ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM,
            uart_irq_handler,
            NULL,
            NULL)
        );

    // Enable RX interrupts
    uart_hal_set_rxfifo_full_thr(&repl_hal, RXFIFO_FULL_THR);
    uart_hal_set_rx_timeout(&repl_hal, RXFIFO_RX_TIMEOUT);
    uart_hal_ena_intr_mask(&repl_hal, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
}

int uart_stdout_tx_strn(const char *str, size_t len) {
    uart_hal_context_t repl_hal = REPL_HAL_DEFN();
    size_t remaining = len;
    uint32_t written = 0;

    // filter empty messages
    if (len == 0) return 0;

    // ---------- MQTT accumulation logic ----------
    // Буфер аккумуляции для формирования полного "сообщения" до '\n' или '\r'
    #define MQTT_ACCUM_MAX 1024
    static char mqtt_accum[MQTT_ACCUM_MAX];
    static size_t mqtt_accum_len = 0;

    if (mqtt_print_stream != NULL) {
        // Если входящая порция слишком большая для свободного места в аккумуляторе,
        // сначала сливаем аккумулятор в stream, чтобы освободить место.
        size_t free_space = (mqtt_accum_len < MQTT_ACCUM_MAX) ? (MQTT_ACCUM_MAX - mqtt_accum_len) : 0;
        const char *p = str;
        size_t to_copy = len;

        if (to_copy > free_space) {
            // flush existing accumulated data first
            if (mqtt_accum_len > 0) {
                xStreamBufferSend(mqtt_print_stream, mqtt_accum, mqtt_accum_len, 0);
                mqtt_accum_len = 0;
            }
            // если incoming chunk всё ещё больше буфера — отправляем его непосредственно кусками
            while (to_copy > MQTT_ACCUM_MAX) {
                xStreamBufferSend(mqtt_print_stream, p, MQTT_ACCUM_MAX, 0);
                p += MQTT_ACCUM_MAX;
                to_copy -= MQTT_ACCUM_MAX;
            }
            // теперь to_copy <= MQTT_ACCUM_MAX -> копируем в аккумулятор
            if (to_copy > 0) {
                memcpy(mqtt_accum + mqtt_accum_len, p, to_copy);
                mqtt_accum_len += to_copy;
            }
        } else {
            // вмещается целиком
            memcpy(mqtt_accum + mqtt_accum_len, p, to_copy);
            mqtt_accum_len += to_copy;
        }

        // Проверяем аккумулятор на наличие '\n' или '\r'.
        // Для каждой найденной терминалки отправляем часть до неё (терминалку НЕ включаем).
        size_t start = 0;
        for (size_t i = 0; i < mqtt_accum_len; i++) {
            if (mqtt_accum[i] == '\n' || mqtt_accum[i] == '\r') {
                size_t msglen = i - start; // не включая \n/\r
                if (msglen > 0) {
                    xStreamBufferSend(mqtt_print_stream, mqtt_accum + start, msglen, 0);
                }
                // если msglen == 0 -> это пустая строка (только перевод строки) -> не отправляем
                start = i + 1; // пропускаем терминальный символ
            }
        }
        // Сдвигаем остаток в начало буфера
        if (start > 0) {
            size_t rem = mqtt_accum_len - start;
            if (rem > 0) {
                memmove(mqtt_accum, mqtt_accum + start, rem);
            }
            mqtt_accum_len = rem;
        }
    }
    // ---------- конец MQTT logic ----------

// UART_ONLY:
    // send to UART (исходная логика)
    for (;;) {
        uart_hal_write_txfifo(&repl_hal, (const void *)str, remaining, &written);
        if (written >= remaining) break;
        remaining -= written;
        str += written;
        ulTaskNotifyTake(pdFALSE, 1);
    }
    return len;
}

// int uart_stdout_tx_strn(const char *str, size_t len) {
//     uart_hal_context_t repl_hal = REPL_HAL_DEFN();
//     size_t remaining = len;
//     uint32_t written = 0;
//     static bool skip_next = false;

//     // filter empty messages
//     if (len == 0) return 0;

//     //  if next message starts with \n or \r, skip it
//     if (len == 1 && (*str == '\n' || *str == '\r')) {
//         if (skip_next) {
//             skip_next = false;
//             goto UART_ONLY; // Пропускаем для MQTT
//         }
//     }

//     // send to MQTT
//     if (mqtt_print_stream != NULL) {
//         // check if last char is \n or \r
//         bool ends_with_newline = (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r'));
        
//         xStreamBufferSend(mqtt_print_stream, str, len, 0);
//         skip_next = ends_with_newline; // set skip_next if last char is \n or \r
//     }

// UART_ONLY:
//     // send to UART
//     for (;;) {
//         uart_hal_write_txfifo(&repl_hal, (const void *)str, remaining, &written);
//         if (written >= remaining) break;
//         remaining -= written;
//         str += written;
//         ulTaskNotifyTake(pdFALSE, 1);
//     }
//     return len;
// }

// all code executed in ISR must be in IRAM, and any const data must be in DRAM
static void IRAM_ATTR uart_irq_handler(void *arg) {
    uint8_t rbuf[SOC_UART_FIFO_LEN];
    int len;
    uart_hal_context_t repl_hal = REPL_HAL_DEFN();

    uart_hal_clr_intsts_mask(&repl_hal, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_FRAM_ERR);

    len = uart_hal_get_rxfifo_len(&repl_hal);

    uart_hal_read_rxfifo(&repl_hal, rbuf, &len);

    for (int i = 0; i < len; i++) {
        if (rbuf[i] == mp_interrupt_char) {
            mp_sched_keyboard_interrupt();
        } else {
            // this is an inline function so will be in IRAM
            ringbuf_put(&stdin_ringbuf, rbuf[i]);
        }
    }
}

#endif // MICROPY_HW_ENABLE_UART_REPL
