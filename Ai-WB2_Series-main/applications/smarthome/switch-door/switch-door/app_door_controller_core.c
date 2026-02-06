#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>

#include "app_conf.h"          // Cấu hình hệ thống (Thời gian, GPIO...)
#include "app_door_controller_core.h"
#include "app_output_relay.h"  // Driver Relay (Mode Hold)
#include "app_mqtt.h"          // Gửi trạng thái lên Server
#include "app_storage.h"       // Lưu/Đọc Flash
#include "app_buzzer.h"        // Driver Còi (Buzzer)
#include "app_events.h"        // Driver Còi (Buzzer)

// ==============================================================================
// ĐỊNH NGHĨA ENUM & STRUCT
// ==============================================================================

// Các bước trong quy trình học RF
typedef enum {
    RF_LEARN_NONE = 0,      // Không học gì cả
    RF_LEARN_WAIT_OPEN,     // Đang chờ bấm nút LÊN
    RF_LEARN_WAIT_STOP,     // Đang chờ bấm nút DỪNG
    RF_LEARN_WAIT_CLOSE,    // Đang chờ bấm nút XUỐNG
    RF_LEARN_WAIT_LOCK,     // Đang chờ bấm nút KHÓA
    RF_LEARN_DONE           // Hoàn tất
} rf_learn_step_t;

// ==============================================================================
// BIẾN TOÀN CỤC (GLOBAL VARIABLES)
// ==============================================================================

// --- QUẢN LÝ CỬA & HÀNH TRÌNH ---
static int g_lock_active = 0;           // Trạng thái khóa (1: Khóa, 0: Mở)
static int g_is_travel_learning = 0;    // Trạng thái học hành trình (Đổi tên từ g_is_learning)
static uint32_t g_learn_start_tick = 0; // Mốc thời gian bắt đầu đo hành trình
static uint32_t g_travel_time_ms = DEFAULT_TRAVEL_TIME_MS; // Thời gian hành trình

// --- QUẢN LÝ HỌC RF ---
static rf_learn_step_t g_rf_learn_step = RF_LEARN_NONE;
static uint32_t g_saved_rf_codes[4] = {0}; // Mã đã lưu: [0]Open, [1]Stop, [2]Close, [3]Lock
static uint32_t g_temp_rf_codes[4]  = {0}; // Mã tạm thời khi đang học

// --- TIMERS ---
static TimerHandle_t g_travel_timer = NULL;        // Timer ngắt relay tự động
static TimerHandle_t g_learn_timeout_timer = NULL; // Timer timeout cho cả 2 chế độ học

// Thêm biến trạng thái config wifi
static int g_is_wifi_config_mode = 0;
// ==============================================================================
// TIMER CALLBACKS
// ==============================================================================

/**
 * @brief Auto Stop: Hết giờ hành trình -> Dừng lại
 * Output: {"state": "STOP"}
 */
static void auto_stop_callback(TimerHandle_t xTimer) {
    printf("[CORE] Auto Stop Triggered\r\n");
    app_relay_stop_all();
    app_mqtt_pub_status("{\"state\": \"STOP\"}"); 
}

/**
 * @brief Learn Timeout: Quá thời gian không thao tác (3 phút hoặc 60s)
 * Xử lý timeout cho cả Học Hành Trình và Học RF
 */
/**
 * @brief Hàm Callback này được gọi khi Timer 3 phút hết hạn
 */
static void learn_timeout_callback(TimerHandle_t xTimer) {
    printf("[CORE] --- Learn Mode TIMEOUT reached ---\r\n");

    // TRƯỜNG HỢP 1: Đang trong chế độ cấu hình Wifi (Combo 10s)
    if (g_is_wifi_config_mode) {
        printf("[CORE] Wifi Config Timeout. No one connected. Closing BLE...\r\n");
        g_is_wifi_config_mode = 0;
        
        // Gửi sự kiện sang Main.c để gọi hàm disable_ble_adv()
        app_send_event(APP_EVENT_WIFI_CONFIG_TIMEOUT, NULL); 
    }

    // TRƯỜNG HỢP 2: Đang trong chế độ học RF (Giữ RF 10s)
    if (g_rf_learn_step != RF_LEARN_NONE) {
        printf("[CORE] RF Learn Timeout. Reverting to old remote codes.\r\n");
        g_rf_learn_step = RF_LEARN_NONE;
        
        // Báo trạng thái lên MQTT để App biết đã hết giờ
        app_mqtt_pub_status("{\"rf_learn\": \"TIMEOUT\"}");
    }

    // TRƯỜNG HỢP 3: Đang trong chế độ học Hành trình (Giữ STOP 10s)
    if (g_is_travel_learning) {
        printf("[CORE] Travel Learn Timeout.\r\n");
        g_is_travel_learning = 0;
        g_learn_start_tick = 0;
        
        app_mqtt_pub_status("{\"calibration\": \"TIMEOUT\"}");
    }
    
    // Luôn dừng relay và kêu bíp báo hiệu lỗi/hết giờ
    app_relay_stop_all();
    app_buzzer_beep(BUZZER_TIME_SHORT);
}

// ==============================================================================
// INIT FUNCTION
// ==============================================================================
void app_door_controller_core_init(void) {
    // 1. Khởi tạo Còi & Biến
    app_buzzer_init();
    g_lock_active = 0;
    g_is_travel_learning = 0;
    g_rf_learn_step = RF_LEARN_NONE;

    // 2. Load thời gian hành trình từ Flash
    uint32_t saved_time = 0;
    if (storage_get_travel_time(&saved_time)) {
        if (saved_time > MIN_LEARN_TIME_MS && saved_time < MAX_SAFE_TIME_MS) {
            g_travel_time_ms = saved_time;
        }
    }
    printf("[CORE] Init. Travel Time: %d ms\r\n", (int)g_travel_time_ms);

    // 3. Load mã RF từ Flash (Giả sử hàm này tồn tại trong app_storage.c)
    // Nếu chưa có thì mảng g_saved_rf_codes sẽ là {0,0,0,0}
    storage_get_rf_codes(g_saved_rf_codes);
    printf("[CORE] Init. RF Codes Loaded.\r\n");

    // 4. Init Timers
    g_travel_timer = xTimerCreate("TravelTmr", pdMS_TO_TICKS(g_travel_time_ms), pdFALSE, (void *)0, auto_stop_callback);
    // Timeout mặc định 3 phút
    g_learn_timeout_timer = xTimerCreate("LearnTimeout", pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), pdFALSE, (void *)0, learn_timeout_callback);
}

// ==============================================================================
// XỬ LÝ RF RAW (QUAN TRỌNG)
// Hàm này được gọi từ main.c khi nhận tín hiệu RF
// ==============================================================================
void app_door_core_handle_rf_raw(uint32_t rf_code) {
    // TRƯỜNG HỢP A: ĐANG TRONG CHẾ ĐỘ HỌC RF
    if (g_rf_learn_step != RF_LEARN_NONE) {
        printf("[RF-LEARN] Step: %d - Code: %06X\r\n", g_rf_learn_step, rf_code);
        app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp xác nhận nhận sóng

        switch (g_rf_learn_step) {
            case RF_LEARN_WAIT_OPEN:
                g_temp_rf_codes[0] = rf_code; // Lưu Open
                g_rf_learn_step = RF_LEARN_WAIT_STOP;
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_STOP\"}");
                printf(">>> Saved OPEN. Waiting STOP...\r\n");
                break;

            case RF_LEARN_WAIT_STOP:
                g_temp_rf_codes[1] = rf_code; // Lưu Stop
                g_rf_learn_step = RF_LEARN_WAIT_CLOSE;
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_CLOSE\"}");
                printf(">>> Saved STOP. Waiting CLOSE...\r\n");
                break;

            case RF_LEARN_WAIT_CLOSE:
                g_temp_rf_codes[2] = rf_code; // Lưu Close
                g_rf_learn_step = RF_LEARN_WAIT_LOCK;
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_LOCK\"}");
                printf(">>> Saved CLOSE. Waiting LOCK...\r\n");
                break;

            case RF_LEARN_WAIT_LOCK:
                g_temp_rf_codes[3] = rf_code; // Lưu Lock
                
                // --- HOÀN TẤT ---
                // Copy từ Temp sang Saved
                memcpy(g_saved_rf_codes, g_temp_rf_codes, sizeof(g_saved_rf_codes));
                
                // Lưu xuống Flash
                storage_save_rf_codes(g_saved_rf_codes);
                
                // Reset trạng thái
                g_rf_learn_step = RF_LEARN_NONE;
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

                // Báo thành công
                app_mqtt_pub_status("{\"rf_learn\": \"DONE\"}");
                app_buzzer_beep(BUZZER_TIME_LONG); // Bíp dài
                printf(">>> RF LEARN DONE!\r\n");
                break;
                
            default: break;
        }
        
        // Reset lại Timer Timeout (để người dùng có thời gian bấm tiếp)
        if (g_rf_learn_step != RF_LEARN_NONE) {
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(RF_LEARN_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
        }
        return; // Kết thúc xử lý học
    }

    // TRƯỜNG HỢP B: CHẾ ĐỘ HOẠT ĐỘNG BÌNH THƯỜNG
    // So sánh code nhận được với code đã lưu để điều khiển
    if (rf_code == g_saved_rf_codes[0]) {      // OPEN
        app_door_controller_core_execute_cmd_string("OPEN");
    } 
    else if (rf_code == g_saved_rf_codes[1]) { // STOP
        app_door_controller_core_execute_cmd_string("STOP");
    }
    else if (rf_code == g_saved_rf_codes[2]) { // CLOSE
        app_door_controller_core_execute_cmd_string("CLOSE");
    }
    else if (rf_code == g_saved_rf_codes[3]) { // LOCK
        // Logic Lock Toggle hoặc Lock riêng
        if (g_lock_active) app_door_controller_core_execute_cmd_string("UNLOCK");
        else app_door_controller_core_execute_cmd_string("LOCK");
    }
}

// ==============================================================================
// CORE LOGIC EXECUTION (XỬ LÝ LỆNH)
// ==============================================================================
void app_door_controller_core_execute_cmd_string(const char* cmd) {
    printf("[CORE] Executing CMD: %s\r\n", cmd);

    // --------------------------------------------------------------------------
    // LỆNH: LEARN_MODE_ON (Học hành trình - Thời gian cửa chạy)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "LEARN_MODE_ON") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("{\"error\": \"LOCKED\"}");
            app_buzzer_beep(BUZZER_TIME_SHORT); return;
        }

        g_is_travel_learning = 1;
        g_learn_start_tick = 0;
        app_relay_stop_all();
        
        // Timeout 3 phút cho học hành trình
        xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0);
        xTimerStart(g_learn_timeout_timer, 0);

        app_mqtt_pub_status("{\"calibration\": \"ON\", \"state\": \"STOP\"}"); 
        app_buzzer_beep(BUZZER_TIME_LONG);
        return;
    }

    // --------------------------------------------------------------------------
    // LỆNH: RF_LEARN_MODE (Học tay khiển RF)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "RF_LEARN_MODE") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("{\"error\": \"LOCKED\"}"); return;
        }

        g_rf_learn_step = RF_LEARN_WAIT_OPEN;
        app_relay_stop_all();

        // Timeout 60s cho học RF
        xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(RF_LEARN_TIMEOUT_MS), 0);
        xTimerStart(g_learn_timeout_timer, 0);

        app_mqtt_pub_status("{\"rf_learn\": \"WAIT_OPEN\"}"); 
        app_buzzer_beep(BUZZER_TIME_LONG);
        printf("[CORE] START RF LEARN -> Wait OPEN\r\n");
        return;
    }

    // --------------------------------------------------------------------------
    // LỆNH: STOP
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "STOP") == 0) {
        // Nếu bấm STOP trên App khi đang học RF -> Hủy học RF
        if (g_rf_learn_step != RF_LEARN_NONE) {
            g_rf_learn_step = RF_LEARN_NONE;
            if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
            app_mqtt_pub_status("{\"rf_learn\": \"CANCELLED\"}");
            app_buzzer_beep(BUZZER_TIME_SHORT);
            return;
        }

        app_relay_stop_all();
        if (xTimerIsTimerActive(g_travel_timer)) xTimerStop(g_travel_timer, 0);
        if (xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

        // Xử lý lưu kết quả Học Hành Trình
        if (g_is_travel_learning) {
            if (g_learn_start_tick > 0) {
                uint32_t now = xTaskGetTickCount();
                uint32_t duration = (now - g_learn_start_tick) * portTICK_PERIOD_MS;
                
                if (duration > MIN_LEARN_TIME_MS) {
                    g_travel_time_ms = duration;
                    storage_save_travel_time(g_travel_time_ms);
                    xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
                    
                    app_buzzer_beep(BUZZER_TIME_LONG);
                    app_mqtt_pub_status("{\"calibration\": \"DONE\", \"state\": \"STOP\"}");
                } else {
                    app_buzzer_beep(BUZZER_TIME_SHORT);
                    app_mqtt_pub_status("{\"calibration\": \"FAILED_SHORT\", \"state\": \"STOP\"}");
                }
            } else {
                app_buzzer_beep(BUZZER_TIME_SHORT);
                app_mqtt_pub_status("{\"calibration\": \"CANCELLED\", \"state\": \"STOP\"}");
            }
            g_is_travel_learning = 0;
            g_learn_start_tick = 0;
        } else {
            app_buzzer_beep(BUZZER_TIME_SHORT);
            app_mqtt_pub_status("{\"state\": \"STOP\"}");
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: OPEN
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "OPEN") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("{\"error\": \"LOCKED\"}");
            app_buzzer_beep(BUZZER_TIME_SHORT); return;
        }

        app_buzzer_beep(BUZZER_TIME_SHORT);
        app_relay_hold_open();
        app_mqtt_pub_status("{\"state\": \"OPEN\"}");

        if (g_is_travel_learning) {
            if (g_learn_start_tick == 0) {
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
                g_learn_start_tick = xTaskGetTickCount();
            }
        } else {
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: CLOSE
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "CLOSE") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("{\"error\": \"LOCKED\"}");
            app_buzzer_beep(BUZZER_TIME_SHORT); return;
        }

        app_buzzer_beep(BUZZER_TIME_SHORT);
        app_relay_hold_close();
        app_mqtt_pub_status("{\"state\": \"CLOSE\"}");

        if (g_is_travel_learning) {
            if (g_learn_start_tick == 0) {
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
                g_learn_start_tick = xTaskGetTickCount();
            }
        } else {
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: LOCK / UNLOCK
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "LOCK") == 0) {
        g_lock_active = 1;
        app_relay_stop_all();
        app_mqtt_pub_status("{\"child_lock\": \"LOCKED\", \"state\": \"STOP\"}");
        app_buzzer_beep(BUZZER_TIME_SHORT);
    }
    else if (strcmp(cmd, "UNLOCK") == 0) {
        g_lock_active = 0;
        app_mqtt_pub_status("{\"child_lock\": \"UNLOCKED\"}");
        app_buzzer_beep(BUZZER_TIME_SHORT);
    }
}

// ==============================================================================
// BUTTON EVENT HANDLER
// ==============================================================================
void app_door_controller_core_handle_button_event(btn_event_t event) {
    switch (event) {
        case BTN_EVENT_OPEN:       
            app_door_controller_core_execute_cmd_string("OPEN"); 
            break;
        case BTN_EVENT_CLOSE:      
            app_door_controller_core_execute_cmd_string("CLOSE"); 
            break;
        case BTN_EVENT_STOP:       
            app_door_controller_core_execute_cmd_string("STOP"); 
            break;
        case BTN_EVENT_LOCK_PRESS:
            if (g_lock_active) app_door_controller_core_execute_cmd_string("UNLOCK");
            else app_door_controller_core_execute_cmd_string("LOCK");
            break;
        // 1. Giữ nút STOP 10s -> Học hành trình
        case BTN_EVENT_LEARN_TRAVEL_TRIGGER:
            printf("[CORE] Button Travel Learn Triggered\r\n");
            app_door_controller_core_execute_cmd_string("LEARN_MODE_ON");
            break;

        // 2. Giữ nút RF 10s -> Học Remote
        case BTN_EVENT_LEARN_RF_TRIGGER:
            printf("[CORE] Button RF Learn Triggered\r\n");
            app_door_controller_core_execute_cmd_string("RF_LEARN_MODE");
            break;
            case BTN_EVENT_WIFI_RESET_TRIGGER:
            printf("[CORE] Wifi Reset Triggered!\r\n");
            
            g_is_wifi_config_mode = 1;
            app_relay_stop_all();

            // 1. Start Timer 3 phút
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
            
            // 2. Báo Main bật BLE lên
            // Ta tái sử dụng event APP_EVENT_WIFI_FATAL_ERROR hoặc tạo event mới APP_EVENT_WIFI_CONFIG_START
            // Ở đây tôi dùng event mới cho rõ nghĩa
            app_send_event(APP_EVENT_WIFI_CONFIG_START, NULL);

            app_buzzer_beep(BUZZER_TIME_LONG);
            break;
    }
}