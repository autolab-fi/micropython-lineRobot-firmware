#ifndef WIFI_SWITCH_H
#define WIFI_SWITCH_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_SWITCH_TIMEOUT_MS 120000
#define WIFI_SWITCH_STABLE_MS 5000

typedef enum {
    WIFI_SWITCH_IDLE, WIFI_SWITCH_PENDING, WIFI_SWITCH_TRIAL,
    WIFI_SWITCH_READY, WIFI_SWITCH_COMMITTED, WIFI_SWITCH_ROLLED_BACK,
    WIFI_SWITCH_FAILED,
} wifi_switch_phase_t;

typedef enum {
    WIFI_SWITCH_NONE, WIFI_SWITCH_APPLY, WIFI_SWITCH_NOTIFY_READY,
    WIFI_SWITCH_ROLLBACK,
} wifi_switch_action_t;

typedef struct {
    wifi_switch_phase_t phase;
    char request_id[49];
    char ssid[33];
    char password[65];
    uint64_t started_ms;
    uint64_t stable_since_ms;
} wifi_switch_t;

bool wifi_credentials_valid(const char *ssid, const char *password);
bool wifi_switch_active(const wifi_switch_t *state);
bool wifi_switch_begin(wifi_switch_t *state, const char *id, const char *ssid,
    const char *password, uint64_t now);
wifi_switch_action_t wifi_switch_poll(wifi_switch_t *state, uint64_t now, bool connected);
bool wifi_switch_can_confirm(const wifi_switch_t *state, const char *id,
    uint64_t now, bool connected);
void wifi_switch_finish(wifi_switch_t *state, wifi_switch_phase_t phase);
const char *wifi_switch_phase_name(wifi_switch_phase_t phase);

#endif
