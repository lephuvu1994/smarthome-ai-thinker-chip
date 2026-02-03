#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"
#include "app_button.h"

static button_callback_t g_cb = NULL;

// --- HÀM HỖ TRỢ: ĐỌC TRẠNG THÁI ỔN ĐỊNH (DEBOUNCE) ---
// Trả về: 0 (Nhấn), 1 (Nhả), -1 (Chưa xác định/Nhiễu)
static int check_button_state(int pin, int *last_stable_state) {
    int current_val = bl_gpio_input_get_value(pin);
    
    // Nếu thấy tín hiệu thay đổi
    if (current_val != *last_stable_state) {
        // Chờ 20ms để lọc nhiễu
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
        
        // Đọc lại lần 2
        if (bl_gpio_input_get_value(pin) == current_val) {
            *last_stable_state = current_val; // Xác nhận trạng thái mới
            return current_val;
        }
    }
    return -1; // Không đổi hoặc là nhiễu
}

// --- TASK QUÉT NÚT BẤM (CHẠY NGẦM) ---
static void button_task(void *pvParameters) {
    printf("[BTN] Button Task Started (GPIO 12/2/11/14)\r\n");

    // Biến lưu trạng thái (Mặc định 1 - Chưa nhấn do dùng Pull-up)
    int st_open = 1, st_close = 1, st_stop = 1, st_lock = 1;

    while (1) {
        // 1. Quét nút OPEN
        if (check_button_state(GPIO_IN_OPEN, &st_open) == 0) {
            printf("[BTN] Event: OPEN\r\n");
            if (g_cb) g_cb(BTN_EVENT_OPEN);
        }

        // 2. Quét nút CLOSE
        if (check_button_state(GPIO_IN_CLOSE, &st_close) == 0) {
            printf("[BTN] Event: CLOSE\r\n");
            if (g_cb) g_cb(BTN_EVENT_CLOSE);
        }

//        // 3. Quét nút STOP
//        if (check_button_state(GPIO_IN_STOP, &st_stop) == 0) {
//            printf("[BTN] Event: STOP\r\n");
//            if (g_cb) g_cb(BTN_EVENT_STOP);
//        }
//
//        // 4. Quét nút LOCK
//        if (check_button_state(GPIO_IN_LOCK, &st_lock) == 0) {
//            printf("[BTN] Event: LOCK PRESS\r\n");
//            if (g_cb) g_cb(BTN_EVENT_LOCK_PRESS);
//        }

        // Nhường CPU 50ms
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_DELAY_MS));
    }
}

// --- HÀM INIT (GỌI TỪ MAIN) ---
void app_button_init(button_callback_t cb) {
    g_cb = cb;

    printf("app_button_init\r\n");
    // 1. CẤU HÌNH GPIO INPUT TẠI ĐÂY (Main không cần lo nữa)
    // Mode: Input, Pull-up (1), No Pull-down (0)
    bl_gpio_enable_input(GPIO_IN_OPEN, 1, 0);
    bl_gpio_enable_input(GPIO_IN_CLOSE, 1, 0);
//    bl_gpio_enable_input(GPIO_IN_STOP, 1, 0);
//    bl_gpio_enable_input(GPIO_IN_LOCK, 1, 0);
    printf("app_button_success\r\n");
    
    // 2. TẠO TASK
    xTaskCreate(button_task, "btn_task", 2048, NULL, 10, NULL);
}
