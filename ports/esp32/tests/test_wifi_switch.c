#include "wifi_switch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void start(wifi_switch_t *state) {
    assert(wifi_switch_begin(state, "request-1", "Test WLAN", "test-only-password", 100));
    assert(wifi_switch_poll(state, 1099, true) == WIFI_SWITCH_NONE);
    assert(!wifi_switch_can_confirm(state, "request-1", 1099, true));
    assert(wifi_switch_poll(state, 1100, true) == WIFI_SWITCH_APPLY);
}

int main(void) {
    char max_ssid[34];
    memset(max_ssid, 'x', sizeof(max_ssid));
    max_ssid[32] = 0;
    char hex_psk[66];
    memset(hex_psk, 'a', sizeof(hex_psk));
    hex_psk[64] = 0;
    assert(wifi_credentials_valid(max_ssid, hex_psk));
    max_ssid[32] = 'x';
    max_ssid[33] = 0;
    assert(!wifi_credentials_valid(max_ssid, hex_psk));
    hex_psk[0] = 'z';
    assert(!wifi_credentials_valid("test", hex_psk));
    hex_psk[64] = 'a';
    hex_psk[65] = 0;
    assert(!wifi_credentials_valid("test", hex_psk));
    assert(!wifi_credentials_valid("", "password"));
    assert(!wifi_credentials_valid(NULL, "password"));
    assert(!wifi_credentials_valid("test", "short"));
    assert(!wifi_credentials_valid("test", "password\n"));
    assert(wifi_credentials_valid("WiFi-лаборатория", "space passphrase"));

    wifi_switch_t state = {0};
    start(&state);
    assert(!wifi_switch_begin(&state, "request-2", "other", "password", 1200));
    // A wrong password / absent AP / unavailable broker must all time out.
    assert(wifi_switch_poll(&state, 120099, false) == WIFI_SWITCH_NONE);
    assert(wifi_switch_poll(&state, 120100, false) == WIFI_SWITCH_ROLLBACK);
    wifi_switch_finish(&state, WIFI_SWITCH_ROLLED_BACK);
    assert(state.password[0] == 0);
    assert(!wifi_switch_begin(&state, "request-1", "test", "password", 130000));

    memset(&state, 0, sizeof(state));
    start(&state);
    assert(wifi_switch_poll(&state, 2000, true) == WIFI_SWITCH_NONE);
    assert(wifi_switch_poll(&state, 6999, true) == WIFI_SWITCH_NONE);
    assert(wifi_switch_poll(&state, 7000, true) == WIFI_SWITCH_NOTIFY_READY);
    assert(!wifi_switch_can_confirm(&state, "wrong-id", 7001, true));
    assert(!wifi_switch_can_confirm(&state, "request-1", 7001, false));
    assert(wifi_switch_can_confirm(&state, "request-1", 7001, true));
    // MQTT reconnect invalidates readiness and restarts the stability window.
    assert(wifi_switch_poll(&state, 7100, false) == WIFI_SWITCH_NONE);
    assert(!wifi_switch_can_confirm(&state, "request-1", 7101, true));
    assert(wifi_switch_poll(&state, 7200, true) == WIFI_SWITCH_NONE);
    assert(wifi_switch_poll(&state, 12200, true) == WIFI_SWITCH_NOTIFY_READY);
    // No confirmation still rolls back even with perfectly healthy connectivity.
    assert(!wifi_switch_can_confirm(&state, "request-1", 120100, true));
    assert(wifi_switch_poll(&state, 120100, true) == WIFI_SWITCH_ROLLBACK);

    memset(&state, 0, sizeof(state));
    start(&state);
    wifi_switch_poll(&state, 2000, true);
    wifi_switch_poll(&state, 7000, true);
    assert(wifi_switch_can_confirm(&state, "request-1", 8000, true));
    wifi_switch_finish(&state, WIFI_SWITCH_COMMITTED);
    assert(!wifi_switch_active(&state));
    assert(state.password[0] == 0);
    assert(wifi_switch_poll(&state, 200000, false) == WIFI_SWITCH_NONE);
    assert(!wifi_switch_begin(&state, "request-1", "test", "password", 210000));
    assert(wifi_switch_begin(&state, "request-2", "test", "password", 220000));
    // Cancel before applying must not count as a permanent update.
    wifi_switch_finish(&state, WIFI_SWITCH_ROLLED_BACK);
    assert(!wifi_switch_active(&state));
    assert(state.password[0] == 0);
    puts("WiFi switch state tests passed");
    return 0;
}
