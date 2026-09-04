#include "status_led.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STATUS_LED_GPIO GPIO_NUM_15
#define STATUS_LED_TIMER LEDC_TIMER_3
#define STATUS_LED_CHANNEL LEDC_CHANNEL_7
#define STATUS_LED_MODE LEDC_LOW_SPEED_MODE
#define STATUS_LED_DUTY_RES LEDC_TIMER_10_BIT
#define STATUS_LED_DUTY_MAX 1023
#define STATUS_LED_MIN_PERCENT 3
#define STATUS_LED_MAX_PERCENT 65
#define STATUS_LED_STEP_MS 30
#define STATUS_LED_HALF_CYCLE_STEPS 50
#define STATUS_LED_TELEMETRY_TIMEOUT_MS 90000

static volatile bool s_charging = false;
static volatile bool s_suspended = false;
static volatile TickType_t s_last_telemetry_tick = 0;
static bool s_pwm_configured = false;

static void status_led_release(void) {
    if (s_pwm_configured) {
        ledc_stop(STATUS_LED_MODE, STATUS_LED_CHANNEL, 0);
        s_pwm_configured = false;
        gpio_reset_pin(STATUS_LED_GPIO);
    }
}

static bool status_led_claim(void) {
    if (s_pwm_configured) {
        return true;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = STATUS_LED_MODE,
        .duty_resolution = STATUS_LED_DUTY_RES,
        .timer_num = STATUS_LED_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        return false;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = STATUS_LED_GPIO,
        .speed_mode = STATUS_LED_MODE,
        .channel = STATUS_LED_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = STATUS_LED_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        gpio_reset_pin(STATUS_LED_GPIO);
        return false;
    }

    s_pwm_configured = true;
    return true;
}

static uint32_t status_led_breathing_duty(uint32_t step) {
    uint32_t level = step <= STATUS_LED_HALF_CYCLE_STEPS
        ? step
        : 2 * STATUS_LED_HALF_CYCLE_STEPS - step;
    uint32_t level_squared = level * level;
    uint32_t range = STATUS_LED_MAX_PERCENT - STATUS_LED_MIN_PERCENT;
    uint32_t percent = STATUS_LED_MIN_PERCENT
        + range * level_squared
            / (STATUS_LED_HALF_CYCLE_STEPS * STATUS_LED_HALF_CYCLE_STEPS);
    return STATUS_LED_DUTY_MAX * percent / 100;
}

static void status_led_wait_until_released(void) {
    for (int attempt = 0; attempt < 100 && s_pwm_configured; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void status_led_set_charging(bool charging) {
    s_charging = charging;
    s_last_telemetry_tick = xTaskGetTickCount();
}

void status_led_begin_user_code(void) {
    s_suspended = true;
    s_charging = false;
    status_led_wait_until_released();
}

void status_led_end_user_code(void) {
    // A fresh battery-status response is required before the indicator can
    // reclaim GPIO15 after MicroPython has released the student's pins.
    s_charging = false;
    s_suspended = false;
}

void status_led_suspend(void) {
    s_suspended = true;
    s_charging = false;
    status_led_wait_until_released();
}

void status_led_task(void *pv_parameter) {
    (void)pv_parameter;
    uint32_t step = 0;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool telemetry_fresh = s_last_telemetry_tick != 0
            && (now - s_last_telemetry_tick)
                <= pdMS_TO_TICKS(STATUS_LED_TELEMETRY_TIMEOUT_MS);

        if (!s_suspended && s_charging && telemetry_fresh && status_led_claim()) {
            uint32_t duty = status_led_breathing_duty(step);
            ledc_set_duty(STATUS_LED_MODE, STATUS_LED_CHANNEL, duty);
            ledc_update_duty(STATUS_LED_MODE, STATUS_LED_CHANNEL);
            step = (step + 1) % (2 * STATUS_LED_HALF_CYCLE_STEPS);
        } else {
            step = 0;
            status_led_release();
        }

        vTaskDelay(pdMS_TO_TICKS(STATUS_LED_STEP_MS));
    }
}
