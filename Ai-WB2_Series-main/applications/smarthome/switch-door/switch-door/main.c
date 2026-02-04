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
#include <hal_wifi.h>
#include <wifi_mgmr_ext.h>

// --- MODULES (NEW) ---
#include <cJSON.h>

#include "app_button.h"     // Driver Input
#include "app_conf.h"
#include "app_door_controller_core.h"  // Business Logic
#include "app_http.h"
#include "app_led.h"
#include "app_mqtt.h"       // Driver Net
#include "app_output_relay.h"      // Driver Output
#include "app_storage.h"
#include "app_watchdog.h"
#include "app_events.h" // Include file sự kiện mới tạo
#include "app_rf.h"

#include "ble_interface.h"
// --- BLE SDK ---
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"



// --- MACROS ---
#define UUID_EXPAND(...)    BT_UUID_128_ENCODE(__VA_ARGS__)
#define UUID_SVC      BT_UUID_DECLARE_128(UUID_EXPAND(UUID_SVC_DEF))
#define UUID_CHR_RX   BT_UUID_DECLARE_128(UUID_EXPAND(UUID_RX_DEF))

// --- GLOBAL VARS ---
static TimerHandle_t g_wifi_check_timer = NULL;
static int g_wifi_retry_cnt = 0;
static int g_ble_active = 0;
static int g_wifi_inited = 0;
static wifi_conf_t conf = { .country_code = "CN" };
static int g_ble_mode = 0; // Cờ Gatekeeper
static int g_has_connected_once = 0;
static int g_lock_active = 0; // Biến quản lý khoa

// ============================================================================
// BLE LOGIC
// ============================================================================
static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, u16_t len, u16_t offset, u8_t flags) {
    // 1. Copy dữ liệu từ BLE Buffer vào biến tạm (để đảm bảo có null-terminator)
    static char data[256]; // Tăng lên 256 cho thoải mái JSON
    if(len > 255) len = 255;
    memcpy(data, buf, len);
    data[len] = '\0';

    printf("[BLE] Received: %s\r\n", data);

    // 2. Parse JSON
    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        printf("[BLE] JSON Parse Error!\r\n");
        return len;
    }
    // 3. Khai báo và lấy các biến (Đã sửa lỗi trùng tên biến)
            cJSON *ssid_item     = cJSON_GetObjectItem(root, "ssid");
            cJSON *pass_item     = cJSON_GetObjectItem(root, "password");
            cJSON *broker_item   = cJSON_GetObjectItem(root, "MQTTBroker");
            cJSON *user_mqtt_item = cJSON_GetObjectItem(root, "MQTTusername"); // Đổi tên biến
            cJSON *pass_mqtt_item = cJSON_GetObjectItem(root, "MQTTpassword"); // Đổi tên biến
            cJSON *token_item    = cJSON_GetObjectItem(root, "MQTTtoken");
            // 4. Xử lý Logic MQTT
                    // Kiểm tra xem ít nhất phải có Broker và Token (hoặc tùy điều kiện dự án của bạn)
                    if ((broker_item && broker_item->type == cJSON_String) &&
                        (token_item && token_item->type == cJSON_String)) {

                        printf("[BLE] Found MQTT Config via BLE\r\n");

                        // Lấy giá trị chuỗi, nếu không có username/pass thì để chuỗi rỗng ""
                        char *mqtt_user = (user_mqtt_item && user_mqtt_item->type == cJSON_String) ? user_mqtt_item->valuestring : "";
                        char *mqtt_pass = (pass_mqtt_item && pass_mqtt_item->type == cJSON_String) ? pass_mqtt_item->valuestring : "";

                        // Lưu vào Flash với đầy đủ thông tin đã parse được
                        storage_save_mqtt_info(broker_item->valuestring,
                                               mqtt_user,
                                               mqtt_pass,
                                               token_item->valuestring);
                    }

                    // 5. Xử lý Logic Wifi
                    if ((ssid_item && ssid_item->type == cJSON_String) &&
                        (pass_item && pass_item->type == cJSON_String)) {

                        printf("[BLE] Found Wifi Config: %s\r\n", ssid_item->valuestring);
                        storage_save_wifi_reboot(ssid_item->valuestring, pass_item->valuestring);
                    }
        // 6. Giải phóng RAM
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
	BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEV_NAME, 10),
};

static void http_register_task(void *pvParameters) {
    printf("[TASK] Starting HTTP Register Task...\r\n");

    // Gọi hàm nặng ở đây
    if (app_http_register_device()) {
        printf("[TASK] Register Success. Rebooting System...\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        bl_sys_reset_por(); // Reset để chạy lại từ đầu với cấu hình mới
    } else {
        printf("[TASK] Register Failed. Will retry or stay offline.\r\n");
    }

    // Tự sát sau khi hoàn thành nhiệm vụ để giải phóng RAM
    vTaskDelete(NULL);
}

static void ble_init_cb(int err) {
    if (!err) bt_gatt_service_register(&config_service);
}

void enable_ble_adv() {
    if (g_ble_active) return;
    g_ble_mode = 1;

    wifi_mgmr_sta_disconnect();

    int ret = bt_le_adv_start(BT_LE_ADV_CONN, ad_config, ARRAY_SIZE(ad_config), NULL, 0);
    if (ret == 0) {
        printf("\r\n[MODE] >>> BLE ACTIVE <<<\r\n");
        // [MODULE] Gọi LED
        led_set_mode(LED_BLINK_SLOW);
        g_ble_active = 1;
    }
}

void disable_ble_adv() {
    if (!g_ble_active) return;
    bt_le_adv_stop();
    printf("\r\n[MODE] >>> BLE STOPPED <<<\r\n");
    g_ble_active = 0;
    g_ble_mode = 0;
}

// ============================================================================
// WIFI LOGIC
// ============================================================================
void connect_wifi_now() {
    char ssid[33], pass[64];
    // [MODULE] Lấy wifi từ Storage
    if (storage_get_wifi(ssid, pass)) {
        printf("[WIFI] Connecting to [%s]...\r\n", ssid);
        led_set_mode(LED_BLINK_FAST);
        wifi_interface_t wifi_interface = wifi_mgmr_sta_enable();
        wifi_mgmr_sta_connect(wifi_interface, ssid, pass, NULL, NULL, 0, 0);
    } else {
        printf("[ERR] SSID Empty!\r\n");
    }
}

static void wifi_check_timer_cb(TimerHandle_t xTimer) {
    printf("\r\n[TIMER] 60s Elapsed. Retry Wifi...\r\n");
    disable_ble_adv();
    vTaskDelay(500);
    g_wifi_retry_cnt = 0;
    connect_wifi_now();
}

static void wifi_event_cb(input_event_t* event, void* private_data) {
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            printf("[MAIN] Wifi Init Done. Start Manager...\r\n");
            wifi_mgmr_start_background(&conf);
            g_wifi_inited = 1;
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            printf("[MAIN] Wifi Manager Ready.\r\n");
            char ssid[33], pass[64];
            // [MODULE] Check storage
            if (!storage_get_wifi(ssid, pass)) {
                printf("[BOOT] No Wifi config -> Start BLE.\r\n");
                enable_ble_adv();
            } else {
                printf("[BOOT] Found Wifi -> Connecting...\r\n");
                connect_wifi_now();
            }
            break;
        case CODE_WIFI_ON_SCAN_DONE:
                {
                    printf("[MAIN] Scan Done. Checking Results...\r\n");
                    // Lấy thông tin SSID từ Flash
                    char ssid[33], pass[64];
                    if (!storage_get_wifi(ssid, pass)) {
                        enable_ble_adv();
                        return;
                    }

                    // Hàm kiểm tra xem SSID có trong danh sách Scan không
                    // Lưu ý: wifi_mgmr_scan_ap_all() trả về danh sách, ta giả lập logic kiểm tra ở đây
                    // Trong SDK BL602 thực tế, nếu bạn gọi connect_wifi_now() mà SSID không tồn tại
                    // nó sẽ tự trả về CODE_WIFI_ON_DISCONNECT sau vài giây.
                    // Nên ta có thể gộp logic vào Disconnect để code đỡ phức tạp.

                    printf("[BOOT] Target SSID: [%s] -> Attempting connection...\r\n", ssid);
                    connect_wifi_now();
                }
                break;
        case CODE_WIFI_ON_GOT_IP:
            printf("\r\n[WIFI] >>> CONNECTED! <<<\r\n");
            g_has_connected_once = 1;
            g_wifi_retry_cnt = 0;
            if (xTimerIsTimerActive(g_wifi_check_timer)) xTimerStop(g_wifi_check_timer, 0);

            disable_ble_adv(); // Tắt BLE để nhường RAM
            led_set_mode(LED_ON_MODE);

            // [LOGIC MỚI]
            if (storage_has_mqtt_config()) {
                printf("[MAIN] Config Found. Starting MQTT...\r\n");
                app_mqtt_start();
                app_mqtt_init(on_mqtt_cmd_bridge);
            } else {
                printf("[MAIN] No Config. Spawning HTTP Task...\r\n");
                // [FIX CRASH] Tạo Task riêng để chạy HTTP, không chạy trực tiếp ở đây!
                // Stack size 4096 bytes: Rất an toàn cho JSON và HTTP
                xTaskCreate(http_register_task, "http_task", 4096, NULL, 10, NULL);
            }
            break;

        case CODE_WIFI_ON_DISCONNECT:
            if (g_ble_mode == 1) return; // Đang config thì kệ
            if(g_wifi_inited) {
            	printf("[WIFI] Disconnected! Status: Connected_Once=%d\r\n", g_has_connected_once);
            	// --- LOGIC PHÂN LOẠI ---
            	// TRƯỜNG HỢP A: Mất mạng / Router tắt (Case 3 & 4)
                // Dấu hiệu: Đã từng kết nối thành công (g_has_connected_once == 1)
                if (g_has_connected_once == 1) {
                	printf("[SYS] Network Lost (Router reboot or signal weak).\r\n");
                	printf("[SYS] Auto Reconnecting forever...\r\n");
                	vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi 5s rồi thử lại
                	connect_wifi_now(); // Thử lại mãi mãi, không bao giờ bật BLE
                }
                // TRƯỜNG HỢP B: Sai Password hoặc SSID không tồn tại (Case 1 & 2)
                // Dấu hiệu: Chưa từng kết nối được lần nào (g_has_connected_once == 0)
                else {
                	g_wifi_retry_cnt++;
                    printf("[AUTH] Login Failed! Attempt %d/%d\r\n", g_wifi_retry_cnt, MAX_RETRY);
                    if (g_wifi_retry_cnt <= MAX_RETRY) {
                    	// Còn lượt thử -> Thử lại
                    	vTaskDelay(pdMS_TO_TICKS(2000));
                    	connect_wifi_now();
                    } else {
                    	// Hết lượt -> CHẮC CHẮN SAI PASS HOẶC SAI TÊN WIFI
                    	printf("[SYS] Critical: Wrong Password or SSID Not Found.\r\n");
                    	printf("[SYS] >>> ENABLE BLE CONFIG MODE <<<\r\n");

                        wifi_mgmr_sta_disconnect();
                        enable_ble_adv(); // Bật BLE
                        led_set_mode(LED_BLINK_FAST);
                        // Reset lại biến đếm
                        g_wifi_retry_cnt = 0;
                    }
                }
           }
    }
}

// ============================================================================
// MAIN ENTRY
// ============================================================================
static void proc_main_entry(void *pvParameters)
{
    // 1. Init Modules
    storage_init();
    led_init();
    printf("LED init success\r\n");
    // 2. Khởi tạo Driver Output (Relay)
    app_relay_init();
    printf("Relay init success\r\n");

    // 3. Khởi tạo Core Logic (Bộ não)
    app_door_controller_core_init();
    printf("Controller init success\r\n");

    // 4. Khởi tạo Button (Input) và gắn cầu nối về Core
    // Từ giờ bấm nút -> on_button_event_bridge -> app_door_core
    app_button_init(on_button_event_bridge);
    printf("app_button_init success\r\n");

    // 5. Tao task cho phần xử lý RF
    app_rf_start_task(2048, 10, on_rf_event_bridge);
    printf("app_rf_start_task");
    // 6. Setup Wifi Logic
    g_wifi_check_timer = xTimerCreate("WfChk", pdMS_TO_TICKS(CHECK_INTERVAL), pdTRUE, (void *)0, wifi_check_timer_cb);

    // 7. Init BLE
    ble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_enable(ble_init_cb);

    // 8. Init Wifi
    printf("[BOOT] System Init. Starting Wifi Stack...\r\n");
    tcpip_init(NULL, NULL);
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    hal_wifi_start_firmware_task();
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
    
    // 9. Watchdog
     app_watchdog_init();

     vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    puts("[OS] Booting V30 (Refactored Storage/LED)...\r\n");
    // [FIX] Stack Size 5120
    xTaskCreate(proc_main_entry, "main", 5120, NULL, 15, NULL);
}
