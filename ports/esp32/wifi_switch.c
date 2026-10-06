#include "wifi_switch.h"
#include <string.h>

bool wifi_credentials_valid(const char *ssid, const char *password) {
    if (!ssid || !password || strlen(ssid) == 0 || strlen(ssid) > 32) {
        return false;
    }
    size_t length = strlen(password);
    if (length < 8 || length > 64) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)password[i];
        if (length == 64) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        } else if (c < 32 || c > 126) {
            return false;
        }
    }
    return true;
}

bool wifi_switch_active(const wifi_switch_t *state) {
    return state->phase == WIFI_SWITCH_PENDING || state->phase == WIFI_SWITCH_TRIAL
        || state->phase == WIFI_SWITCH_READY;
}

bool wifi_switch_begin(wifi_switch_t *state, const char *id, const char *ssid,
    const char *password, uint64_t now) {
    if (wifi_switch_active(state) || !id || !*id || strlen(id) >= sizeof(state->request_id)
        || !wifi_credentials_valid(ssid, password)) {
        return false;
    }
    // A duplicate QoS 1 delivery must never start a second switch.
    if (strcmp(state->request_id, id) == 0) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    strcpy(state->request_id, id);
    strcpy(state->ssid, ssid);
    strcpy(state->password, password);
    state->started_ms = now;
    state->phase = WIFI_SWITCH_PENDING;
    return true;
}

wifi_switch_action_t wifi_switch_poll(wifi_switch_t *state, uint64_t now, bool connected) {
    if (!wifi_switch_active(state)) {
        return WIFI_SWITCH_NONE;
    }
    if (now - state->started_ms >= WIFI_SWITCH_TIMEOUT_MS) {
        return WIFI_SWITCH_ROLLBACK;
    }
    if (state->phase == WIFI_SWITCH_PENDING) {
        if (now - state->started_ms >= 1000) {
            state->phase = WIFI_SWITCH_TRIAL;
            return WIFI_SWITCH_APPLY;
        }
        return WIFI_SWITCH_NONE;
    }
    if (!connected) {
        state->stable_since_ms = 0;
        state->phase = WIFI_SWITCH_TRIAL;
    } else if (!state->stable_since_ms) {
        state->stable_since_ms = now;
    } else if (state->phase == WIFI_SWITCH_TRIAL
        && now - state->stable_since_ms >= WIFI_SWITCH_STABLE_MS) {
        state->phase = WIFI_SWITCH_READY;
        return WIFI_SWITCH_NOTIFY_READY;
    }
    return WIFI_SWITCH_NONE;
}

bool wifi_switch_can_confirm(const wifi_switch_t *state, const char *id,
    uint64_t now, bool connected) {
    return id && strcmp(state->request_id, id) == 0 && state->phase == WIFI_SWITCH_READY
        && connected && now - state->started_ms < WIFI_SWITCH_TIMEOUT_MS;
}

void wifi_switch_finish(wifi_switch_t *state, wifi_switch_phase_t phase) {
    state->phase = phase;
    memset(state->password, 0, sizeof(state->password));
}

const char *wifi_switch_phase_name(wifi_switch_phase_t phase) {
    switch (phase) {
        case WIFI_SWITCH_IDLE: return "idle";
        case WIFI_SWITCH_PENDING: return "pending";
        case WIFI_SWITCH_TRIAL: return "trial";
        case WIFI_SWITCH_READY: return "ready";
        case WIFI_SWITCH_COMMITTED: return "committed";
        case WIFI_SWITCH_ROLLED_BACK: return "rolled_back";
        default: return "failed";
    }
}
