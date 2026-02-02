#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_button.h"

static button_callback_t g_cb = NULL;

// Hàm hỗ trợ đọc trạng thái nút với Debounce 2 lớp
// Trả về: 0 (Đang nhấn), 1 (Nhả), -1 (Nhiễu/Không đổi)
static int check_button_state(int pin, int *stable_state) {
    int raw_val = bl_gpio_input_get_value(pin);

    if (raw_val != *stable_state) {
        // Có thay đổi, chờ lọc nhiễu
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));

        // Đọc lại lần 2
        if (bl_gpio_input_get_value(pin) == raw_val) {
            *stable_state = raw_val;
            return raw_val; // Trạng thái mới đã được xác nhận
        }
    }
    return -1; // Không đổi hoặc nhiễu
}

static void button_task(void *pvParameters) {
    // 1. Chờ khởi động xong để né nhiễu Boot log (TXD/RXD)
    vTaskDelay(pdMS_TO_TICKS(2000));
    printf("[BTN] Button Polling Started\r\n");

    // 2. Init GPIO Input Pull-up
    bl_gpio_enable_input(GPIO_IN_OPEN, 1, 0);
    bl_gpio_enable_input(GPIO_IN_CLOSE, 1, 0);
    bl_gpio_enable_input(GPIO_IN_STOP, 1, 0);
    bl_gpio_enable_input(GPIO_IN_LOCK, 1, 0);

    // 3. Biến lưu trạng thái ổn định (Mặc định 1 - chưa nhấn)
    int st_open = 1, st_close = 1, st_stop = 1, st_lock = 1;

    while (1) {
        // --- NÚT OPEN ---
        if (check_button_state(GPIO_IN_OPEN, &st_open) == 0) {
            if (g_cb) g_cb(BTN_EVENT_OPEN);
        }

        // --- NÚT CLOSE ---
        if (check_button_state(GPIO_IN_CLOSE, &st_close) == 0) {
            if (g_cb) g_cb(BTN_EVENT_CLOSE);
        }

        // --- NÚT STOP ---
        if (check_button_state(GPIO_IN_STOP, &st_stop) == 0) {
            if (g_cb) g_cb(BTN_EVENT_STOP);
        }

        // --- NÚT LOCK ---
        if (check_button_state(GPIO_IN_LOCK, &st_lock) == 0) {
            if (g_cb) g_cb(BTN_EVENT_LOCK_PRESS);
        }

        // Ngủ để nhường CPU
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_DELAY_MS));
    }
}

void app_button_init(button_callback_t cb) {
    g_cb = cb;
    xTaskCreate(button_task, "btn_task", 1024, NULL, 10, NULL);
}
