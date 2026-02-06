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
#include "app_events.h"    
#include "app_mqtt.h"
#include "app_conf.h"
#include "app_door_controller_core.h"
#include "app_button.h"
#include "app_led.h"
#include "app_output_relay.h"
#include "app_storage.h"
#include "app_watchdog.h"
#include "app_rf.h"
#include "app_http.h"
#include "app_buzzer.h" // <--- [1] THÊM MODULE BUZZER
#include "app_events.h" 

// --- BLE SDK ---
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"

// --- GLOBAL VARS ---
QueueHandle_t g_app_queue = NULL; 
static int g_wifi_retry_cnt = 0;
static int g_ble_active = 0;
static int g_wifi_inited = 0;
static int g_has_connected_once = 0;
static wifi_conf_t conf = { .country_code = "CN" };

// --- MACROS BLE ---
#define UUID_EXPAND(...)    BT_UUID_128_ENCODE(__VA_ARGS__)
#define UUID_SVC      BT_UUID_DECLARE_128(UUID_EXPAND(UUID_SVC_DEF))
#define UUID_CHR_RX   BT_UUID_DECLARE_128(UUID_EXPAND(UUID_RX_DEF))

// Forward Declarations
void enable_ble_adv(void);
void disable_ble_adv(void);

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
                    g_wifi_retry_cnt = 0;
                    
                    disable_ble_adv();
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
                        xTaskCreate((TaskFunction_t)app_http_register_device, "http", 3072, NULL, 10, NULL);
                    }
                    break;

                // -------------------------------------------------
                // CASE 2: WIFI LOST
                // -------------------------------------------------
                case APP_EVENT_WIFI_DISCONNECTED:
                    printf("[APP] Wifi Lost.\r\n");
                    led_set_mode(LED_BLINK_FAST_MODE);
                    
                    if (g_has_connected_once) {
                        // Router tắt -> Retry mãi mãi
                        printf("[APP] Retrying connection in 5s...\r\n");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        if(storage_get_wifi(wifi_ssid, wifi_pass)) {
                             wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), wifi_ssid, wifi_pass, NULL, NULL, 0, 0);
                        }
                    } else {
                        // Chưa từng kết nối được -> Sai pass?
                        g_wifi_retry_cnt++;
                        if (g_wifi_retry_cnt <= 5) { 
                             printf("[APP] Retry %d/5...\r\n", g_wifi_retry_cnt);
                             vTaskDelay(pdMS_TO_TICKS(2000));
                             if(storage_get_wifi(wifi_ssid, wifi_pass)) {
                                  wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), wifi_ssid, wifi_pass, NULL, NULL, 0, 0);
                             }
                        } else {
                             // Hết lượt -> Bật BLE Config
                             app_send_event(APP_EVENT_WIFI_FATAL_ERROR, NULL);
                        }
                    }
                    break;

                // -------------------------------------------------
                // CASE 3: FATAL ERROR / NO CONFIG -> BẬT BLE
                // -------------------------------------------------
                case APP_EVENT_WIFI_FATAL_ERROR:
                    // [3] SỬA LOG CHO THÂN THIỆN HƠN
                    printf("[APP] Setup Mode Active (No Wifi or Connect Failed) -> Enabling BLE.\r\n");
                    
                    wifi_mgmr_sta_disconnect();
                    enable_ble_adv(); 
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
                        // ============================================================
                        // 1. XỬ LÝ ĐIỀU KHIỂN CỬA (Key: "state")
                        // Payload: {"state": "OPEN"} | {"state": "CLOSE"} | {"state": "STOP"}
                        // Hỗ trợ thêm: {"state": "LOCK"} | {"state": "UNLOCK"} (Tiện ích)
                        // ============================================================
                        cJSON *state_item = cJSON_GetObjectItem(root, "state");
                        
                        if (state_item && state_item->type == cJSON_String && state_item->valuestring) {
                            char *val = state_item->valuestring;
                            
                            // Các lệnh hợp lệ gửi vào Core
                            if (strcmp(val, "OPEN") == 0 || 
                                strcmp(val, "CLOSE") == 0 || 
                                strcmp(val, "STOP") == 0 ||
                                strcmp(val, "LOCK") == 0 || 
                                strcmp(val, "UNLOCK") == 0) {
                                
                                app_door_controller_core_execute_cmd_string(val);
                            } 
                            else {
                                printf("[APP] Unknown state: %s\r\n", val);
                            }
                        }

                        // ============================================================
                        // 2. XỬ LÝ KHÓA TRẺ EM (Key chuẩn Z2M: "child_lock")
                        // Payload: {"child_lock": "LOCKED"} | {"child_lock": "UNLOCKED"}
                        // ============================================================
                        cJSON *lock_item = cJSON_GetObjectItem(root, "child_lock");
                        
                        if (lock_item && lock_item->type == cJSON_String && lock_item->valuestring) {
                            if (strcmp(lock_item->valuestring, "LOCKED") == 0) {
                                app_door_controller_core_execute_cmd_string("LOCK");
                            } 
                            else if (strcmp(lock_item->valuestring, "UNLOCKED") == 0) {
                                app_door_controller_core_execute_cmd_string("UNLOCK");
                            }
                        }
                
                        // ============================================================
                        // 3. XỬ LÝ CHẾ ĐỘ HỌC (Key: "calibration")
                        // Payload: {"calibration": "TRAVEL"} -> Học hành trình
                        // Payload: {"calibration": "RF"}     -> Học Remote
                        // ============================================================
                        cJSON *calib_item = cJSON_GetObjectItem(root, "calibration");
                        
                        if (calib_item && calib_item->type == cJSON_String && calib_item->valuestring) {
                            char *mode = calib_item->valuestring;

                            // A. Học hành trình (Thời gian đóng mở)
                            if (strcmp(mode, "ON") == 0 || strcmp(mode, "TRAVEL") == 0) {
                                app_door_controller_core_execute_cmd_string("LEARN_MODE_ON");
                            }
                            // B. Học tay khiển RF
                            else if (strcmp(mode, "RF") == 0) {
                                app_door_controller_core_execute_cmd_string("RF_LEARN_MODE");
                            }
                        }
                
                        cJSON_Delete(root); // Quan trọng: Giải phóng RAM
                    } 
                    else {
                        printf("[APP] JSON Parse Error\r\n");
                    }
                    break;
                case APP_EVENT_WIFI_CONFIG_START:
                    printf("[MAIN] Enter Wifi Config Mode (BLE ON).\r\n");
                    // Ngắt Wifi tạm thời (nếu muốn) hoặc cứ để chạy song song
                    // wifi_mgmr_sta_disconnect(); 
                    
                    enable_ble_adv(); // Bật BLE
                    led_set_mode(LED_BLINK_FAST_MODE);
                    break;

                case APP_EVENT_WIFI_CONFIG_TIMEOUT:
                    printf("[MAIN] Wifi Config Timeout -> BLE OFF.\r\n");
                    disable_ble_adv(); // Tắt BLE
                    
                    // Khôi phục trạng thái LED
                    if (g_has_connected_once) {
                        led_set_mode(LED_ON_MODE);
                    } else {
                        // Nếu chưa từng kết nối được thì lại thử kết nối lại
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
    // Code này ĐÃ TỐT. 
    // Vì app_button.c đã được sửa để bắn ra BTN_EVENT_LEARN_TRAVEL_TRIGGER
    // Và app_door_controller_core.c đã được sửa để hứng sự kiện đó.
    // Nên ở đây chỉ cần chuyển tiếp là xong.
    app_door_controller_core_handle_button_event(event);
}

// [HÀM MỚI] Cầu nối RF Raw -> Core Logic
// Hàm này chỉ có nhiệm vụ chuyển phát nhanh mã số vào Core
static void on_rf_code_received(uint32_t code, int pulse_width) {
    // 1. Log ra để debug xem remote có phát đúng không
    printf("[RF-BRIDGE] Raw Code: %06X - Pulse: %d\r\n", code, pulse_width);

    // 2. Đẩy thẳng mã thô vào Core
    // Core sẽ tự kiểm tra:
    // - Nếu đang học: Lưu mã này lại.
    // - Nếu đang chạy: So sánh mã này với mã đã lưu để mở/đóng cửa.
    app_door_core_handle_rf_raw(code);
}

// ============================================================================
// BLE IMPLEMENTATION (Giữ nguyên)
// ============================================================================
static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, u16_t len, u16_t offset, u8_t flags) {
    static char data[512]; 
    if(len > 511) len = 511;
    memcpy(data, buf, len);
    data[len] = '\0';

    printf("[BLE] Payload: %s\r\n", data);
    cJSON *root = cJSON_Parse(data);
    if (!root) return len;

    cJSON *wifi_ssid = cJSON_GetObjectItem(root, "wifi_ssid");
    cJSON *wifi_pass = cJSON_GetObjectItem(root, "wifi_pass");
    cJSON *mqtt_broker = cJSON_GetObjectItem(root, "mqtt_broker");
    cJSON *mqtt_username = cJSON_GetObjectItem(root, "mqtt_username");
    cJSON *mqtt_pass = cJSON_GetObjectItem(root, "mqtt_pass");
    cJSON *mqtt_token_device = cJSON_GetObjectItem(root, "mqtt_token_device");

    if (mqtt_broker && mqtt_token_device) {
        storage_save_mqtt_info(mqtt_broker->valuestring, 
                               mqtt_username ? mqtt_username->valuestring : "", 
                               mqtt_pass ? mqtt_pass->valuestring : "", 
                               mqtt_token_device->valuestring);
    }
    if (wifi_ssid && wifi_pass) {
        printf("[BLE] Saving Wifi & Rebooting...\r\n");
        app_buzzer_beep(BUZZER_TIME_LONG); // Kêu dài báo hiệu nhận cấu hình
        vTaskDelay(pdMS_TO_TICKS(500));
        storage_save_wifi_reboot(wifi_ssid->valuestring, wifi_pass->valuestring);
    }

    cJSON_Delete(root);
    return len;
}

static struct bt_gatt_attr config_attrs[]= {
    BT_GATT_PRIMARY_SERVICE(UUID_SVC),
    BT_GATT_CHARACTERISTIC(UUID_CHR_RX, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE, NULL, ble_write_cb, NULL),
};
static struct bt_gatt_service config_service = BT_GATT_SERVICE(config_attrs);
static const struct bt_data ad_config[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEV_NAME, 12), 
};

static void ble_init_cb(int err) {
    if (!err) bt_gatt_service_register(&config_service);
}

void enable_ble_adv() {
    if (g_ble_active) return;
    bt_le_adv_start(BT_LE_ADV_CONN, ad_config, ARRAY_SIZE(ad_config), NULL, 0);
    g_ble_active = 1;
    printf("[BLE] Advertising Started.\r\n");
}
void disable_ble_adv() {
    if (!g_ble_active) return;
    bt_le_adv_stop();
    g_ble_active = 0;
    printf("[BLE] Advertising Stopped.\r\n");
}

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
    app_buzzer_init(); // <--- [2] KHỞI TẠO CÒI Ở ĐÂY
    app_door_controller_core_init();
    
    // Gắn callback nút bấm
    app_button_init(on_button_event_bridge);

    // 2. KHỞI TẠO QUEUE
    g_app_queue = xQueueCreate(15, sizeof(app_msg_t));
    if (g_app_queue == NULL) printf("[ERR] Queue create failed!\r\n");

    // 3. TẠO TASK TRUNG TÂM
    xTaskCreate(app_process_task, "app_core", 3072, NULL, 12, NULL);

    // 4. KHỞI CHẠY RF
    app_rf_start_task(1024, 10, on_rf_code_received);

    // 5. INIT BLE
    ble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_enable(ble_init_cb);

    // 6. INIT WIFI
    printf("[BOOT] Starting Wifi Stack...\r\n");
    tcpip_init(NULL, NULL);
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    hal_wifi_start_firmware_task();
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
    
    // 7. Watchdog
    app_watchdog_init();

    vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    xTaskCreate(proc_main_entry, "main", 1024, NULL, 15, NULL);
}