#include "micropython_task.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_cpu.h"
#include "mbedtls/platform_time.h"

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

#include "uart.h"
#include "usb.h"
#include "usb_serial_jtag.h"
#include "modmachine.h"
#include "modnetwork.h"
#include "settings_manager.h"
#include "mqtt_handler.h"

#if MICROPY_BLUETOOTH_NIMBLE
#include "extmod/modbluetooth.h"
#endif

#if MICROPY_PY_ESPNOW
#include "modespnow.h"
#endif

// Constants
#define MAX_STR_LEN 64

static const char *TAG = "micropython_task";

// Global variables
//TaskHandle_t mp_main_task_handle = NULL;
QueueHandle_t python_code_queue = NULL;
StreamBufferHandle_t mqtt_print_stream = NULL;

// Static variables for native code management
static native_code_node_t *native_code_head = NULL;

// ADC measurement flag
static bool measure_adc = false;

// Python code storage
static char* py_code = "";

// Python string argument structure
typedef struct {
    const char *code;   // указатель на строку Python-кода
} py_string_arg_t;

// ADC flag functions
bool get_measure_adc_flag(void) {
    return measure_adc;
}

void set_measure_adc_flag(bool value) {
    measure_adc = value;
}

void clear_measure_adc_flag(void) {
    measure_adc = false;
}


void  execute_python_code(const char* code){
    if (code != NULL) {
        if (xQueueSend(python_code_queue, &code, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send Python code to queue");
            free(code);
        } else {
            ESP_LOGI(TAG, "Python code sent to execution queue: %s", code);
        }
    }
}

// MicroPython task running on core 0
void mp_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting MicroPython task on core %d", xPortGetCoreID());
    
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    #if MICROPY_PY_THREAD
    mp_thread_init(pxTaskGetStackStart(NULL), MICROPY_TASK_STACK_SIZE / sizeof(uintptr_t));
    #endif
    #if MICROPY_HW_ESP_USB_SERIAL_JTAG
    usb_serial_jtag_init();
    #elif MICROPY_HW_ENABLE_USBDEV
    usb_init();
    #endif
    #if MICROPY_HW_ENABLE_UART_REPL
    uart_stdout_init();
    #endif
    machine_init();

    // Configure time function, for mbedtls certificate time validation.
    time_t platform_mbedtls_time(time_t *timer) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec + TIMEUTILS_SECONDS_1970_TO_2000;
    }
    mbedtls_platform_set_time(platform_mbedtls_time);

    void *mp_task_heap = MP_PLAT_ALLOC_HEAP(MICROPY_GC_INITIAL_HEAP_SIZE);
    if (mp_task_heap == NULL) {
        printf("mp_task_heap allocation failed!\n");
        esp_restart();
    }

soft_reset:
    // initialise the stack pointer for the main thread
    mp_cstack_init_with_top((void *)sp, MICROPY_TASK_STACK_SIZE);
    gc_init(mp_task_heap, mp_task_heap + MICROPY_GC_INITIAL_HEAP_SIZE);
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    readline_init0();

    // initialise peripherals
    machine_pins_init();
    #if MICROPY_PY_MACHINE_I2S
    machine_i2s_init0();
    #endif

    // run boot-up scripts
    pyexec_frozen_module("_boot.py", false);
    int ret = pyexec_file_if_exists("boot.py");
    if (ret & PYEXEC_FORCED_EXIT) {
        goto soft_reset_exit;
    }

    char* received_code = NULL;

    for (;;) {
        // Check ADC measurement flag
        if (measure_adc) {
            pyexec_file_if_exists("battery_status.py");
            measure_adc = false;
        }
        
        // Check for new Python code to execute
        if (xQueueReceive(python_code_queue, &received_code, pdMS_TO_TICKS(10)) == pdTRUE) {
            py_code = received_code;
        }
        
        // Execute Python code if available
        if (strlen(py_code) > 0) {
            printf("Executing Python code: %s\n", py_code);
            py_string_arg_t codic = {py_code};
            py_string_arg_t *arg = &codic;
            
            mp_lexer_t *lex = mp_lexer_new_from_str_len(
                MP_QSTR__lt_string_gt_, arg->code, strlen(arg->code), false
            );
            mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        
            // Compile
            mp_obj_t module_fun = mp_compile(&parse_tree,
                                             MP_QSTR__lt_string_gt_,
                                             false);
        
            nlr_buf_t nlr;
            if (nlr_push(&nlr) == 0) {
                mp_call_function_0(module_fun);
                nlr_pop();
                
                // Send execution success status via MQTT
                //publish_system_message("{\"msg\":\"Python code executed successfully\"}");
            } else {
                printf("Python exception:\n");
                mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
                
                // Send execution error status via MQTT
               // publish_system_message("{\"msg\":\"Python code execution failed\"}");
            }

            if (received_code != NULL) {
                free(received_code);
                received_code = NULL;
            }
            py_code = "";
            goto soft_reset;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

soft_reset_exit:

    #if MICROPY_BLUETOOTH_NIMBLE
    mp_bluetooth_deinit();
    #endif

    #if MICROPY_PY_ESPNOW
    espnow_deinit(mp_const_none);
    MP_STATE_PORT(espnow_singleton) = NULL;
    #endif

    machine_timer_deinit_all();

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    #endif

    #if MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE
    mp_usbd_deinit();
    #endif

    gc_sweep_all();

    // Free any native code pointers that point to iRAM.
    esp_native_code_free_all();

    mp_hal_stdout_tx_str("MPY: soft reboot\r\n");

    // deinitialise peripherals
    machine_pwm_deinit_all();
    // TODO: machine_rmt_deinit_all();
    machine_pins_deinit();
    machine_deinit();
    #if MICROPY_PY_SOCKET_EVENTS
    socket_events_deinit();
    #endif

    mp_deinit();
    fflush(stdout);
    goto soft_reset;
}

// NLR jump failure handler
MP_WEAK void nlr_jump_fail(void *val) {
    printf("NLR jump failed, val=%p\n", val);
    //esp_restart();
}

// Native code management functions
void esp_native_code_free_all(void) {
    while (native_code_head != NULL) {
        native_code_node_t *next = native_code_head->next;
        heap_caps_free(native_code_head);
        native_code_head = next;
    }
}

void *esp_native_code_commit(void *buf, size_t len, void *reloc) {
    len = (len + 3) & ~3;
    size_t len_node = sizeof(native_code_node_t) + len;
    native_code_node_t *node = heap_caps_malloc(len_node, MALLOC_CAP_EXEC);
    #if CONFIG_IDF_TARGET_ESP32S2
    // Workaround for ESP-IDF bug https://github.com/espressif/esp-idf/issues/14835
    if (node != NULL && !esp_ptr_executable(node)) {
        free(node);
        node = NULL;
    }
    #endif // CONFIG_IDF_TARGET_ESP32S2
    if (node == NULL) {
        m_malloc_fail(len_node);
    }
    node->next = native_code_head;
    native_code_head = node;
    void *p = node->data;
    if (reloc) {
        mp_native_relocate(reloc, buf, (uintptr_t)p);
    }
    memcpy(p, buf, len);
    return p;
}