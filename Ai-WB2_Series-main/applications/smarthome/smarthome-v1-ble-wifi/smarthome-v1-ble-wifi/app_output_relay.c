#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_output_relay.h"

// Hàm nội bộ: Kích xung
static void trigger_pulse(int pin) {
    printf("[RELAY] Pulse Pin %d\r\n", pin);
    bl_gpio_output_set(pin, 1);       // Bật
    vTaskDelay(pdMS_TO_TICKS(RELAY_PULSE_MS)); // Chờ 0.5s
    bl_gpio_output_set(pin, 0);       // Tắt
}

void app_relay_init(void) {
    // Cấu hình GPIO Output, mặc định mức 0
    bl_gpio_enable_output(GPIO_OUT_STOP, 0, 0);
//    bl_gpio_enable_output(GPIO_OUT_LOCK, 0, 0);
    bl_gpio_enable_output(GPIO_OUT_CLOSE, 0, 0);
    bl_gpio_enable_output(GPIO_OUT_OPEN, 0, 0);
//    bl_gpio_enable_output(GPIO_LED_STATUS, 0, 0);
    printf("[RELAY] Init Done\r\n");
}

void app_relay_pulse_open(void) { trigger_pulse(GPIO_OUT_OPEN); }
void app_relay_pulse_close(void) { trigger_pulse(GPIO_OUT_CLOSE); }
void app_relay_pulse_stop(void) { trigger_pulse(GPIO_OUT_STOP); }
//void app_relay_pulse_lock(void) { trigger_pulse(GPIO_OUT_LOCK); }
