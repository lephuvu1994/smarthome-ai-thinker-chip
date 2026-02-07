#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_output_relay.h"

// ==============================================================================
// TASK PHỤ: TẮT RELAY SAU 1 GIÂY (NON-BLOCKING)
// ==============================================================================
static void relay_pulse_worker(void *pvParameters) {
    uint32_t pin = (uint32_t)pvParameters;

    // 1. Chờ 1000ms (Task này ngủ, CPU làm việc khác, không treo hệ thống)
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. Tắt Relay (Về mức 1 - Active Low)
    // Lưu ý: Nếu Relay của bạn kích mức 1 thì sửa số 1 thành 0
    bl_gpio_output_set(pin, 0);
    
    printf("[RELAY] Pin %d Auto OFF (Pulse Done)\r\n", (int)pin);

    // 3. Tự xóa Task để giải phóng Ram
    vTaskDelete(NULL);
}

// ==============================================================================
// HÀM KÍCH XUNG (GỌI TỪ CORE)
// ==============================================================================
void app_relay_pulse(int pin) {
    // 1. Bật Relay NGAY LẬP TỨC (Mức 0 - Active Low)
    bl_gpio_output_set(pin, 1);
    printf("[RELAY] Pin %d Triggered ON\r\n", pin);

    // 2. Tạo Task phụ để canh giờ tắt (Stack 512 là đủ)
    // Task này sẽ chạy song song, code phía dưới chạy tiếp luôn không bị dừng
    xTaskCreate(relay_pulse_worker, "pulse_worker", 512, (void*)pin, 10, NULL);
}

// ==============================================================================
// KHỞI TẠO RELAY (QUAN TRỌNG)
// ==============================================================================
void app_relay_init(void) {
    // Cấu hình Output
    bl_gpio_enable_output(GPIO_OUT_CLOSE, 0, 0);
    bl_gpio_enable_output(GPIO_OUT_OPEN, 0, 0);
    bl_gpio_enable_output(GPIO_OUT_STOP, 0, 0);

    // [QUAN TRỌNG] Set mức 1 ngay lập tức để Relay KHÔNG tự đóng khi khởi động
    bl_gpio_output_set(GPIO_OUT_OPEN, 0);
    bl_gpio_output_set(GPIO_OUT_CLOSE, 0);
    bl_gpio_output_set(GPIO_OUT_STOP, 0);
    
    printf("[RELAY] Init Done. All forced to OFF (High Level)\r\n");
}

// ==============================================================================
// DỪNG KHẨN CẤP (AN TOÀN)
// ==============================================================================
void app_relay_stop_all(void) {
    // Ngắt toàn bộ Relay ngay lập tức (Về mức 1)
    bl_gpio_output_set(GPIO_OUT_OPEN, 0);
    bl_gpio_output_set(GPIO_OUT_CLOSE, 0);
    bl_gpio_output_set(GPIO_OUT_STOP, 0);
    printf("[RELAY] Safety Stop Triggered\r\n");
}