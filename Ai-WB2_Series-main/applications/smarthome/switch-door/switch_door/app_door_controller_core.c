#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>

#include "app_conf.h"          // Cấu hình hệ thống
#include "app_door_controller_core.h"
#include "app_output_relay.h"  // Driver Relay (Mode Pulse)
#include "app_mqtt.h"          // MQTT
#include "app_storage.h"       // Lưu/Đọc Flash
#include "app_buzzer.h"        // Driver Còi
#include "app_events.h"        // Events
#include "app_ble.h"           // BLE
#include <bl_gpio.h>
#include <easyflash.h>

// [MỚI] Thêm thư viện Settings đã tách
#include "app_door_settings.h" 

#undef TAG
#define TAG "CORE"

// ==============================================================================
// ĐỊNH NGHĨA ENUM & STRUCT
// ==============================================================================

// Trạng thái logic chi tiết
typedef enum {
    DOOR_LOGIC_STOPPED = 0,
    DOOR_LOGIC_OPENING,
    DOOR_LOGIC_CLOSING,
    DOOR_LOGIC_OPENED,     
    DOOR_LOGIC_CLOSED      
} door_logic_state_t;

// Các bước học RF
typedef enum {
    RF_LEARN_NONE = 0,      
    RF_LEARN_WAIT_OPEN,     
    RF_LEARN_WAIT_STOP,     
    RF_LEARN_WAIT_CLOSE,    
    RF_LEARN_WAIT_LOCK,     
    RF_LEARN_DONE           
} rf_learn_step_t;

// ==============================================================================
// BIẾN TOÀN CỤC (GLOBAL VARIABLES)
// ==============================================================================

// --- QUẢN LÝ CỬA & HÀNH TRÌNH ---
static door_logic_state_t g_logic_state = DOOR_LOGIC_STOPPED;
static int g_lock_active = 0;           
static int g_is_travel_learning = 0;    
static uint32_t g_learn_start_tick = 0; 
static uint32_t g_travel_time_ms = DEFAULT_TRAVEL_TIME_MS; 

// --- QUẢN LÝ HỌC RF ---
static rf_learn_step_t g_rf_learn_step = RF_LEARN_NONE;
static uint32_t g_saved_rf_codes[4] = {0}; 
static uint32_t g_temp_rf_codes[4]  = {0}; 

// --- TIMERS ---
static TimerHandle_t g_travel_timer = NULL;        // Timer hành trình
static TimerHandle_t g_learn_timeout_timer = NULL; // Timer timeout học
// [ĐÃ XÓA] Timer Multi-click (đã chuyển sang app_door_settings)

// Trạng thái config wifi
static int g_is_wifi_config_mode = 0;
static int g_is_ble_control_mode = 0;

// ==============================================================================
// PROTOTYPES
// ==============================================================================
// Hàm thực thi hành động thật sự (Callback cho Settings module)
static void core_action_callback(const char* cmd); 

// ==============================================================================
// HÀM HỖ TRỢ (SYNC & SAVE)
// ==============================================================================

void app_door_sync_status(void) {
    const char* status_json = "{\"state\": \"STOP\"}";
    switch (g_logic_state) {
        case DOOR_LOGIC_OPENED:  status_json = "{\"state\": \"OPENED\"}"; break;
        case DOOR_LOGIC_CLOSED:  status_json = "{\"state\": \"CLOSED\"}"; break;
        case DOOR_LOGIC_OPENING: status_json = "{\"state\": \"OPENING\"}"; break;
        case DOOR_LOGIC_CLOSING: status_json = "{\"state\": \"CLOSING\"}"; break;
        default:                 status_json = "{\"state\": \"STOP\"}"; break;
    }

    if (app_mqtt_is_connected()) {
        app_mqtt_pub_status(status_json);
    } else {
        app_ble_set_adv_data(status_json); 
    }
}

void save_logic_state(door_logic_state_t state) {
    g_logic_state = state;
    char val[2];
    snprintf(val, sizeof(val), "%d", (int)state);
    ef_set_env("last_door_state", val);
    ef_save_env();
}

// Wrapper cập nhật cấu hình (Gọi sang module Settings)
void app_door_core_update_settings(int open, int close, int def_open, int def_close, int mode, int start, int end) {
    // Chuyển tiếp sang app_door_settings xử lý
    door_settings_update_from_mqtt(open, close, def_open, def_close, mode, start, end);
}

// ==============================================================================
// TIMER CALLBACKS
// ==============================================================================

static void auto_stop_callback(TimerHandle_t xTimer) {
    printf("[CORE] Auto Stop Triggered\r\n");
    app_relay_pulse(GPIO_OUT_STOP);
    if (g_logic_state == DOOR_LOGIC_OPENING) save_logic_state(DOOR_LOGIC_OPENED);
    else if (g_logic_state == DOOR_LOGIC_CLOSING) save_logic_state(DOOR_LOGIC_CLOSED);
    app_door_sync_status();
}

static void learn_timeout_callback(TimerHandle_t xTimer) {
    printf("[CORE] Learn Timeout.\r\n");
    // 1. Timeout của Wifi Config (Reset)
    if (g_is_wifi_config_mode) {
        g_is_wifi_config_mode = 0;
        app_send_event(APP_EVENT_WIFI_CONFIG_TIMEOUT, NULL);
    }

    // 2. Timeout của BLE Control (Bật qua MQTT) - [THÊM ĐOẠN NÀY]
    if (g_is_ble_control_mode) {
        g_is_ble_control_mode = 0;
        printf("[CORE] BLE Control Timeout. Auto turning off BLE.\r\n");
        app_send_event(APP_EVENT_BLE_CONTROL_STOP, NULL); // Gửi lệnh tắt BLE nhẹ nhàng
        app_mqtt_pub_status("{\"ble\": \"OFF\"}");
    }
    if (g_is_wifi_config_mode) { 
        g_is_wifi_config_mode = 0; 
        app_send_event(APP_EVENT_WIFI_CONFIG_TIMEOUT, NULL); 
    }
    if (g_rf_learn_step != RF_LEARN_NONE) { 
        g_rf_learn_step = RF_LEARN_NONE; 
        app_mqtt_pub_status("{\"rf_learn\": \"TIMEOUT\"}"); 
    }
    if (g_is_travel_learning) { 
        g_is_travel_learning = 0; 
        g_learn_start_tick = 0; 
        app_mqtt_pub_status("{\"calibration\": \"TIMEOUT\"}"); 
    }
    app_relay_pulse(GPIO_OUT_STOP);
    app_buzzer_beep(BUZZER_TIME_SHORT);
    app_door_sync_status();
}

// ==============================================================================
// INIT FUNCTION
// ==============================================================================
void app_door_controller_core_init(void) {
    app_buzzer_init();
    g_lock_active = 0;
    
    // 1. Load Last State
    char *saved_s = ef_get_env("last_door_state");
    if (saved_s) g_logic_state = (door_logic_state_t)atoi(saved_s);

    // 2. Load Travel Time
    uint32_t saved_time = 0;
    if (storage_get_travel_time(&saved_time)) {
        if (saved_time > MIN_LEARN_TIME_MS && saved_time < MAX_SAFE_TIME_MS) g_travel_time_ms = saved_time;
    }
    printf("[CORE] Travel Time: %d ms\r\n", (int)g_travel_time_ms);

    // 3. Load RF
    storage_get_rf_codes(g_saved_rf_codes);

    // 4. [MỚI] Init Settings Module (Load cấu hình Click/Time)
    // Truyền hàm callback 'core_action_callback' để Settings gọi khi đủ click
    door_settings_init(core_action_callback);
    door_settings_load();

    // 5. Init Timers
    g_travel_timer = xTimerCreate("TravelTmr", pdMS_TO_TICKS(g_travel_time_ms), pdFALSE, (void *)0, auto_stop_callback);
    g_learn_timeout_timer = xTimerCreate("LearnTimeout", pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), pdFALSE, (void *)0, learn_timeout_callback);
    
    app_mqtt_on_connect(app_door_sync_status);
}

// ==============================================================================
// RF HANDLER
// ==============================================================================
void app_door_core_handle_rf_raw(uint32_t rf_code) {
    // A. CHẾ ĐỘ HỌC RF
    if (g_rf_learn_step != RF_LEARN_NONE) {
        printf("[RF-LEARN] Step: %d - Code: %06X\r\n", g_rf_learn_step, rf_code);
        app_buzzer_beep(BUZZER_TIME_SHORT); 
        switch (g_rf_learn_step) {
            case RF_LEARN_WAIT_OPEN: 
                g_temp_rf_codes[0] = rf_code; g_rf_learn_step = RF_LEARN_WAIT_STOP; 
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_STOP\"}"); break;
            case RF_LEARN_WAIT_STOP: 
                g_temp_rf_codes[1] = rf_code; g_rf_learn_step = RF_LEARN_WAIT_CLOSE; 
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_CLOSE\"}"); break;
            case RF_LEARN_WAIT_CLOSE: 
                g_temp_rf_codes[2] = rf_code; g_rf_learn_step = RF_LEARN_WAIT_LOCK; 
                app_mqtt_pub_status("{\"rf_learn\": \"WAIT_LOCK\"}"); break;
            case RF_LEARN_WAIT_LOCK: 
                g_temp_rf_codes[3] = rf_code; 
                memcpy(g_saved_rf_codes, g_temp_rf_codes, sizeof(g_saved_rf_codes)); 
                storage_save_rf_codes(g_saved_rf_codes);
                g_rf_learn_step = RF_LEARN_NONE; 
                if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
                app_mqtt_pub_status("{\"rf_learn\": \"DONE\"}"); 
                app_buzzer_beep(BUZZER_TIME_LONG); break;
            default: break;
        }
        if (g_rf_learn_step != RF_LEARN_NONE) {
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(RF_LEARN_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
        }
        return;
    }

    // B. CHẾ ĐỘ THƯỜNG
    // Gọi execute_cmd_string để áp dụng logic Multi-Click cho cả RF
    if (rf_code == g_saved_rf_codes[0]) app_door_controller_core_execute_cmd_string("OPEN");
    else if (rf_code == g_saved_rf_codes[1]) app_door_controller_core_execute_cmd_string("STOP");
    else if (rf_code == g_saved_rf_codes[2]) app_door_controller_core_execute_cmd_string("CLOSE");
    else if (rf_code == g_saved_rf_codes[3]) {
        if (g_lock_active) app_door_controller_core_execute_cmd_string("UNLOCK");
        else app_door_controller_core_execute_cmd_string("LOCK");
    }
}

// ==============================================================================
// INTERNAL EXECUTE (Thực thi thật sự - Callback từ Settings Module)
// ==============================================================================
static void core_action_callback(const char* cmd) {
    // --- OPEN ---
    if (strcmp(cmd, "OPEN") == 0) {
        if (g_lock_active) { app_mqtt_pub_status("{\"error\": \"LOCKED\"}"); app_buzzer_beep(BUZZER_TIME_SHORT); return; }
        
        app_buzzer_beep(BUZZER_TIME_SHORT); 
        app_relay_pulse(GPIO_OUT_OPEN); 
        
        g_logic_state = DOOR_LOGIC_OPENING; 
        app_door_sync_status();

        if (g_is_travel_learning) {
            g_learn_start_tick = xTaskGetTickCount();
            if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
        } else {
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
    // --- CLOSE ---
    else if (strcmp(cmd, "CLOSE") == 0) {
        if (g_lock_active) { app_mqtt_pub_status("{\"error\": \"LOCKED\"}"); app_buzzer_beep(BUZZER_TIME_SHORT); return; }
        
        app_buzzer_beep(BUZZER_TIME_SHORT); 
        app_relay_pulse(GPIO_OUT_CLOSE); 
        
        g_logic_state = DOOR_LOGIC_CLOSING; 
        app_door_sync_status();

        if (g_is_travel_learning) {
            g_learn_start_tick = xTaskGetTickCount();
            if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
        } else {
            xTimerChangePeriod(g_travel_timer, pdMS_TO_TICKS(g_travel_time_ms), 0);
            xTimerStart(g_travel_timer, 0);
        }
    }
}

// ==============================================================================
// PUBLIC EXECUTE (Nhận lệnh -> Đẩy qua Settings để lọc)
// ==============================================================================
void app_door_controller_core_execute_cmd_string(const char* cmd) {
    printf("[CORE] Receive CMD: %s\r\n", cmd);

    // 1. LỆNH ƯU TIÊN (STOP) -> Reset bộ đếm bên Settings
    if (strcmp(cmd, "STOP") == 0) {
        // [MỚI] Gọi Settings reset đếm
        door_settings_reset_click();
        
        // Hủy học RF
        if (g_rf_learn_step != RF_LEARN_NONE) { 
            g_rf_learn_step = RF_LEARN_NONE; 
            if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0); 
            app_mqtt_pub_status("{\"rf_learn\": \"CANCELLED\"}"); 
            app_buzzer_beep(BUZZER_TIME_SHORT); 
            return; 
        }

        app_relay_pulse(GPIO_OUT_STOP);

        if (xTimerIsTimerActive(g_travel_timer)) xTimerStop(g_travel_timer, 0);
        if (xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

        if (g_is_travel_learning) {
             if (g_learn_start_tick > 0) {
                 uint32_t now = xTaskGetTickCount(); 
                 uint32_t duration = (now - g_learn_start_tick) * portTICK_PERIOD_MS;
                 
                 if (duration > MIN_LEARN_TIME_MS) { 
                     g_travel_time_ms = duration; 
                     storage_save_travel_time(g_travel_time_ms);
                     g_logic_state = DOOR_LOGIC_STOPPED; 
                     save_logic_state(DOOR_LOGIC_STOPPED); 
                     app_buzzer_beep(BUZZER_TIME_LONG); 
                     app_mqtt_pub_status("{\"calibration\": \"DONE\", \"state\": \"STOP\"}"); 
                 }
                 else { 
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
             g_logic_state = DOOR_LOGIC_STOPPED; 
             save_logic_state(DOOR_LOGIC_STOPPED); 
             app_door_sync_status();
        }
        return;
    }
    
    // 2. LỆNH CÀI ĐẶT -> Chạy ngay
    if (strcmp(cmd, "LOCK") == 0) { 
        g_lock_active = 1; app_relay_pulse(GPIO_OUT_STOP); 
        app_mqtt_pub_status("{\"child_lock\": \"LOCKED\", \"state\": \"STOP\"}"); 
        app_buzzer_beep(BUZZER_TIME_SHORT); return; 
    }
    if (strcmp(cmd, "UNLOCK") == 0) { 
        g_lock_active = 0; app_mqtt_pub_status("{\"child_lock\": \"UNLOCKED\"}"); 
        app_buzzer_beep(BUZZER_TIME_SHORT); return; 
    }
    if (strcmp(cmd, "LEARN_MODE_ON") == 0) { 
        if (g_lock_active) { app_mqtt_pub_status("{\"error\": \"LOCKED\"}"); return;} 
        g_is_travel_learning = 1; g_learn_start_tick = 0; 
        app_relay_pulse(GPIO_OUT_STOP); 
        xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0); 
        xTimerStart(g_learn_timeout_timer, 0); 
        app_mqtt_pub_status("{\"calibration\": \"ON\", \"state\": \"STOP\"}"); 
        app_buzzer_beep(BUZZER_TIME_LONG); return; 
    }
    if (strcmp(cmd, "RF_LEARN_MODE") == 0) { 
        if (g_lock_active) { app_mqtt_pub_status("{\"error\": \"LOCKED\"}"); return;} 
        g_rf_learn_step = RF_LEARN_WAIT_OPEN; 
        app_relay_pulse(GPIO_OUT_STOP); 
        xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(RF_LEARN_TIMEOUT_MS), 0); 
        xTimerStart(g_learn_timeout_timer, 0); 
        app_mqtt_pub_status("{\"rf_learn\": \"WAIT_OPEN\"}"); 
        app_buzzer_beep(BUZZER_TIME_LONG); return; 
    }

    // 3. LỆNH OPEN / CLOSE -> GỬI SANG MODULE SETTINGS
    if (strcmp(cmd, "OPEN") == 0 || strcmp(cmd, "CLOSE") == 0) {
        // Hỏi Settings: "Tôi chạy được chưa?"
        // 1: Chạy ngay (Do config = 1 hoặc đã đủ click)
        // 0: Chờ tí (Đang đếm timer)
        if (door_settings_process_cmd(cmd) == 1) {
             core_action_callback(cmd);
        }
    }
    // --------------------------------------------------------------------------
    // [MỚI] LỆNH: BLE_START (Bật BLE Advertising)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "BLE_START") == 0) {
        printf("[CORE] Enabling BLE Control Mode via MQTT...\r\n");

        if (strcmp(cmd, "BLE_START") == 0) {
        printf("[CORE] BLE ON.\r\n");
        
        // Gửi event để Main bật BLE
        app_send_event(APP_EVENT_BLE_CONTROL_START, NULL); 

        // Tắt Timer Timeout cũ nếu có (để không bị tắt ngang xương sau 3 phút)
        if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

        app_mqtt_pub_status("{\"ble\": \"ON\"}");
        app_buzzer_beep(BUZZER_TIME_SHORT);
        return;
    }
    }

    // --------------------------------------------------------------------------
    // [MỚI] LỆNH: BLE_STOP (Tắt BLE ngay lập tức)
    // --------------------------------------------------------------------------
    if (strcmp(cmd, "BLE_STOP") == 0) {
        printf("[CORE] Disabling BLE Control Mode...\r\n");
        
        // 1. Tắt BLE
        app_send_event(APP_EVENT_BLE_CONTROL_STOP, NULL); 

        // 2. Dừng Timer Timeout
        if(xTimerIsTimerActive(g_learn_timeout_timer)) xTimerStop(g_learn_timeout_timer, 0);

        // 3. Phản hồi
        app_mqtt_pub_status("{\"ble\": \"OFF\"}");
        app_buzzer_beep(BUZZER_TIME_SHORT);
        return;
    }
}
// ==============================================================================
// BUTTON EVENT HANDLER (Hàm này đang bị thiếu gây lỗi Linker)
// ==============================================================================
void app_door_controller_core_handle_button_event(btn_event_t event) {
    printf("[CORE] Button Event: %d\r\n", event);
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
            
        // 3. Combo Reset Wifi
        case BTN_EVENT_WIFI_RESET_TRIGGER:
            printf("[CORE] Wifi Reset Triggered!\r\n");
            
            g_is_wifi_config_mode = 1;
            app_relay_pulse(GPIO_OUT_STOP);

            // Start Timer 3 phút để timeout nếu ko ai config
            xTimerChangePeriod(g_learn_timeout_timer, pdMS_TO_TICKS(LEARN_MODE_TIMEOUT_MS), 0);
            xTimerStart(g_learn_timeout_timer, 0);
            
            // Báo Main bật BLE lên
            app_send_event(APP_EVENT_WIFI_CONFIG_START, NULL);

            app_buzzer_beep(BUZZER_TIME_LONG);
            break;
            
        default:
            break;
    }
}