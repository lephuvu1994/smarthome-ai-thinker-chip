#ifndef APP_CONF_H
#define APP_CONF_H

// --- HARDWARE PIN ---
#define LED_PIN_1           4
#define LED_PIN_2           14

// --- TIMING & LOGIC ---
#define MAX_RETRY           5
#define CHECK_INTERVAL      60000
#define LED_BLINK_SLOW      1000
#define LED_BLINK_FAST      200
#define LED_ON_MODE         0

// --- WATCHDOG CONFIG ---
#define WDT_TIMEOUT_SEC     20      // Chip sẽ reset nếu treo quá 10s
#define WDT_FEED_MS         6000    // Tự động cho ăn mỗi 3s

// --- BLE CONFIG ---
#define BLE_DEV_NAME        "EuroSmartHome"
// UUID Service
#define UUID_SVC_DEF        0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455
// UUID RX Characteristic
#define UUID_RX_DEF         0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616

// 1. Cấu hình cho HTTP thường (app_http.c)
#define HTTP_SERVER_IP      "192.168.0.100" // IP Server local
#define HTTP_PORT           80              // Cổng HTTP (số)
#define HTTP_URL            "/api/v1/setup" // Đường dẫn API

// 2. Cấu hình cho HTTPS SSL (app_https.c)
#define HTTPS_SERVER_DNS    "test.mosquitto.org" // Domain server thật
#define HTTPS_PORT_STR      "443"                // Cổng HTTPS (chuỗi)
#define HTTPS_URL           "/api/register"      // Đường dẫn API

#endif
