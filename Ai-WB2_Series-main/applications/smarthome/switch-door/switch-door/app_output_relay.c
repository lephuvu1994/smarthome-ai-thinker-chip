#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_output_relay.h"

void app_relay_init(void) {
    // Output mức 0 (Tắt) khi khởi động
    bl_gpio_enable_output(GPIO_OUT_CLOSE, 0, 0);
    bl_gpio_enable_output(GPIO_OUT_OPEN, 0, 0);
    printf("[RELAY] Init Done (Hold Mode)\r\n");
}

// Hàm tắt toàn bộ (Safety)
void app_relay_stop_all(void) {
    bl_gpio_output_set(GPIO_OUT_OPEN, 0);
    bl_gpio_output_set(GPIO_OUT_CLOSE, 0);
    printf("[RELAY] All STOP\r\n");
}

// Bật chế độ Mở (Có khóa chéo an toàn - Software Interlock)
void app_relay_hold_open(void) {
    bl_gpio_output_set(GPIO_OUT_CLOSE, 0); // Đảm bảo Close tắt trước
    bl_gpio_output_set(GPIO_OUT_OPEN, 1);  // Giữ mức 1
    printf("[RELAY] HOLD OPEN (ON)\r\n");
}

// Bật chế độ Đóng (Có khóa chéo an toàn)
void app_relay_hold_close(void) {
    bl_gpio_output_set(GPIO_OUT_OPEN, 0);  // Đảm bảo Open tắt trước
    bl_gpio_output_set(GPIO_OUT_CLOSE, 1); // Giữ mức 1
    printf("[RELAY] HOLD CLOSE (ON)\r\n");
}