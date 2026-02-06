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

// ==============================================================================
// BIẾN TOÀN CỤC (GLOBAL VARIABLES)
// ==============================================================================
static int g_lock_active = 0;           // Trạng thái khóa (1: Khóa, 0: Mở)
static int g_is_learning = 0;           // Trạng thái chế độ học (1: Đang học)
static uint32_t g_learn_start_tick = 0; // Mốc thời gian bắt đầu đo hành trình
static uint32_t g_travel_time_ms = DEFAULT_TRAVEL_TIME_MS; // Thời gian hành trình (Mặc định hoặc load từ Flash)

// Timer Handles
static TimerHandle_t g_travel_timer = NULL;        // Timer ngắt relay khi cửa chạy bình thường
static TimerHandle_t g_learn_timeout_timer = NULL; // Timer đếm lùi 3 phút chờ thao tác

// ==============================================================================
// TIMER CALLBACKS
// ==============================================================================

/**
 * @brief Callback chạy khi cửa chạy hết thời gian hành trình (Chế độ thường)
 * Tự động ngắt điện Relay để bảo vệ động cơ.
 */
static void auto_stop_callback(TimerHandle_t xTimer) {
    printf("[CORE] Auto Stop Triggered (Travel time reached)\r\n");
    app_relay_stop_all();                // Ngắt điện Relay
    app_mqtt_pub_status("STOPPED_AUTO"); // Báo về App
}

/**
 * @brief Callback chạy khi vào chế độ học quá lâu mà không thao tác (3 phút)
 * Tự động thoát chế độ học.
 */
static void learn_timeout_callback(TimerHandle_t xTimer) {
    printf("[CORE] Learning Mode TIMEOUT (Idle > 3 mins). Exiting...\r\n");
    
    // Thoát chế độ học
    g_is_learning = 0;
    g_learn_start_tick = 0;
    
    app_relay_stop_all(); // Dừng mọi thứ cho an toàn
    
    app_buzzer_beep(BUZZER_TIME_SHORT);   // Bíp báo lỗi
    app_mqtt_pub_status("LEARN_TIMEOUT"); // Báo lỗi về App
}

// ==============================================================================
// INIT FUNCTION
// ==============================================================================
void app_door_controller_core_init(void) {
    // 1. Khởi tạo Còi
    app_buzzer_init();

    g_lock_active = 0;
    
    // 2. Load thời gian đã học từ Flash
    uint32_t saved = 0;
    if (storage_get_travel_time(&saved)) {
        // Kiểm tra tính hợp lệ (tránh giá trị rác)
        if (saved > MIN_LEARN_TIME_MS && saved < MAX_SAFE_TIME_MS) {
            g_travel_time_ms = saved;
        }
    }
    printf("[CORE] Init Done. Current Travel Time: %d ms\r\n", (int)g_travel_time_ms);

    // 3. Tạo Timer Auto-Stop (Chưa Start)
    g_travel_timer = xTimerCreate("TravelTmr", 
                                  pdMS_TO_TICKS(g_travel_time_ms), 
                                  pdFALSE, // One-shot (Chạy 1 lần)
                                  (void *)0, 
                                  auto_stop_callback);

    // 4. Tạo Timer Timeout 3 phút (Chưa Start)
    // LEARN_MODE_TIMEOUT_MS được define trong app_conf.h
    g_learn_timeout_timer = xTimerCreate("LearnTimeout", 
                                         pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 
                                         pdFALSE, 
                                         (void *)0, 
                                         learn_timeout_callback);
}

// ==============================================================================
// CORE LOGIC EXECUTION
// ==============================================================================
void app_door_controller_core_execute_cmd_string(const char* cmd) {
    printf("[CORE] Executing CMD: %s\r\n", cmd);

    // --------------------------------------------------------------------------
    // LỆNH: LEARN_MODE_ON (Bật chế độ học)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "LEARN_MODE_ON") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("LOCKED_ERR");
            app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp báo lỗi
            return;
        }

        g_is_learning = 1;
        g_learn_start_tick = 0; // Reset mốc đo, chưa tính giờ ngay
        
        app_relay_stop_all();   // Dừng cửa để chuẩn bị
        
        // Bắt đầu đếm ngược 3 phút chờ người dùng bấm OPEN/CLOSE
        if (g_learn_timeout_timer != NULL) {
            xTimerReset(g_learn_timeout_timer, 0); 
        }

        app_mqtt_pub_status("LEARNING_WAIT_INPUT"); 
        app_buzzer_beep(BUZZER_TIME_LONG); // Bíp dài báo hiệu bắt đầu
        printf("[CORE] >>> ENTER LEARNING MODE. Waiting for input (Timeout 3min)...\r\n");
        return;
    }

    // --------------------------------------------------------------------------
    // LỆNH: STOP (Dừng & Lưu kết quả học)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "STOP") == 0) {
        // 1. Dừng vật lý ngay lập tức (Ưu tiên cao nhất)
        app_relay_stop_all();
        
        // 2. Dừng các Timer đang chạy ngầm
        if (xTimerIsTimerActive(g_travel_timer)) xTimerStop(g_travel_timer, 0);
        if (xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

        // 3. XỬ LÝ KHI ĐANG HỌC LỆNH
        if (g_is_learning) {
            if (g_learn_start_tick > 0) {
                // Đã chạy cửa -> Tính toán thời gian thực tế
                uint32_t now = xTaskGetTickCount();
                uint32_t duration = (now - g_learn_start_tick) * portTICK_PERIOD_MS;
                
                if (duration > MIN_LEARN_TIME_MS) {
                    g_travel_time_ms = duration;
                    printf("[LEARN] SUCCESS! New Travel Time: %d ms\r\n", (int)g_travel_time_ms);
                    
                    // Lưu vào Flash & Update Timer
                    storage_save_travel_time(g_travel_time_ms);
                    xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
                    
                    app_buzzer_beep(BUZZER_TIME_LONG); // Bíp dài: Thành công
                    app_mqtt_pub_status("LEARN_SUCCESS");
                } else {
                    printf("[LEARN] Duration too short (< 1s). Ignored.\r\n");
                    app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp ngắn: Lỗi
                    app_mqtt_pub_status("LEARN_FAILED_TOO_SHORT");
                }
            } else {
                // Người dùng bấm STOP mà chưa bấm OPEN/CLOSE lần nào
                printf("[LEARN] Cancelled by User (No movement).\r\n");
                app_buzzer_beep(BUZZER_TIME_SHORT);
                app_mqtt_pub_status("LEARN_CANCELLED");
            }
            
            // Dù thành công hay thất bại -> Thoát chế độ học
            g_is_learning = 0;
            g_learn_start_tick = 0;
        } else {
            // Chế độ thường
            app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp xác nhận dừng
            app_mqtt_pub_status("STOPPED");
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: OPEN
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "OPEN") == 0) {
        if (g_lock_active) {
            printf("[CORE] Blocked by LOCK\r\n");
            app_mqtt_pub_status("LOCKED_ERR");
            app_buzzer_beep(BUZZER_TIME_SHORT);
            return;
        }

        app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp xác nhận
        app_relay_hold_open();              // Kích hoạt Relay Giữ
        app_mqtt_pub_status("OPENING");

        if (g_is_learning) {
            // [CHẾ ĐỘ HỌC]
            // Chỉ bắt đầu tính giờ nếu đây là lần bấm đầu tiên
            if (g_learn_start_tick == 0) {
                // Hủy Timer chờ 3 phút vì người dùng đã thao tác
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
                
                // Bắt đầu bấm giờ
                g_learn_start_tick = xTaskGetTickCount();
                printf("[LEARN] Action detected! Start Measuring...\r\n");
            }
        } else {
            // [CHẾ ĐỘ THƯỜNG]
            // Reset và chạy Timer tự ngắt theo thời gian đã học
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: CLOSE
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "CLOSE") == 0) {
        if (g_lock_active) {
            printf("[CORE] Blocked by LOCK\r\n");
            app_mqtt_pub_status("LOCKED_ERR");
            app_buzzer_beep(BUZZER_TIME_SHORT);
            return;
        }

        app_buzzer_beep(BUZZER_TIME_SHORT); // Bíp xác nhận
        app_relay_hold_close();             // Kích hoạt Relay Giữ
        app_mqtt_pub_status("CLOSING");

        if (g_is_learning) {
            // [CHẾ ĐỘ HỌC]
            if (g_learn_start_tick == 0) {
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
                
                g_learn_start_tick = xTaskGetTickCount();
                printf("[LEARN] Action detected! Start Measuring...\r\n");
            }
        } else {
            // [CHẾ ĐỘ THƯỜNG]
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
    // --------------------------------------------------------------------------
    // LỆNH: LOCK / UNLOCK
    // --------------------------------------------------------------------------
    else if (strcmp(cmd, "LOCK") == 0) {
        g_lock_active = 1;
        app_relay_stop_all(); // Khóa thì phải dừng ngay
        app_mqtt_pub_status("LOCKED");
        app_buzzer_beep(BUZZER_TIME_SHORT);
    }
    else if (strcmp(cmd, "UNLOCK") == 0) {
        g_lock_active = 0;
        app_mqtt_pub_status("UNLOCKED");
        app_buzzer_beep(BUZZER_TIME_SHORT);
    }
}

// ==============================================================================
// BUTTON EVENT HANDLER (Mapping từ app_button.c)
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
        
        // Sự kiện nhấn giữ nút STOP > 10s (được map từ app_button.c)
        case BTN_EVENT_LEARN_MODE_TRIGGER:
            app_door_controller_core_execute_cmd_string("LEARN_MODE_ON");
            break;

        case BTN_EVENT_LOCK_PRESS:
            // Toggle khóa (Khóa <-> Mở)
            if (g_lock_active) app_door_controller_core_execute_cmd_string("UNLOCK");
            else app_door_controller_core_execute_cmd_string("LOCK");
            break;
    }
}