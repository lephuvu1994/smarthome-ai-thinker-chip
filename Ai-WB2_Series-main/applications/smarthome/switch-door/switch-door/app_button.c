#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include "app_conf.h"   // Chứa các define pin và thời gian (BTN_LONG_PRESS_MS)
#include "app_button.h" // Chứa enum sự kiện

// ============================================================================
// BIẾN TOÀN CỤC
// ============================================================================
static button_callback_t g_cb = NULL;

// Biến theo dõi nút STOP
static uint32_t g_stop_pressed_tick = 0;
static int g_stop_handled = 0;

// Biến theo dõi nút RF SETUP
static uint32_t g_rf_pressed_tick = 0;
static int g_rf_handled = 0;

// Biến theo dõi COMBO (Reset Wifi)
static uint32_t g_combo_pressed_tick = 0;
static int g_combo_handled = 0;

// Biến lưu trạng thái trước đó để bắt sườn xung (Edge Detection)
static int last_open_val = 1;
static int last_close_val = 1;
// Nút Stop cũng cần lưu trạng thái cũ để bắt sườn xuống chính xác nếu cần
// (nhưng logic giữ 10s dùng level check là chủ yếu)

// ============================================================================
// TASK QUÉT NÚT BẤM
// ============================================================================
static void button_task(void *pvParameters) {
    printf("[BTN] Button Task Started.\r\n");

    while (1) {
        uint32_t now = xTaskGetTickCount();

        // 1. ĐỌC TẤT CẢ CÁC INPUT CÙNG LÚC
        // ---------------------------------------------------------
        int s_stop  = bl_gpio_input_get_value(GPIO_IN_STOP);
        int s_open  = bl_gpio_input_get_value(GPIO_IN_OPEN);
        int s_close = bl_gpio_input_get_value(GPIO_IN_CLOSE);
        int s_rf    = bl_gpio_input_get_value(GPIO_IN_SETUP_LEARN_RF);

        // =========================================================
        // 2. XỬ LÝ COMBO: STOP + (OPEN HOẶC CLOSE) -> RESET WIFI
        //    (Ưu tiên cao nhất để override nút đơn lẻ)
        // =========================================================
        // Logic: STOP đang nhấn VÀ (OPEN đang nhấn HOẶC CLOSE đang nhấn)
        if (s_stop == 0 && (s_open == 0 || s_close == 0)) {
            
            if (g_combo_pressed_tick == 0) {
                // Bắt đầu đếm giờ Combo
                g_combo_pressed_tick = now;
                g_combo_handled = 0;
                
                // [QUAN TRỌNG] Đánh dấu nút STOP đơn lẻ là "đã xử lý"
                // Để tránh việc nó kích hoạt tính năng "Học hành trình" của riêng nó
                g_stop_handled = 1; 
            }
            else {
                // Đang giữ Combo -> Kiểm tra 10s
                if (!g_combo_handled && (now - g_combo_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                    printf("[BTN] COMBO Held > 10s -> TRIGGER WIFI RESET\r\n");
                    
                    if (g_cb) g_cb(BTN_EVENT_WIFI_RESET_TRIGGER);
                    
                    g_combo_handled = 1; // Đánh dấu đã xử lý
                }
            }
        }
        else {
            // Không phải Combo (hoặc đã nhả ra)
            g_combo_pressed_tick = 0;
            g_combo_handled = 0;

            // =====================================================
            // 3. XỬ LÝ NÚT STOP ĐƠN LẺ (Chỉ khi không phải Combo)
            // =====================================================
            if (s_stop == 0) { // Đang NHẤN
                if (g_stop_pressed_tick == 0) {
                    g_stop_pressed_tick = now;
                    // Reset cờ handled chỉ khi chắc chắn không phải là dư âm của combo
                    // (Nếu vừa nhả combo ra, g_stop_handled vẫn = 1 để chặn lệnh stop rác)
                    if (g_combo_pressed_tick == 0) g_stop_handled = 0; 
                } 
                else {
                    // Đang giữ -> Check 10s -> Học hành trình
                    if (!g_stop_handled && (now - g_stop_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                        printf("[BTN] STOP Held > 10s -> Trigger TRAVEL LEARN\r\n");
                        if (g_cb) g_cb(BTN_EVENT_LEARN_TRAVEL_TRIGGER);
                        g_stop_handled = 1; 
                    }
                }
            } 
            else { // Đang NHẢ
                // Nếu trước đó có nhấn và CHƯA xử lý (chưa đủ 10s và ko phải combo)
                if (g_stop_pressed_tick != 0 && !g_stop_handled) {
                    uint32_t press_duration = (now - g_stop_pressed_tick) * portTICK_PERIOD_MS;
                    
                    // Chống nhiễu nhẹ
                    if (press_duration > BTN_DEBOUNCE_MS) {
                         printf("[BTN] Event: STOP (Short press)\r\n");
                         if (g_cb) g_cb(BTN_EVENT_STOP);
                    }
                }
                g_stop_pressed_tick = 0;
                // Khi nhả hẳn STOP thì mới cho phép lần nhấn tiếp theo được xử lý lại
                g_stop_handled = 0; 
            }

            // =====================================================
            // 4. XỬ LÝ NÚT OPEN (Sườn xuống)
            // =====================================================
            // Thêm điều kiện: Không đang trong trạng thái tính giờ Combo
            if (s_open == 0 && last_open_val == 1 && g_combo_pressed_tick == 0) {
                printf("[BTN] Event: OPEN\r\n");
                if (g_cb) g_cb(BTN_EVENT_OPEN);
            }
            last_open_val = s_open;

            // =====================================================
            // 5. XỬ LÝ NÚT CLOSE (Sườn xuống)
            // =====================================================
            if (s_close == 0 && last_close_val == 1 && g_combo_pressed_tick == 0) {
                printf("[BTN] Event: CLOSE\r\n");
                if (g_cb) g_cb(BTN_EVENT_CLOSE);
            }
            last_close_val = s_close;
        }

        // =========================================================
        // 6. XỬ LÝ NÚT RF SETUP (Độc lập hoàn toàn)
        // =========================================================
        if (s_rf == 0) { // Đang NHẤN
            if (g_rf_pressed_tick == 0) {
                g_rf_pressed_tick = now;
                g_rf_handled = 0;
            } 
            else {
                // Kiểm tra giữ 10s
                if (!g_rf_handled && (now - g_rf_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                    printf("[BTN] RF BTN Held > 10s -> Trigger RF LEARN\r\n");
                    if (g_cb) g_cb(BTN_EVENT_LEARN_RF_TRIGGER);
                    g_rf_handled = 1;
                }
            }
        } 
        else { // Đang NHẢ
            g_rf_pressed_tick = 0;
            g_rf_handled = 0;
        }

        // ---------------------------------------------------------
        // 7. Sleep
        // ---------------------------------------------------------
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_DELAY_MS));
    }
}

// ============================================================================
// INIT FUNCTION
// ============================================================================
void app_button_init(button_callback_t cb) {
    g_cb = cb;

    printf("[BTN] Init GPIO Inputs...\r\n");
    
    // Cấu hình Input Pull-up (Mức 1 khi không nhấn, Mức 0 khi nhấn)
    bl_gpio_enable_input(GPIO_IN_OPEN, 1, 0);
    bl_gpio_enable_input(GPIO_IN_CLOSE, 1, 0);
    bl_gpio_enable_input(GPIO_IN_STOP, 1, 0);
    bl_gpio_enable_input(GPIO_IN_SETUP_LEARN_RF, 1, 0);

    // Tạo Task
    xTaskCreate(button_task, "btn_task", 2048, NULL, 10, NULL);
    printf("[BTN] Init Success.\r\n");
}