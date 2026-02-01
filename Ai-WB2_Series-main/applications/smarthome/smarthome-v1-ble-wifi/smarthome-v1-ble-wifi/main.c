#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// --- SYSTEM & NETWORK ---
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <bl_sys.h>
#include <easyflash.h>
#include <blog.h>
#include <bl_gpio.h>
#include <lwip/tcpip.h>
#include <hal_wifi.h>
#include <wifi_mgmr_ext.h>

// --- BLE SDK ---
#include "ble_interface.h"
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"

// ============================================================================
// CẤU HÌNH (DEFINE)
// ============================================================================
#define LED_PIN_1       4
#define LED_PIN_2       14
#define BLINK_SLOW      1000
#define BLINK_FAST      200
#define LED_ON_MODE     0

#define MAX_RETRY       5           // Thử tối đa 5 lần
#define CHECK_INTERVAL  60000       // 60 giây check Wifi 1 lần

// UUID Service & Char (Cấu hình BLE)
#define UUID_SVC      BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455))
#define UUID_CHR_RX   BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616))
#define NAME_CONFIG   "Ai-WB2-V27"

// ============================================================================
// BIẾN TOÀN CỤC
// ============================================================================
static TimerHandle_t g_led_timer = NULL;
static TimerHandle_t g_wifi_check_timer = NULL;
static int g_led_state = 0;
static int g_wifi_retry_cnt = 0;
static int g_ble_active = 0;
static int g_wifi_inited = 0; // Cờ đánh dấu Wifi Driver đã init xong
static wifi_conf_t conf = { .country_code = "CN" };
static int g_ble_mode = 0; // Cờ đánh dấu xe có đang bật BLE khoong

// ============================================================================
// 1. CÁC HÀM TIỆN ÍCH (HELPER & STORAGE)
// ============================================================================

// Hàm cắt khoảng trắng thừa
void trim_str(char *str) {
    char *p = str;
    int l = strlen(p);
    while(l > 0 && (isspace((unsigned char)p[l - 1]) || p[l - 1] == '\r' || p[l - 1] == '\n')) p[--l] = 0;
    while(*p && (isspace((unsigned char)*p) || *p == '\r' || *p == '\n')) p++;
    if(p != str) memmove(str, p, strlen(p) + 1);
}

// Hàm lấy Wifi an toàn (Tránh lỗi ghi đè bộ nhớ đệm)
int get_wifi_safe(char *ssid_out, char *pass_out) {
    memset(ssid_out, 0, 33);
    memset(pass_out, 0, 64);
    char *temp_ssid = ef_get_env("my_ssid");
    if (temp_ssid) strcpy(ssid_out, temp_ssid);
    char *temp_pass = ef_get_env("my_pass");
    if (temp_pass) strcpy(pass_out, temp_pass);
    return (strlen(ssid_out) > 0);
}

// Hàm lưu Wifi và khởi động lại
static void save_wifi_and_reboot(char* ssid, char* pass) {
    trim_str(ssid); trim_str(pass);
    printf(">>> SAVE & REBOOT: [%s] [%s]\r\n", ssid, pass);
    ef_set_env("my_ssid", ssid);
    ef_set_env("my_pass", pass);
    ef_save_env();
    vTaskDelay(500);
    bl_sys_reset_por();
}

// ============================================================================
// 2. ĐIỀU KHIỂN LED
// ============================================================================
static void led_timer_cb(TimerHandle_t xTimer) {
    g_led_state = !g_led_state;
    bl_gpio_output_set(LED_PIN_1, g_led_state);
    bl_gpio_output_set(LED_PIN_2, g_led_state);
}

void led_init() {
    bl_gpio_enable_output(LED_PIN_1, 0, 0);
    bl_gpio_enable_output(LED_PIN_2, 0, 0);
    g_led_timer = xTimerCreate("LedTmr", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, led_timer_cb);
}

void set_led_mode(int period_ms) {
    if (period_ms == LED_ON_MODE) {
        if (xTimerIsTimerActive(g_led_timer)) xTimerStop(g_led_timer, 0);
        bl_gpio_output_set(LED_PIN_1, 0);
        bl_gpio_output_set(LED_PIN_2, 0);
    } else {
        if (xTimerIsTimerActive(g_led_timer)) xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0);
        else { xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0); xTimerStart(g_led_timer, 0); }
    }
}

// ============================================================================
// 3. LOGIC BLE (ADV & GATT)
// ============================================================================

static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, u16_t len, u16_t offset, u8_t flags) {
    static char data[128];
    if(len > 127) len = 127;
    memcpy(data, buf, len);
    data[len] = '\0';

    char *comma = strchr(data, ',');
    if(comma) {
        *comma = '\0';
        save_wifi_and_reboot(data, comma+1);
    }
    return len;
}

static struct bt_gatt_attr config_attrs[]= {
    BT_GATT_PRIMARY_SERVICE(UUID_SVC),
    BT_GATT_CHARACTERISTIC(UUID_CHR_RX, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE, NULL, ble_write_cb, NULL),
};
static struct bt_gatt_service config_service = BT_GATT_SERVICE(config_attrs);

static const struct bt_data ad_config[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, NAME_CONFIG, 10), // Độ dài tên
};

static void ble_init_cb(int err) {
    if (!err) bt_gatt_service_register(&config_service);
}

// Bật quảng cáo BLE (Control Mode)
void enable_ble_adv() {
    if (g_ble_active) return;
    g_ble_mode = 1;

    // [FIX] Ngắt kết nối Wifi trước để tránh xung đột RF
    wifi_mgmr_sta_disconnect();

    int ret = bt_le_adv_start(BT_LE_ADV_CONN, ad_config, ARRAY_SIZE(ad_config), NULL, 0);
    if (ret == 0) {
        printf("\r\n[MODE] >>> BLE ACTIVE (Control/Config) <<<\r\n");
        set_led_mode(BLINK_SLOW);
        g_ble_active = 1;
    }
}

// Tắt quảng cáo BLE (Wifi Mode)
void disable_ble_adv() {
    if (!g_ble_active) return;
    bt_le_adv_stop();
    printf("\r\n[MODE] >>> BLE STOPPED (Wifi Priority) <<<\r\n");
    g_ble_active = 0;
    g_ble_mode = 0;
}

// ============================================================================
// 4. LOGIC WIFI & PING PONG (CORE LOGIC)
// ============================================================================

void connect_wifi_now() {
    char ssid[33];
    char pass[64];

    if (get_wifi_safe(ssid, pass)) {
        printf("[WIFI] Connecting to [%s]...\r\n", ssid);
        set_led_mode(BLINK_FAST);
        wifi_interface_t wifi_interface = wifi_mgmr_sta_enable();
        wifi_mgmr_sta_connect(wifi_interface, ssid, pass, NULL, NULL, 0, 0);
    } else {
        printf("[ERR] SSID Empty!\r\n");
    }
}

// Timer 60s: Hết giờ ở chế độ BLE -> Tắt BLE thử lại Wifi
static void wifi_check_timer_cb(TimerHandle_t xTimer) {
    printf("\r\n[TIMER] 60s Elapsed. Retry Wifi...\r\n");
    disable_ble_adv();
    vTaskDelay(500);
    g_wifi_retry_cnt = 0; // Reset đếm lỗi
    connect_wifi_now();
}

static void wifi_event_cb(input_event_t* event, void* private_data) {
    switch (event->code) {
        // --- GIAI ĐOẠN KHỞI TẠO DRIVER ---
        case CODE_WIFI_ON_INIT_DONE:
            printf("[MAIN] Wifi Init Done. Starting Manager...\r\n");
            wifi_mgmr_start_background(&conf);
            g_wifi_inited = 1; // Đánh dấu đã init
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            printf("[MAIN] Wifi Manager Ready. Checking Storage...\r\n");
            // Lúc này Driver đã lên, an toàn để quyết định
            char ssid[33], pass[64];
            if (!get_wifi_safe(ssid, pass)) {
                printf("[BOOT] No Wifi config. Start BLE.\r\n");
                enable_ble_adv();
            } else {
                printf("[BOOT] Found Wifi [%s]. Connecting...\r\n", ssid);
                connect_wifi_now();
            }
            break;

        // --- GIAI ĐOẠN KẾT NỐI ---
        case CODE_WIFI_ON_GOT_IP:
            printf("\r\n[WIFI] >>> CONNECTED! <<<\r\n");
            g_wifi_retry_cnt = 0;
            if (xTimerIsTimerActive(g_wifi_check_timer)) xTimerStop(g_wifi_check_timer, 0);

            disable_ble_adv(); // Đảm bảo tắt BLE
            set_led_mode(LED_ON_MODE);
            break;

        // --- GIAI ĐOẠN MẤT KẾT NỐI (PING-PONG) ---
        case CODE_WIFI_ON_DISCONNECT:
             // Chỉ xử lý nếu đã init xong
        	if (g_ble_mode == 1) {
        	         return;
        	     }
             if(g_wifi_inited) {
                 g_wifi_retry_cnt++;
                 printf("[WIFI] Disconnected! Retry %d/%d\r\n", g_wifi_retry_cnt, MAX_RETRY);

                 if (g_wifi_retry_cnt < MAX_RETRY) {
                     vTaskDelay(2000);
                     connect_wifi_now();
                 }
                 else {
                     printf("[SYS] Too many failures. KILL WIFI -> START BLE.\r\n");

                     // 1. Dừng Wifi
                     wifi_mgmr_sta_disconnect();

                     // 2. Bật BLE
                     enable_ble_adv();

                     // 3. Hẹn giờ 60s thử lại
                     if (!xTimerIsTimerActive(g_wifi_check_timer)) {
                         xTimerStart(g_wifi_check_timer, 0);
                     }
                 }
             }
             break;
    }
}

// ============================================================================
// MAIN ENTRY
// ============================================================================
static void proc_main_entry(void *pvParameters)
{
    easyflash_init();
    led_init();

    // Tạo Timer (chưa start)
    g_wifi_check_timer = xTimerCreate("WfChk", pdMS_TO_TICKS(CHECK_INTERVAL), pdTRUE, (void *)0, wifi_check_timer_cb);

    // Init BLE Controller (Lớp vật lý)
    ble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_enable(ble_init_cb);

    // [FIX] LUÔN KHỞI ĐỘNG WIFI DRIVER TRƯỚC
    // Để tránh lỗi BLE bị treo khi lấy MAC Address
    printf("[BOOT] System Init. Starting Wifi Stack...\r\n");
    tcpip_init(NULL, NULL);
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    hal_wifi_start_firmware_task();
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);

    vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    puts("[OS] Booting V27 (Monolithic + Fix Hang)...\r\n");
    xTaskCreate(proc_main_entry, "main", 2048, NULL, 15, NULL);
}
