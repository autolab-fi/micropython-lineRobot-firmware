#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

void status_led_task(void *pv_parameter);
void status_led_set_charging(bool charging);
void status_led_begin_user_code(void);
void status_led_end_user_code(void);
void status_led_suspend(void);

#endif // STATUS_LED_H
