#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h> // [NEW] Thư viện xử lý ký tự

// --- SYSTEM ---
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <bl_sys.h>
#include <easyflash.h>
#include <blog.h>
#include <bl_gpio.h>

// --- WIFI ---
#include <lwip/tcpip.h>
#include <wifi_mgmr_ext.h>
#include <hal_wifi.h>

// --- BLE ---
#include "ble_interface.h"
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"

// ============================================================================
// CẤU HÌNH
// ============================================================================
#define LED_PIN_1     4
#define LED_PIN_2     14
#define BLINK_SLOW    1000
#define BLINK_FAST    200
#define LED_ON_MODE   0

#define UUID_SVC      BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455))
#define UUID_CHR_RX   BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616))
#define NAME_CONFIG   "Ai-WB2-CONFIG"

static TimerHandle_t g_led_timer = NULL;
static int g_led_state = 0;

// ============================================================================
// 1. CÁC HÀM TIỆN ÍCH (HELPER) - [QUAN TRỌNG]
// ============================================================================

// Hàm cắt khoảng trắng và ký tự xuống dòng ở đầu và cuối chuỗi
// Ví dụ: "  MyWifi \n" -> "MyWifi"
void trim_str(char *str) {
    char *p = str;
    int l = strlen(p);

    // Cắt khoảng trắng bên phải (Trailing)
    while(l > 0 && (isspace((unsigned char)p[l - 1]) || p[l - 1] == '\r' || p[l - 1] == '\n'))
        p[--l] = 0;

    // Cắt khoảng trắng bên trái (Leading) - Dịch chuyển chuỗi
    while(*p && (isspace((unsigned char)*p) || *p == '\r' || *p == '\n'))
        p++;

    if(p != str) memmove(str, p, strlen(p) + 1);
}

static void led_timer_cb(TimerHandle_t xTimer) {
    g_led_state = !g_led_state;
    bl_gpio_output_set(LED_PIN_1, g_led_state);
    bl_gpio_output_set(LED_PIN_2, g_led_state);
}

void set_led_mode(int period_ms) {
    if (period_ms == LED_ON_MODE) {
        if (xTimerIsTimerActive(g_led_timer)) xTimerStop(g_led_timer, 0);
        bl_gpio_output_set(LED_PIN_1, 0);
        bl_gpio_output_set(LED_PIN_2, 0);
    } else {
        if (xTimerIsTimerActive(g_led_timer)) {
            xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0);
        } else {
            xTimerChangePeriod(g_led_timer, pdMS_TO_TICKS(period_ms), 0);
            xTimerStart(g_led_timer, 0);
        }
    }
}

void led_init() {
    bl_gpio_enable_output(LED_PIN_1, 0, 0);
    bl_gpio_enable_output(LED_PIN_2, 0, 0);
    g_led_timer = xTimerCreate("LedTmr", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, led_timer_cb);
}

// ============================================================================
// 2. BLE LOGIC
// ============================================================================

static void save_wifi_and_reboot(char* ssid, char* pass) {
    // [FIX] Cắt gọt sạch sẽ trước khi lưu
    trim_str(ssid);
    trim_str(pass);

    printf("\r\n/******************************************/\r\n");
    printf("   SAVING WIFI CONFIG:\r\n");
    printf("   SSID: [%s] (Len: %d)\r\n", ssid, strlen(ssid));
    printf("   PASS: [%s] (Len: %d)\r\n", pass, strlen(pass));
    printf("/******************************************/\r\n");

    ef_set_env("my_ssid", ssid);
    ef_set_env("my_pass", pass);
    ef_save_env();

    set_led_mode(BLINK_FAST);
    vTaskDelay(1000);
    bl_sys_reset_por();
}

static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, u16_t len, u16_t offset, u8_t flags) {
    static char data[128];
    if(len > 127) len = 127;
    memcpy(data, buf, len);
    data[len] = '\0';

    printf("[BLE] Raw Recv: '%s'\r\n", data); // In ra xem có ký tự lạ không

    char *comma = strchr(data, ',');
    if(comma) {
        *comma = '\0';
        // Gọi hàm lưu (đã tích hợp bộ lọc)
        save_wifi_and_reboot(data, comma+1);
    } else {
        printf("[BLE] ERR: Format must be 'SSID,PASS'\r\n");
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
	BT_DATA(BT_DATA_NAME_COMPLETE, NAME_CONFIG, 13),
};

static void ble_ready_cb(int err) {
    if (!err) {
        bt_gatt_service_register(&config_service);
        bt_le_adv_start(BT_LE_ADV_CONN, ad_config, ARRAY_SIZE(ad_config), NULL, 0);
        printf("[MODE] BLE CONFIG STARTED.\r\n");
        set_led_mode(BLINK_SLOW);
    }
}

void start_ble_only_mode() {
    ble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_enable(ble_ready_cb);
}

// ============================================================================
// 3. WIFI LOGIC
// ============================================================================
static wifi_conf_t conf = { .country_code = "CN" };

static void wifi_event_cb(input_event_t* event, void* private_data) {
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_start_background(&conf);
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            {
                char *ssid = ef_get_env("my_ssid");
                char *pass = ef_get_env("my_pass");
                if(ssid) {
                    printf("[WIFI] Connecting to [%s]...\r\n", ssid);
                    set_led_mode(BLINK_FAST);
                    wifi_interface_t wifi_interface = wifi_mgmr_sta_enable();
                    wifi_mgmr_sta_connect(wifi_interface, ssid, pass, NULL, NULL, 0, 0);
                }
            }
            break;

        case CODE_WIFI_ON_GOT_IP:
            printf("\r\n[WIFI] >>> CONNECTED! <<<\r\n");
            set_led_mode(LED_ON_MODE);
            break;

        case CODE_WIFI_ON_DISCONNECT:
             // Thử lại hoặc logic đếm số lần (tùy ý)
             printf("[WIFI] Disconnected! Scanning again...\r\n");
             set_led_mode(BLINK_FAST);
             break;
    }
}

void start_wifi_only_mode() {
    printf("\r\n[MODE] WIFI STARTED.\r\n");
    set_led_mode(BLINK_FAST);
    tcpip_init(NULL, NULL);
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    hal_wifi_start_firmware_task();
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
}

// ============================================================================
// 4. MAIN
// ============================================================================
static void proc_main_entry(void *pvParameters)
{
    easyflash_init();
    led_init();

    char *ssid = ef_get_env("my_ssid");

    // In ra xem Wifi đang lưu là gì (có ký tự lạ không)
    if (ssid) printf("[BOOT] Current SSID in Flash: '%s'\r\n", ssid);

    if (ssid == NULL || strlen(ssid) == 0) {
        start_ble_only_mode();
    }
    else {
        start_wifi_only_mode();
    }

    vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    puts("[OS] Booting V21 (Trim Input Fix)...\r\n");
    xTaskCreate(proc_main_entry, "main", 2048, NULL, 15, NULL);
}
