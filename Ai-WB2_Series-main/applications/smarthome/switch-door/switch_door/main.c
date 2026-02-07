#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// --- SYSTEM & NETWORK ---
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <bl_sys.h>
#include <blog.h>
#include <lwip/tcpip.h>
#include <lwip/apps/sntp.h> 
#include <hal_wifi.h>
#include <wifi_mgmr_ext.h>

// --- MODULES ---
#include <cJSON.h>

#include "app_ble.h" // [QUAN TRỌNG] Đã tách BLE ra file riêng
#include "app_button.h"
#include "app_buzzer.h"
#include "app_conf.h"
#include "app_door_controller_core.h"
#include "app_events.h"
#include "app_http.h"
#include "app_led.h"
#include "app_mqtt.h"
#include "app_output_relay.h"
#include "app_rf.h"
#include "app_storage.h"
#include "app_watchdog.h"

// --- GLOBAL VARS ---
QueueHandle_t g_app_queue = NULL; 
static int g_wifi_retry_cnt = 0;
static int g_wifi_inited = 0;
static int g_has_connected_once = 0;
static wifi_conf_t conf = { .country_code = "CN" };
static TimerHandle_t wifi_reconnect_timer = NULL;

// ============================================================

// ============================================================================
// HÀM WRAPPER ĐỂ SỬA LỖI CASTING TASK HTTP
// ============================================================================
static void http_task_wrapper(void *pvParameters) {
    app_http_register_device();
    vTaskDelete(NULL);
}

// ============================================================================
// [APP TASK] BỘ NÃO TRUNG TÂM
// ============================================================================
static void app_process_task(void *pvParameters) {
    app_msg_t msg;
    char wifi_ssid[33], wifi_pass[64];

    while (1) {
        if (xQueueReceive(g_app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            switch (msg.type) {
                // -------------------------------------------------
                // CASE 1: WIFI CONNECTED
                // -------------------------------------------------
                case APP_EVENT_WIFI_CONNECTED:
                    printf("\r\n[APP] >>> EVENT: WIFI CONNECTED <<<\r\n");
                    g_has_connected_once = 1;
                    g_wifi_retry_cnt = 0; // Reset đếm lỗi
                    
                    // [QUAN TRỌNG] Có Wifi rồi -> Tắt BLE đi
                    // (Lệnh này chỉ chạy khi mạng vừa khôi phục. Nếu đang có mạng mà bật MQTT thì không ảnh hưởng)
                    app_door_controller_core_execute_cmd_string("BLE_STOP");
                    
                    // [QUAN TRỌNG] Tắt Timer quét 5 phút (nếu đang chạy)
                    if (wifi_reconnect_timer && xTimerIsTimerActive(wifi_reconnect_timer)) {
                        xTimerStop(wifi_reconnect_timer, 0);
                    }

                    led_set_mode(LED_ON_MODE);
                    
                    // Kích hoạt SNTP
                    printf("[TIME] Syncing SNTP...\r\n");
                    sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    sntp_setservername(0, "pool.ntp.org");
                    sntp_init();
                    
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    if (storage_has_mqtt_config()) {
                        printf("[APP] MQTT Config found. Starting MQTT...\r\n");
                        app_mqtt_start(); 
                    } else {
                        printf("[APP] No MQTT Config. Starting HTTP Task...\r\n");
                        xTaskCreate(http_task_wrapper, "http", 4096, NULL, 10, NULL);
                    }
                    break;

                // -------------------------------------------------
                // CASE 2: WIFI LOST
                // -------------------------------------------------
                case APP_EVENT_WIFI_DISCONNECTED:
                    printf("[APP] Wifi Lost.\r\n");
                    led_set_mode(LED_BLINK_FAST_MODE);
                    
                    // TRƯỜNG HỢP A: Đã từng kết nối (Router bị rút điện / Mất mạng đột ngột)
                    if (g_has_connected_once) {
                        g_wifi_retry_cnt++;
                        
                        // 5 Lần đầu: Thử kết nối lại liên tục (cách nhau 5s)
                        if (g_wifi_retry_cnt <= 5) {
                            printf("[APP] Lost connection. Retry %d/5 in 5s...\r\n", g_wifi_retry_cnt);
                            vTaskDelay(pdMS_TO_TICKS(5000));
                            if(storage_get_wifi(wifi_ssid, wifi_pass)) {
                                 wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), wifi_ssid, wifi_pass, NULL, NULL, 0, 0);
                            }
                        } 
                        // Sau 5 lần thất bại: Bật BLE dự phòng & Chuyển sang chế độ chờ 5 phút
                        else {
                            printf("[APP] Wifi fatal (5 retries failed). ENABLE BLE & Start 5-min Timer.\r\n");
                            
                            // 1. Bật BLE ngay lập tức để cứu hộ
                            app_door_controller_core_execute_cmd_string("BLE_START");
                            
                            // 2. Kích hoạt Timer 5 phút để thử lại sau
                            // (Timer này cần được tạo ở main_init: wifi_reconnect_timer)
                            if (wifi_reconnect_timer && !xTimerIsTimerActive(wifi_reconnect_timer)) {
                                xTimerStart(wifi_reconnect_timer, 0);
                            }
                        }
                    } 
                    // TRƯỜNG HỢP B: Chưa từng kết nối (Cấu hình sai Pass hoặc lần đầu)
                    else {
                        g_wifi_retry_cnt++;
                        if (g_wifi_retry_cnt <= 5) { 
                             printf("[APP] First connect retry %d/5...\r\n", g_wifi_retry_cnt);
                             vTaskDelay(pdMS_TO_TICKS(2000));
                             if(storage_get_wifi(wifi_ssid, wifi_pass)) {
                                  wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), wifi_ssid, wifi_pass, NULL, NULL, 0, 0);
                             }
                        } else {
                             // Hết lượt -> Vào chế độ Config lại từ đầu
                             printf("[APP] Cannot connect. Enter Config Mode.\r\n");
                             app_send_event(APP_EVENT_WIFI_FATAL_ERROR, NULL);
                        }
                    }
                    break;
                // -------------------------------------------------
                // CASE 3: FATAL ERROR / NO CONFIG -> BẬT BLE
                // -------------------------------------------------
                case APP_EVENT_WIFI_FATAL_ERROR:
                    printf("[APP] Setup Mode Active (No Wifi or Connect Failed) -> Enabling BLE.\r\n");
                    
                    wifi_mgmr_sta_disconnect();
                    
                    // [SỬA] Gọi hàm từ app_ble.h
                    app_ble_start_adv();
                     
                    led_set_mode(LED_BLINK_FAST_MODE);
                    g_wifi_retry_cnt = 0;
                    
                    // Có thể kêu tít tít báo hiệu vào chế độ cài đặt
                    app_buzzer_beep(BUZZER_TIME_SHORT);
                    break;

                // -------------------------------------------------
                // CASE 4: NHẬN LỆNH TỪ MQTT
                // -------------------------------------------------
                case APP_EVENT_MQTT_DATA_RX:
                    printf("[APP] MQTT Payload: %s\r\n", msg.data);
                    
                    cJSON *root = cJSON_Parse(msg.data);
                    if (root) {
                        // 1. XỬ LÝ ĐIỀU KHIỂN CỬA (Key: "state")
                        cJSON *state_item = cJSON_GetObjectItem(root, "state");
                        if (state_item && state_item->type == cJSON_String && state_item->valuestring) {
                            char *val = state_item->valuestring;
                            if (strcmp(val, "OPEN") == 0 || strcmp(val, "CLOSE") == 0 || 
                                strcmp(val, "STOP") == 0 || strcmp(val, "LOCK") == 0 || 
                                strcmp(val, "UNLOCK") == 0) {
                                app_door_controller_core_execute_cmd_string(val);
                            } 
                        }

                        // 2. XỬ LÝ KHÓA TRẺ EM (Key: "child_lock")
                        cJSON *lock_item = cJSON_GetObjectItem(root, "child_lock");
                        if (lock_item && lock_item->type == cJSON_String && lock_item->valuestring) {
                            if (strcmp(lock_item->valuestring, "LOCKED") == 0) {
                                app_door_controller_core_execute_cmd_string("LOCK");
                            } 
                            else if (strcmp(lock_item->valuestring, "UNLOCKED") == 0) {
                                app_door_controller_core_execute_cmd_string("UNLOCK");
                            }
                        }
                
                        // 3. XỬ LÝ CHẾ ĐỘ HỌC (Key: "calibration")
                        cJSON *calib_item = cJSON_GetObjectItem(root, "calibration");
                        if (calib_item && calib_item->type == cJSON_String && calib_item->valuestring) {
                            char *mode = calib_item->valuestring;
                            if (strcmp(mode, "ON") == 0 || strcmp(mode, "TRAVEL") == 0) {
                                app_door_controller_core_execute_cmd_string("LEARN_MODE_ON");
                            }
                            else if (strcmp(mode, "RF") == 0) {
                                app_door_controller_core_execute_cmd_string("RF_LEARN_MODE");
                            }
                        }
                        cJSON *ble_item = cJSON_GetObjectItem(root, "ble");
                        if (ble_item && ble_item->type == cJSON_String && ble_item->valuestring) {
                            if (strcmp(ble_item->valuestring, "ON") == 0) {
                                app_door_controller_core_execute_cmd_string("BLE_START");
                            } 
                            else if (strcmp(ble_item->valuestring, "OFF") == 0) {
                                app_door_controller_core_execute_cmd_string("BLE_STOP");
                            }
                        }

                        cJSON_Delete(root);
                    } 
                    break;

                // [THÊM MỚI] Xử lý bật BLE Control (Wifi vẫn sống)
                case APP_EVENT_BLE_CONTROL_START:
                    printf("[MAIN] Enable BLE Control (Wifi kept alive).\r\n");
                    app_ble_start_adv(); 
                    // KHÔNG đổi chế độ LED, hoặc chỉ set nháy cực chậm nếu muốn
                    // led_set_mode(LED_BLINK_SLOW_MODE); 
                    break;

                // [THÊM MỚI] Tắt BLE
                case APP_EVENT_BLE_CONTROL_STOP:
                    printf("[MAIN] Disable BLE Control.\r\n");
                    app_ble_stop_adv();
                    // Nếu cần, set lại LED theo trạng thái Wifi hiện tại
                    if (app_mqtt_is_connected()) {
                        led_set_mode(LED_ON_MODE); // Sáng đứng nếu đang có mạng
                    }
                    break;
                case APP_EVENT_WIFI_CONFIG_TIMEOUT:
                    printf("[MAIN] Wifi Config Timeout -> BLE OFF.\r\n");
                    // [SỬA] Gọi hàm từ app_ble.h
                    app_ble_stop_adv(); 
                    
                    // Khôi phục trạng thái LED
                    if (g_has_connected_once) {
                        led_set_mode(LED_ON_MODE);
                    } else {
                        app_send_event(APP_EVENT_WIFI_DISCONNECTED, NULL);
                    }
                    break;
                default:
                     break;
            }
        }
    }
}

// ============================================================================
// CÁC HÀM CẦU NỐI (BRIDGE)
// ============================================================================

// Cầu nối Nút bấm -> Core Logic
static void on_button_event_bridge(btn_event_t event) {
    app_door_controller_core_handle_button_event(event);
}

// Cầu nối RF Raw -> Core Logic
// [SỬA] Đảm bảo signature khớp với app_rf.h: (uint32_t, int)
static void on_rf_code_received(uint32_t code, int pulse_width) {
    printf("[RF-BRIDGE] Raw Code: %06X - Pulse: %d\r\n", code, pulse_width);
    app_door_core_handle_rf_raw(code);
}

// ============================================================================
// BLE IMPLEMENTATION -> ĐÃ CHUYỂN SANG app_ble.c
// ============================================================================
// (Đã xóa toàn bộ phần ble_write_cb, config_attrs, enable_ble_adv cũ ở đây)

// ============================================================================
// WIFI CALLBACK 
// ============================================================================
static void wifi_event_cb(input_event_t* event, void* private_data) {
    char wifi_ssid[33], wifi_pass[64];
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_start_background(&conf);
            g_wifi_inited = 1;
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            if (!storage_get_wifi(wifi_ssid, wifi_pass)) {
                // Không có wifi -> Vào mode cài đặt (Không phải lỗi nghiêm trọng)
                printf("[BOOT] No Wifi Config -> Enter Setup Mode.\r\n");
                app_send_event(APP_EVENT_WIFI_FATAL_ERROR, NULL);
            } else {
                printf("[BOOT] Connecting Wifi: %s\r\n", wifi_ssid);
                wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), wifi_ssid, wifi_pass, NULL, NULL, 0, 0);
            }
            break;

        case CODE_WIFI_ON_GOT_IP:
            app_send_event(APP_EVENT_WIFI_CONNECTED, NULL);
            break;

        case CODE_WIFI_ON_DISCONNECT:
            if(g_wifi_inited) {
                app_send_event(APP_EVENT_WIFI_DISCONNECTED, NULL);
            }
            break;
    }
}

// ============================================================================
// MAIN ENTRY
// ============================================================================
static void proc_main_entry(void *pvParameters)
{
    // 1. Init Modules cơ bản
    storage_init(); 
    led_init();
    app_relay_init();
    app_buzzer_init(); 
    
    // [SỬA] Khởi tạo BLE Stack từ module ngoài
    app_ble_init();

    app_door_controller_core_init();

    printf("[INIT] app_button_init...\r\n");
    // Gắn callback nút bấm
    app_button_init(on_button_event_bridge);

    // 2. KHỞI TẠO QUEUE
    g_app_queue = xQueueCreate(15, sizeof(app_msg_t));
    if (g_app_queue == NULL) printf("[ERR] Queue create failed!\r\n");

    // 3. TẠO TASK TRUNG TÂM
    xTaskCreate(app_process_task, "app_core", 3072, NULL, 12, NULL);

    // 4. KHỞI CHẠY RF
    // [SỬA] Signature của on_rf_code_received giờ đã khớp
    app_rf_start_task(1024, 10, on_rf_code_received);

    // 5. INIT WIFI
    printf("[BOOT] Starting Wifi Stack...\r\n");
    tcpip_init(NULL, NULL);
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    hal_wifi_start_firmware_task();
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
    
    // 6. Watchdog
    app_watchdog_init();

    vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    xTaskCreate(proc_main_entry, "main", 1024, NULL, 15, NULL);
}
