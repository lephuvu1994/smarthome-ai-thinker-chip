#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"   // Chứa các define pin và thời gian
#include "app_button.h" // Chứa enum sự kiện

static button_callback_t g_cb = NULL;

// Biến theo dõi thời gian nút STOP
static uint32_t g_stop_start_tick = 0;

// --- HÀM HỖ TRỢ: ĐỌC TRẠNG THÁI ỔN ĐỊNH (Cho Open/Close) ---
// Hàm cũ của bạn: Tốt cho việc nhấn nhả dứt khoát
static int check_button_state(int pin, int *last_stable_state) {
    int current_val = bl_gpio_input_get_value(pin);
    
    // Nếu thấy tín hiệu thay đổi (So với trạng thái ổn định trước đó)
    if (current_val != *last_stable_state) {
        // Chờ debounce
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
        
        // Đọc lại lần 2
        if (bl_gpio_input_get_value(pin) == current_val) {
            *last_stable_state = current_val; // Cập nhật trạng thái mới
            return current_val;
        }
    }
    return -1; // Không đổi hoặc nhiễu
}

// --- TASK QUÉT NÚT BẤM ---
static void button_task(void *pvParameters) {
    printf("[BTN] Button Task Started.\r\n");

    // Biến lưu trạng thái (Mặc định 1 - Chưa nhấn do Pull-up)
    int st_open = 1;
    int st_close = 1;
    
    // Riêng nút STOP cần biến lưu trạng thái tức thời để bắt sườn lên/xuống
    int last_stop_val = 1; 

    while (1) {
        // ---------------------------------------------------------
        // 1. QUÉT NÚT OPEN (Logic cũ: Nhấn là gửi lệnh ngay)
        // ---------------------------------------------------------
        if (check_button_state(GPIO_IN_OPEN, &st_open) == 0) {
            printf("[BTN] Event: OPEN\r\n");
            if (g_cb) g_cb(BTN_EVENT_OPEN);
        }

        // ---------------------------------------------------------
        // 2. QUÉT NÚT CLOSE (Logic cũ: Nhấn là gửi lệnh ngay)
        // ---------------------------------------------------------
        if (check_button_state(GPIO_IN_CLOSE, &st_close) == 0) {
            printf("[BTN] Event: CLOSE\r\n");
            if (g_cb) g_cb(BTN_EVENT_CLOSE);
        }

        // ---------------------------------------------------------
        // 3. QUÉT NÚT STOP (LOGIC MỚI: XỬ LÝ NHẤN GIỮ)
        // ---------------------------------------------------------
        int curr_stop_val = bl_gpio_input_get_value(GPIO_IN_STOP);

        // Phát hiện Cạnh Xuống (Vừa nhấn vào: 1 -> 0)
        if (curr_stop_val == 0 && last_stop_val == 1) {
            // Ghi nhớ thời điểm bắt đầu nhấn
            g_stop_start_tick = xTaskGetTickCount();
        }

        // Phát hiện Cạnh Lên (Vừa nhả tay ra: 0 -> 1)
        if (curr_stop_val == 1 && last_stop_val == 0) {
            // Tính toán thời gian đã giữ nút
            uint32_t now = xTaskGetTickCount();
            uint32_t press_duration = (now - g_stop_start_tick) * portTICK_PERIOD_MS;

            // -- Phân loại hành động --
            if (press_duration >= BTN_LONG_PRESS_MS) { 
                // Nếu giữ > 10s (BTN_LONG_PRESS_MS trong app_conf.h)
                printf("[BTN] STOP Long Press detected (%d ms) -> LEARN MODE\r\n", press_duration);
                if (g_cb) g_cb(BTN_EVENT_LEARN_MODE_TRIGGER);
            } 
            else if (press_duration >= BTN_DEBOUNCE_MS) {
                // Nếu nhấn ngắn bình thường (nhưng phải lớn hơn nhiễu)
                printf("[BTN] Event: STOP (Short press)\r\n");
                if (g_cb) g_cb(BTN_EVENT_STOP);
            }
            else {
                // Nhấn quá nhanh (< 20ms) -> Coi là nhiễu, bỏ qua
            }
            
            // Reset
            g_stop_start_tick = 0;
        }
        last_stop_val = curr_stop_val;

        // ---------------------------------------------------------
        // 4. Sleep để giảm tải CPU
        // ---------------------------------------------------------
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_DELAY_MS));
    }
}

// --- INIT ---
void app_button_init(button_callback_t cb) {
    g_cb = cb;

    printf("[BTN] Init GPIO Input...\r\n");
    
    // Cấu hình Input Pull-up (Mức 1 khi không nhấn, Mức 0 khi nhấn)
    bl_gpio_enable_input(GPIO_IN_OPEN, 1, 0);
    bl_gpio_enable_input(GPIO_IN_CLOSE, 1, 0);
    
    // Đã bỏ comment nút STOP để kích hoạt tính năng mới
    bl_gpio_enable_input(GPIO_IN_STOP, 1, 0);

    // Cấu hình Input cho nút học lệnh RF
    // bl_gpio_enable_input(GPIO_IN_SETUP_LEARN_RF, 1, 0);

    // Tạo Task với Stack đủ dùng
    xTaskCreate(button_task, "btn_task", 2048, NULL, 10, NULL);
    printf("[BTN] Init Success.\r\n");
}