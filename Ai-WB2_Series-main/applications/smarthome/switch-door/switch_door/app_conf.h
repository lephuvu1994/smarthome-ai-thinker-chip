#ifndef APP_CONF_H
#define APP_CONF_H

#define MY_DEVICE_CODE         "1001"
#define MY_COMPANY_CODE     "BKTech"

// --- TIMING & LOGIC ---
#define MAX_RETRY           5
#define CHECK_INTERVAL      60000
#define LED_BLINK_SLOW_MODE      1000
#define LED_BLINK_FAST_MODE      200
#define LED_ON_MODE         0

// --- WATCHDOG CONFIG ---
#define WDT_TIMEOUT_SEC     15000      // Chip sẽ reset nếu treo quá 15s
#define WDT_FEED_MS         6000    // Tự động cho ăn mỗi 6s

// --- BLE CONFIG ---
#define BLE_DEV_NAME        "BKTech"

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
#define GPIO_IN_OPEN           4   
#define GPIO_IN_CLOSE          5   
#define GPIO_IN_STOP           GPIO_IN_OPEN  
#define GPIO_IN_SETUP_LEARN_RF GPIO_IN_OPEN  
#define GPIO_IN_RF_PIN         11  

#define GPIO_OUT_CLOSE         3  
#define GPIO_OUT_OPEN          17  
#define GPIO_OUT_STOP          14   
#define GPIO_OUT_BUZZER        7 // Tạm dùng chung chân Open để test log trước
#define GPIO_LED_STATUS        16

// --- CẤU HÌNH THỜI GIAN CỦA NÚT LỆNH ĐIỀU KHIỂN HỌC LỆNH KÉO DÀI 10s ---
#define BTN_LONG_PRESS_MS   10000
#define LEARN_MODE_TIMEOUT_MS   180000 // 3 phút

#define BUZZER_TIME_SHORT     100  // Kêu tít 1 cái (0.1s) - Dùng cho lệnh Open/Close
#define BUZZER_TIME_LONG      1000 // Kêu tít dài (1s) - Dùng khi vào chế độ học hoặc thành công

// ============================================================
// 2. CẤU HÌNH MQTT
// ============================================================
// Topic Format: %s sẽ được thay thế bằng DEVICE_CODE
// --- CẤU HÌNH MQTT ---
// %s sẽ được thay thế bằng TOKEN lấy từ Storage
// VD: device/dai-token-ngau-nhien-xyz/cmd
#define MQTT_TOPIC_SUB_CMD      "%s/%s/%s/set"     
#define MQTT_TOPIC_PUB_STATUS   "%s/%s/%s/status"
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


// --- CẤU HÌNH CỦA NÚT LỆNH ĐIỀU KHIỂN ---
#define DEFAULT_TRAVEL_TIME_MS  20000  // Mặc định 20 giây (khi chưa học)
#define MIN_LEARN_TIME_MS       1000   // Tối thiểu 1 giây mới tính là học
#define MAX_SAFE_TIME_MS        120000 // Giới hạn an toàn 2 phút

#define RF_LEARN_TIMEOUT_MS   60000 // 60s nếu không bấm lệnh học thì thoát


#endif
