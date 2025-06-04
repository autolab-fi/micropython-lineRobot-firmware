#include "uart_handler.h"
#include "settings_manager.h"
#include "micropython_task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static const char *TAG = "uart_handler";

QueueHandle_t uart_event_queue;

static const char* HELP_MSG = 
"Available commands:\n"
"  help - Show this help message\n"
"  ping - Test connection (responds with pong)\n"
"  reset - Reboot the device\n"
"  set <type> <name>=<value>; - Set a parameter\n"
"    Types: int, float, string\n"
"    Example: set string wifi_ssid=MyWiFi\n"
"    Example: set string wifi_pass=MyWiFiPassword\n"
"    Example: set float speed=1.5\n"
"  get <type> <name>; - Get a parameter value\n"
"    Types: int, float, string\n"
"    Example: get string wifi_ssid\n"
"  test-movement - Run movement test\n"
"  test-line-sensor - Run line sensor test\n"
"  test-color-sensor - Run color sensor test\n"
"  test-scan-i2c - Scan I2C bus\n"
"  battery-status - Get battery status\n";

esp_err_t uart_handler_init(void) {
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    esp_err_t ret = uart_driver_install(UART_PORT_NUM, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, 100, &uart_event_queue, ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        return ret;
    }
    
    ret = uart_param_config(UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters");
        return ret;
    }
    
    ret = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins");
        return ret;
    }
    
    ESP_LOGI(TAG, "UART handler initialized successfully");
    return ESP_OK;
}

void process_uart_command(const char* command) {
    ESP_LOGI(TAG, "Processing UART command: %s", command);
    
    // Check for help command
    if (strcmp(command, "help;") == 0) {
        printf("\n%s", HELP_MSG);
        return;
    }
    
    // Check for ping command
    if (strcmp(command, "ping;") == 0) {
        printf("pong\n");
        return;
    }
    
    // Check for reset command
    if (strcmp(command, "reset;") == 0) {
        esp_restart();
        return;
    }
    
    // Handle set commands (set type name=value;)
    if (strncmp(command, "set ", 4) == 0) {
        const char* set_cmd = command + 4;
        char* space = strchr(set_cmd, ' ');
        char* equals = strchr(set_cmd, '=');
        char* semicolon = strchr(set_cmd, ';');
        
        if (space && equals && semicolon && (space < equals) && (equals < semicolon)) {
            char type[16] = {0};
            char name[64] = {0};
            char value[128] = {0};
            
            // Extract type, name and value
            strncpy(type, set_cmd, space - set_cmd);
            strncpy(name, space + 1, equals - space - 1);
            strncpy(value, equals + 1, semicolon - equals - 1);
            
            if (strcmp(type, "int") == 0) {
                char* endptr;
                long ival = strtol(value, &endptr, 10);
                if (*endptr == '\0') {
                    set_setting(name, cJSON_CreateNumber(ival));
                    printf("Set %s (int) to %ld\n", name, ival);
                } else {
                    printf("Invalid integer value: %s\n", value);
                }
            } 
            else if (strcmp(type, "float") == 0) {
                char* endptr;
                float fval = strtof(value, &endptr);
                if (*endptr == '\0') {
                    set_setting(name, cJSON_CreateNumber(fval));
                    printf("Set %s (float) to %f\n", name, fval);
                } else {
                    printf("Invalid float value: %s\n", value);
                }
            }
            else if (strcmp(type, "string") == 0) {
                set_setting(name, cJSON_CreateString(value));
                printf("Set %s (string) to %s\n", name, value);
            }
            else {
                printf("Unknown type: %s\n", type);
            }
        } else {
            printf("Invalid set command format. Use: set <type> <name>=<value>;\n");
        }
        return;
    }
    
    // Handle get commands (get type name;)
    if (strncmp(command, "get ", 4) == 0) {
        const char* get_cmd = command + 4;
        char* space = strchr(get_cmd, ' ');
        char* semicolon = strchr(get_cmd, ';');
        
        if (space && semicolon && (space < semicolon)) {
            char type[16] = {0};
            char name[64] = {0};
            
            strncpy(type, get_cmd, space - get_cmd);
            strncpy(name, space + 1, semicolon - space - 1);
            
            if (strcmp(type, "int") == 0) {
                int ival = get_int_setting(name, INT_MIN);
                if (ival != INT_MIN) {
                    printf("%s (int): %d\n", name, ival);
                } else {
                    printf("Setting %s not found or not an int\n", name);
                }
            }
            else if (strcmp(type, "float") == 0) {
                float fval = get_float_setting(name, -999999.0f);
                if (fval != -999999.0f) {
                    printf("%s (float): %f\n", name, fval);
                } else {
                    printf("Setting %s not found or not a float\n", name);
                }
            }
            else if (strcmp(type, "string") == 0) {
                char sval[MAX_STR_LEN];
                if (get_string_setting(name, sval, sizeof(sval)) == ESP_OK) {
                    printf("%s (string): %s\n", name, sval);
                } else {
                    printf("Setting %s not found or not a string\n", name);
                }
            }
            else {
                printf("Unknown type: %s\n", type);
            }
        } else {
            printf("Invalid get command format. Use: get <type> <name>;\n");
        }
        return;
    }
    
    // Handle test commands
    if (strcmp(command, "test-movement;") == 0) {
        execute_python_code("from test_robot_lib import test\ntest()");
    } 
    else if (strcmp(command, "test-line-sensor;") == 0) {
        execute_python_code("from test_octoliner import test\ntest()");
    }
    else if (strcmp(command, "test-color-sensor;") == 0) {
        execute_python_code("from test_tcs import test\ntest()");
    }
    else if (strcmp(command, "test-scan-i2c;") == 0) {
        execute_python_code("from scan import scan\nscan()");
    }
    else if (strcmp(command, "battery-status;") == 0) {
        set_measure_adc_flag(true);
    }
    else if (strcmp(command, "print-settings;") == 0) {
        print_all_settings();
    }
    else {
        printf("Unknown command. Type 'help;' for available commands.\n");
    }
}

void uart_handler_task(void* pvParameter) {
    ESP_LOGI(TAG, "Starting UART handler task");
    
    char cmd_buffer[256];
    size_t cmd_length = 0;
    
    while (1) {
        uint8_t data[256];
        int length = uart_read_bytes(UART_PORT_NUM, data, sizeof(data), pdMS_TO_TICKS(10));
        
        if (length > 0) {
            // Process received data
            for (int i = 0; i < length; i++) {
                // Check for command terminator (newline or semicolon)
                if (data[i] == '\n' || data[i] == ';') {
                    // Null-terminate the command
                    if (cmd_length < sizeof(cmd_buffer)) {
                        cmd_buffer[cmd_length++] = ';';
                        cmd_buffer[cmd_length] = '\0';
                        
                        // Process the complete command
                        process_uart_command(cmd_buffer);
                        
                        // Reset command buffer
                        cmd_length = 0;
                    } else {
                        ESP_LOGE(TAG, "Command buffer overflow");
                        cmd_length = 0;
                    }
                } else {
                    // Add character to command buffer if there's space
                    if (cmd_length < sizeof(cmd_buffer) - 1) {
                        cmd_buffer[cmd_length++] = data[i];
                    } else {
                        ESP_LOGE(TAG, "Command too long, discarding");
                        cmd_length = 0;
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}