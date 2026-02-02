#ifndef APP_CONF_H
#define APP_CONF_H

// --- HARDWARE PIN ---
#define LED_PIN_1           4
#define LED_PIN_2           14

#define DEVICE_CODE         "1001"

// --- TIMING & LOGIC ---
#define MAX_RETRY           5
#define CHECK_INTERVAL      60000
#define LED_BLINK_SLOW      1000
#define LED_BLINK_FAST      200
#define LED_ON_MODE         0

// --- WATCHDOG CONFIG ---
#define WDT_TIMEOUT_SEC     15000      // Chip sẽ reset nếu treo quá 5s
#define WDT_FEED_MS         6000    // Tự động cho ăn mỗi 4s

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

// --- INPUT (Cảm biến/Nút nhấn) ---
// IN mở: Dùng chân TXD -> GPIO 16
#define GPIO_IN_OPEN        16
// IN dừng: Dùng chân RXD -> GPIO 7
#define GPIO_IN_STOP        7
// IN Khóa: Dùng chân IO11
#define GPIO_IN_LOCK        11
// IN Đóng: Dùng chân IO14
#define GPIO_IN_CLOSE       14
// --- OUTPUT (Relay điều khiển) ---
// OUT dừng: Dùng chân IO1
#define GPIO_OUT_STOP       1
// OUT khóa: Dùng chân IO5
#define GPIO_OUT_LOCK       5
// OUT đóng: Dùng chân IO4
#define GPIO_OUT_CLOSE      4
// OUT mở: Dùng chân IO3
#define GPIO_OUT_OPEN       3
// --- KHÁC ---
// LED báo trạng thái: Dùng chân IO17
#define GPIO_LED_STATUS     17


// ============================================================
// 2. CẤU HÌNH MQTT
// ============================================================
// Topic Format: %s sẽ được thay thế bằng DEVICE_CODE
// VD: home/garage/GARAGE_DOOR_01/cmd
#define MQTT_TOPIC_SUB_CMD      "home/garage/%s/cmd"     // Nhận lệnh
#define MQTT_TOPIC_PUB_STATUS   "home/garage/%s/status"  // Báo trạng thái

#define MQTT_QOS            1       // QoS 1 cho tin cậy
#define MQTT_KEEP_ALIVE     60      // Giây


// ============================================================
// 4. TIMING & LOGIC
// ============================================================
#define PULSE_DURATION_MS      500     // Thời gian giữ Relay (ms)
#define BTN_DEBOUNCE_MS     20      // Thời gian lọc nhiễu nút bấm
#define BTN_POLL_DELAY_MS   50      // Chu kỳ quét nút bấm

#define CHECK_INTERVAL      60000 // 60s check wifi

#endif
