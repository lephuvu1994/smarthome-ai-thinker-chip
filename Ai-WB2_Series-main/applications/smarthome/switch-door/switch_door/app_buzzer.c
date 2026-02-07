#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_buzzer.h"

static TimerHandle_t g_buzzer_timer = NULL;

// Callback: Tự động tắt còi khi hết giờ
static void buzzer_auto_off_cb(TimerHandle_t xTimer) {
    bl_gpio_output_set(GPIO_OUT_BUZZER, 0); // Tắt còi
}

void app_buzzer_init(void) {
    // 1. Cấu hình GPIO Output (Mặc định tắt)
    // bl_gpio_enable_output(GPIO_OUT_BUZZER, 0, 0);

    // 2. Tạo Timer (One-shot)
    g_buzzer_timer = xTimerCreate("BuzzerTmr", 
                                pdMS_TO_TICKS(100), // Giá trị khởi tạo tạm
                                pdFALSE, 
                                (void *)0, 
                                buzzer_auto_off_cb);
}

// Hàm kích hoạt còi (Non-blocking)
void app_buzzer_beep(int duration_ms) {
    if (g_buzzer_timer == NULL) return;

    // 1. Bật còi ngay lập tức
    bl_gpio_output_set(GPIO_OUT_BUZZER, 1);

    // 2. Cài đặt thời gian tắt và chạy Timer
    xTimerChangePeriod(g_buzzer_timer, pdMS_TO_TICKS(duration_ms), 0);
    xTimerStart(g_buzzer_timer, 0);
}