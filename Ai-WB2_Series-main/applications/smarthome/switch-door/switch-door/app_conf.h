#ifndef APP_CONF_H
#define APP_CONF_H

#define DEVICE_CODE         "1001"

// --- TIMING & LOGIC ---
#define MAX_RETRY           5
#define CHECK_INTERVAL      60000
#define LED_BLINK_SLOW      1000
#define LED_BLINK_FAST      200
#define LED_ON_MODE         0

// --- WATCHDOG CONFIG ---
#define WDT_TIMEOUT_SEC     15000      // Chip sẽ reset nếu treo quá 15s
#define WDT_FEED_MS         6000    // Tự động cho ăn mỗi 6s

// --- BLE CONFIG ---
#define BLE_DEV_NAME        "EuroSmartHome"
// UUID Service
#define UUID_SVC_DEF        0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455
// UUID RX Characteristic
#define UUID_RX_DEF         0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616

// 1. Cấu hình cho HTTP thường (app_http.c)
#define HTTP_SERVER_IP      "192.168.0.100" // IP Server local
#define HTTP_PORT           80              // Cổng HTTP (số)
#define HTTP_URL            "/api/v1/registerDevice" // Đường dẫn API

// 2. Cấu hình cho HTTPS SSL (app_https.c)
#define HTTPS_SERVER_DNS    "test.mosquitto.org" // Domain server thật
#define HTTPS_PORT_STR      "443"                // Cổng HTTPS (chuỗi)
#define HTTPS_URL           "/api/register"      // Đường dẫn API


// ============================================================
// CẤU HÌNH CHÂN GPIO (Mapping theo Ai-WB2-12F)
// ============================================================
// INPUT (Nút bấm nối đất, kích mức 0)
// Lưu ý: GPIO 16 (TXD) và 7 (RXD) cần xử lý nhiễu khởi động kỹ
// --- INPUT (Nút bấm - Kích mức 0/GND) ---
// Sử dụng toàn bộ chân sạch nhất cho Input để nút bấm nhạy, không nhiễu
#define GPIO_IN_OPEN        12  // IO12 (Cạnh trái)
#define GPIO_IN_CLOSE       3   // IO3  (Cạnh phải)
//#define GPIO_IN_STOP        17  // IO17 (Cạnh trái)
//#define GPIO_IN_LOCK        11  // IO11 (Cạnh trái)

#define RF_PIN 17 // Chân DOUT của CMT2220LS

// --- OUTPUT (Relay - Kích mức 1/VCC) ---
#define GPIO_OUT_CLOSE      4   // IO4 (Trùng LED_PIN_1 - Vừa đóng vừa sáng)
#define GPIO_OUT_OPEN       11  // IO14 (Trùng LED_PIN_2 - Vừa mở vừa sáng)
#define GPIO_OUT_STOP       5   // IO5 (Cạnh phải)

// Đèn báo (Trùng với relay để tiết kiệm chân)
#define GPIO_LED_STATUS     GPIO_OUT_OPEN

// --- HARDWARE PIN ---
#define LED_PIN_1           4
#define LED_PIN_2           14


// ============================================================
// 2. CẤU HÌNH MQTT
// ============================================================
// Topic Format: %s sẽ được thay thế bằng DEVICE_CODE
// --- CẤU HÌNH MQTT ---
// %s sẽ được thay thế bằng TOKEN lấy từ Storage
// VD: device/dai-token-ngau-nhien-xyz/cmd
#define MQTT_TOPIC_SUB_CMD      "device/%s/cmd"     
#define MQTT_TOPIC_PUB_STATUS   "device/%s/status"
#define MQTT_QOS            1       // QoS 1 cho tin cậy
#define MQTT_KEEP_ALIVE     60      // Giây


// ============================================================
// 4. TIMING & LOGIC
// ============================================================
#define PULSE_DURATION_MS      500     // Thời gian giữ Relay (ms)
#define BTN_DEBOUNCE_MS     20      // Thời gian lọc nhiễu nút bấm
#define BTN_POLL_DELAY_MS   50      // Chu kỳ quét nút bấm
#define RELAY_PULSE_MS    20000     // Chu kỳ quét RF

#define CHECK_INTERVAL      60000 // 60s check wifi ( Auto connect wifi)

#endif
