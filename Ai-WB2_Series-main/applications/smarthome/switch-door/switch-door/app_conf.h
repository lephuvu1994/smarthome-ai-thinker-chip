#ifndef APP_CONF_H
#define APP_CONF_H

#define DEVICE_CODE         "1001"

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
#define GPIO_IN_OPEN        4  // IO12 (Cạnh trái)
#define GPIO_IN_CLOSE       8   // IO3  (Cạnh phải)
#define GPIO_IN_STOP        5  // IO17 (Cạnh trái)
// #define GPIO_IN_SETUP_LEARN_RF        11  // IO17 (Cạnh trái)
#define GPIO_IN_RF_PIN 11 // Chân DOUT của CMT2220LS

// --- OUTPUT (Relay - Kích mức 1/VCC) ---
#define GPIO_OUT_CLOSE      14   // IO4 (Trùng LED_PIN_1 - Vừa đóng vừa sáng)
#define GPIO_OUT_OPEN       17  // IO14 (Trùng LED_PIN_2 - Vừa mở vừa sáng)
#define GPIO_OUT_STOP       3   // IO5 (Cạnh phải)
#define GPIO_OUT_BUZZER    12   // IO13 (Cạnh phải)
// Đèn báo (Trùng với relay để tiết kiệm chân)
#define GPIO_LED_STATUS     GPIO_OUT_BUZZER

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


// --- CẤU HÌNH CỦA NÚT LỆNH ĐIỀU KHIỂN ---
#define DEFAULT_TRAVEL_TIME_MS  20000  // Mặc định 20 giây (khi chưa học)
#define MIN_LEARN_TIME_MS       1000   // Tối thiểu 1 giây mới tính là học
#define MAX_SAFE_TIME_MS        120000 // Giới hạn an toàn 2 phút


// --- CẤU HÌNH CỦA MQTTS ---
// --- CẤU HÌNH CỦA MQTTS ---
// Chứng chỉ CA: DigiCert Global Root G2 (Trích xuất từ file emqxsl-ca.crt)
#define CA_CERT "-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
"Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n";


#endif
