#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <bl_gpio.h>
#include <blog.h>       // Thêm thư viện blog
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

static uint32_t g_boot_tick = 0;

// Cờ Startup Guard (Mặc định 0 - Chưa an toàn)
static int g_stop_eligible = 0;
static int g_rf_eligible = 0;
static int g_open_eligible = 0;
static int g_close_eligible = 0;

// ============================================================================
// TASK QUÉT NÚT BẤM
// ============================================================================
static void button_task(void *pvParameters) {
    blog_info("Button Task Started.");
    g_boot_tick = xTaskGetTickCount();

    while (1) {
        uint32_t now = xTaskGetTickCount();
        
        // --- 1. CHỐNG NHIỄU 5S ĐẦU KHI KHỞI ĐỘNG ---
        if ((now - g_boot_tick) * portTICK_PERIOD_MS < 5000) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Đọc giá trị các chân GPIO (Mức 0 là nhấn, Mức 1 là nhả)
        int s_stop  = bl_gpio_input_get_value(GPIO_IN_STOP);
        int s_open  = bl_gpio_input_get_value(GPIO_IN_OPEN);
        int s_close = bl_gpio_input_get_value(GPIO_IN_CLOSE);
        int s_rf    = bl_gpio_input_get_value(GPIO_IN_SETUP_LEARN_RF);

        // --- 2. STARTUP GUARD: KIỂM TRA CHÂN NHẢ (MỨC 1) ---
        // Chỉ cho phép hoạt động nếu chân đã từng ở mức 1 sau khi hết 5s chống nhiễu
        if (s_stop == 1)  g_stop_eligible = 1;
        if (s_rf == 1)    g_rf_eligible = 1;
        if (s_open == 1)  g_open_eligible = 1;
        if (s_close == 1) g_close_eligible = 1;

        // =========================================================
        // 3. XỬ LÝ COMBO: STOP + (OPEN HOẶC CLOSE) -> RESET WIFI
        // =========================================================
        if (s_stop == 0 && (s_open == 0 || s_close == 0) && g_stop_eligible) {
            
            if (g_combo_pressed_tick == 0) {
                g_combo_pressed_tick = now;
                g_combo_handled = 0;
                g_stop_handled = 1; 
            }
            else {
                if (!g_combo_handled && (now - g_combo_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                    blog_warn("COMBO Held > 10s -> TRIGGER WIFI RESET");
                    if (g_cb) g_cb(BTN_EVENT_WIFI_RESET_TRIGGER);
                    g_combo_handled = 1; 
                }
            }
        }
        else {
            g_combo_pressed_tick = 0;
            g_combo_handled = 0;

            // =====================================================
            // 4. XỬ LÝ NÚT STOP ĐƠN LẺ (Học hành trình)
            // =====================================================
            if (s_stop == 0 && g_stop_eligible) { 
                if (g_stop_pressed_tick == 0) {
                    g_stop_pressed_tick = now;
                    if (g_combo_pressed_tick == 0) g_stop_handled = 0; 
                } 
                else {
                    if (!g_stop_handled && (now - g_stop_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                        blog_info("STOP Held > 10s -> Trigger TRAVEL LEARN");
                        if (g_cb) g_cb(BTN_EVENT_LEARN_TRAVEL_TRIGGER);
                        g_stop_handled = 1; 
                    }
                }
            } 
            else { 
                if (g_stop_pressed_tick != 0 && !g_stop_handled && g_stop_eligible) {
                    uint32_t press_duration = (now - g_stop_pressed_tick) * portTICK_PERIOD_MS;
                    if (press_duration > BTN_DEBOUNCE_MS) {
                         blog_info("Event: STOP (Short press)");
                         if (g_cb) g_cb(BTN_EVENT_STOP);
                    }
                }
                g_stop_pressed_tick = 0;
                g_stop_handled = 0; 
            }

            // =====================================================
            // 5. XỬ LÝ NÚT OPEN / CLOSE (Sườn xuống)
            // =====================================================
            // Nút OPEN
            if (s_open == 0 && last_open_val == 1 && g_combo_pressed_tick == 0 && g_open_eligible) {
                blog_info("Event: OPEN");
                if (g_cb) g_cb(BTN_EVENT_OPEN);
            }
            last_open_val = s_open;

            // Nút CLOSE
            if (s_close == 0 && last_close_val == 1 && g_combo_pressed_tick == 0 && g_close_eligible) {
                blog_info("Event: CLOSE");
                if (g_cb) g_cb(BTN_EVENT_CLOSE);
            }
            last_close_val = s_close;
        }

        // =========================================================
        // 6. XỬ LÝ NÚT RF SETUP (Học Remote)
        // =========================================================
        if (s_rf == 0 && g_rf_eligible) { 
            if (g_rf_pressed_tick == 0) {
                g_rf_pressed_tick = now;
                g_rf_handled = 0;
            } 
            else {
                if (!g_rf_handled && (now - g_rf_pressed_tick) * portTICK_PERIOD_MS >= BTN_LONG_PRESS_MS) {
                    blog_info("RF BTN Held > 10s -> Trigger RF LEARN");
                    if (g_cb) g_cb(BTN_EVENT_LEARN_RF_TRIGGER);
                    g_rf_handled = 1;
                }
            }
        } 
        else { 
            g_rf_pressed_tick = 0;
            g_rf_handled = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_DELAY_MS));
    }
}

// ============================================================================
// INIT FUNCTION
// ============================================================================
void app_button_init(button_callback_t cb) {
    g_cb = cb;
    
    // Cấu hình Input Pull-up
    bl_gpio_enable_input(GPIO_IN_OPEN, 1, 0);
    bl_gpio_enable_input(GPIO_IN_CLOSE, 1, 0);
    bl_gpio_enable_input(GPIO_IN_STOP, 1, 0);
    bl_gpio_enable_input(GPIO_IN_SETUP_LEARN_RF, 1, 0);

    // Tạo Task
    xTaskCreate(button_task, "btn_task", 2048, NULL, 10, NULL);
    blog_info("Init Success.");
}