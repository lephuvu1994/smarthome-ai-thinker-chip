#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_led.h"

static TimerHandle_t g_led_timer = NULL;
static int g_led_state = 0;

static void led_timer_cb(TimerHandle_t xTimer) {
    g_led_state = !g_led_state;
    bl_gpio_output_set(LED_PIN_1, g_led_state);
    bl_gpio_output_set(LED_PIN_2, g_led_state);
}

void led_init(void) {
    bl_gpio_enable_output(LED_PIN_1, 0, 0);
    bl_gpio_enable_output(LED_PIN_2, 0, 0);
    g_led_timer = xTimerCreate("LedTmr", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, led_timer_cb);
}

void set_led_mode(int period_ms) {
    if (period_ms == LED_ON_MODE) {
        if (xTimerIsTimerActive(g_led_timer)) xTimerStop(g_led_timer, 0);
        bl_gpio_output_set(LED_PIN_1, 0); // 0 sáng hay 1 sáng tùy mạch
        bl_gpio_output_set(LED_PIN_2, 0);
    } else {
        if (xTimerIsTimerActive(g_led_timer)) xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0);
        else {
            xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0);
            xTimerStart(g_led_timer, 0);
        }
    }
}
