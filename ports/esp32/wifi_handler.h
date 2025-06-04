#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Maximum retry attempts
#define WIFI_MAXIMUM_RETRY  5

// Function declarations
esp_err_t wifi_init_sta(void);
bool is_wifi_connected(void);

#endif // WIFI_HANDLER_H