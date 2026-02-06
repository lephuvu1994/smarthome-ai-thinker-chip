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
#include <lwip/apps/sntp.h> // Đồng bộ thời gian
#include <hal_wifi.h>
#include <wifi_mgmr_ext.h>

// --- MODULES ---
#include <cJSON.h>
#include "app_events.h"    // Queue Definition
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

// --- BLE SDK ---
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"

// --- GLOBAL VARS ---
QueueHandle_t g_app_queue = NULL; // Queue chính
static int g_wifi_retry_cnt = 0;
static int g_ble_active = 0;
static int g_wifi_inited = 0;
static int g_has_connected_once = 0;
static wifi_conf_t conf = { .country_code = "CN" };

// --- MACROS BLE ---
#define BLE_DEV_NAME "SmartDoor_V3"
#define UUID_EXPAND(...)    BT_UUID_128_ENCODE(__VA_ARGS__)
#define UUID_SVC      BT_UUID_DECLARE_128(UUID_EXPAND(UUID_SVC_DEF))
#define UUID_CHR_RX   BT_UUID_DECLARE_128(UUID_EXPAND(UUID_RX_DEF))

// Forward Declarations
void enable_ble_adv(void);
void disable_ble_adv(void);

// ============================================================================
// [APP TASK] BỘ NÃO TRUNG TÂM
// Mọi logic quan trọng đều xử lý ở đây để tránh xung đột
// ============================================================================
static void app_process_task(void *pvParameters) {
    app_msg_t msg;
    char ssid[33], pass[64];

    while (1) {
        // Block chờ tin nhắn (Tiết kiệm CPU)
        if (xQueueReceive(g_app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            switch (msg.type) {
                // -------------------------------------------------
                // CASE 1: WIFI CONNECTED
                // -------------------------------------------------
                case APP_EVENT_WIFI_CONNECTED:
                    printf("\r\n[APP] >>> EVENT: WIFI CONNECTED <<<\r\n");
                    g_has_connected_once = 1;
                    g_wifi_retry_cnt = 0;
                    
                    // Tắt BLE nếu đang bật
                    disable_ble_adv();
                    led_set_mode(LED_ON_MODE);
                    
                    // Kích hoạt SNTP (Lấy giờ chuẩn) - QUAN TRỌNG CHO SSL
                    printf("[TIME] Syncing SNTP...\r\n");
                    sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    sntp_setservername(0, "pool.ntp.org");
                    sntp_init();
                    
                    // Delay 2s chờ giờ cập nhật
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    // Quyết định: Chạy MQTT hay Đăng ký HTTP
                    if (storage_has_mqtt_config()) {
                        printf("[APP] Starting MQTT...\r\n");
                        app_mqtt_start(); // Hàm này nằm bên app_mqtt.c
                    } else {
                        printf("[APP] No MQTT Config. Starting HTTP Task...\r\n");
                        // [FIX MEMORY] Giảm Stack HTTP xuống 3072 (đủ cho JSON)
                        // Giúp tiết kiệm 1KB RAM cho SSL
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
                        if(storage_get_wifi(ssid, pass)) {
                             wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), ssid, pass, NULL, NULL, 0, 0);
                        }
                    } else {
                        // Chưa từng kết nối được -> Sai pass?
                        g_wifi_retry_cnt++;
                        if (g_wifi_retry_cnt <= 5) { // Thử 5 lần
                             printf("[APP] Retry %d/5...\r\n", g_wifi_retry_cnt);
                             vTaskDelay(pdMS_TO_TICKS(2000));
                             if(storage_get_wifi(ssid, pass)) {
                                  wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), ssid, pass, NULL, NULL, 0, 0);
                             }
                        } else {
                             // Hết lượt -> Bật BLE Config
                             app_send_event(APP_EVENT_WIFI_FATAL_ERROR, NULL);
                        }
                    }
                    break;

                // -------------------------------------------------
                // CASE 3: FATAL ERROR -> BẬT BLE
                // -------------------------------------------------
                case APP_EVENT_WIFI_FATAL_ERROR:
                    printf("[APP] Critical Error -> Enable BLE Config Mode.\r\n");
                    wifi_mgmr_sta_disconnect();
                    enable_ble_adv(); 
                    led_set_mode(LED_BLINK_FAST_MODE);
                    g_wifi_retry_cnt = 0;
                    break;

                // -------------------------------------------------
                // CASE 4: NHẬN LỆNH TỪ MQTT
                // -------------------------------------------------
                case APP_EVENT_MQTT_DATA_RX:
                    printf("[APP] MQTT CMD: %s\r\n", msg.data);
                    // Đẩy lệnh vào logic điều khiển cửa
                    app_door_controller_core_execute_cmd_string(msg.data);
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

// 1. Cầu nối Nút bấm -> Core Logic
static void on_button_event_bridge(btn_event_t event) {
    // Gọi trực tiếp vào Core để xử lý ngay lập tức
    app_door_controller_core_handle_button_event(event);
}

// 2. Cầu nối RF -> Core Logic
static void on_rf_event_bridge(rf_action_t action) {
    const char* cmd = NULL;
    switch (action) {
        case RF_ACTION_OPEN:  cmd = "OPEN"; break;
        case RF_ACTION_CLOSE: cmd = "CLOSE"; break;
        case RF_ACTION_STOP:  cmd = "STOP"; break;
        case RF_ACTION_LOCK:  cmd = "LOCK"; break;
        default: break;
    }

    if (cmd != NULL) {
        printf("[RF] Bridge CMD: %s\r\n", cmd);
        app_door_controller_core_execute_cmd_string(cmd);
    }
}

// ============================================================================
// BLE IMPLEMENTATION
// ============================================================================
static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, u16_t len, u16_t offset, u8_t flags) {
    static char data[512]; // Buffer lớn cho JSON
    if(len > 511) len = 511;
    memcpy(data, buf, len);
    data[len] = '\0';

    printf("[BLE] Payload: %s\r\n", data);
    cJSON *root = cJSON_Parse(data);
    if (!root) return len;

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    cJSON *broker = cJSON_GetObjectItem(root, "MQTTBroker");
    cJSON *user = cJSON_GetObjectItem(root, "MQTTusername");
    cJSON *mqpass = cJSON_GetObjectItem(root, "MQTTpassword");
    cJSON *token = cJSON_GetObjectItem(root, "MQTTtoken");

    // Lưu MQTT
    if (broker && token) {
        storage_save_mqtt_info(broker->valuestring, 
                               user ? user->valuestring : "", 
                               mqpass ? mqpass->valuestring : "", 
                               token->valuestring);
    }
    // Lưu Wifi & Reboot
    if (ssid && pass) {
        printf("[BLE] Saving Wifi & Rebooting...\r\n");
        storage_save_wifi_reboot(ssid->valuestring, pass->valuestring);
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
    BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEV_NAME, 12), // 12 = len("SmartDoor_V3")
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
// WIFI CALLBACK (Chỉ gửi tin nhắn về Queue)
// ============================================================================
static void wifi_event_cb(input_event_t* event, void* private_data) {
    char ssid[33], pass[64];
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_start_background(&conf);
            g_wifi_inited = 1;
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            if (!storage_get_wifi(ssid, pass)) {
                // Không có wifi -> Báo Fatal Error để bật BLE
                app_send_event(APP_EVENT_WIFI_FATAL_ERROR, NULL);
            } else {
                printf("[BOOT] Connecting Wifi: %s\r\n", ssid);
                wifi_mgmr_sta_connect(wifi_mgmr_sta_enable(), ssid, pass, NULL, NULL, 0, 0);
            }
            break;

        case CODE_WIFI_ON_GOT_IP:
            // Đã kết nối -> Gửi tin về App Task
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
    app_door_controller_core_init();
    
    // Gắn callback nút bấm
    app_button_init(on_button_event_bridge);

    // 2. KHỞI TẠO QUEUE
    g_app_queue = xQueueCreate(15, sizeof(app_msg_t));
    if (g_app_queue == NULL) printf("[ERR] Queue create failed!\r\n");

    // 3. TẠO TASK TRUNG TÂM
    // [FIX MEMORY] Giảm từ 4096 xuống 3072. Logic điều khiển cửa không cần quá 3KB.
    xTaskCreate(app_process_task, "app_core", 3072, NULL, 12, NULL);

    // 4. KHỞI CHẠY RF
    // [FIX MEMORY] Giảm từ 4096 xuống 2048. RF chỉ nhận GPIO, rất nhẹ.
    app_rf_start_task(1024, 10, on_rf_event_bridge);

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