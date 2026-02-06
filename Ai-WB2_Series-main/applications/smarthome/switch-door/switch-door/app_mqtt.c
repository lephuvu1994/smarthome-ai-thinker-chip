#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <mqtt_client.h>
#include "blog.h"
#include "app_mqtt.h"
#include "app_storage.h"
#include "app_events.h" // Để gửi tin về Queue
#include "app_conf.h"

// --- CHỨNG CHỈ CA (DigiCert Global Root G2 - Dùng cho EMQX) ---
const char *emqx_ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
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

static axk_mqtt_client_handle_t mqtt_client = NULL;
static char g_sub_topic[64];
static char g_pub_topic[64];

// Callback xử lý sự kiện MQTT (Chạy trong Task ngầm của thư viện)
// Nguyên tắc: KHÔNG xử lý nặng, KHÔNG điều khiển phần cứng trực tiếp.
static axk_err_t mqtt_event_cb(axk_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("[MQTT] Connected to Broker!");
            // Subscribe topic điều khiển ngay khi kết nối
            // Ví dụ topic: "cmd/<deviceID>"
            axk_mqtt_client_subscribe(event->client, g_sub_topic, 1);
            printf("[MQTT] Subscribed to: %s", g_sub_topic);
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("[MQTT] Disconnected. Library will auto-reconnect...");
            break;

        case MQTT_EVENT_DATA:
            printf("[MQTT] Data Received!");
            // Copy data nhận được vào buffer tạm thời
            char tmp_buf[32] = {0};
            int len = event->data_len;
            if (len > 31) len = 31; // Cắt bớt nếu dài quá
            
            memcpy(tmp_buf, event->data, len);
            tmp_buf[len] = '\0';

            // Gửi sự kiện về App Task xử lý
            app_send_event(APP_EVENT_MQTT_DATA_RX, tmp_buf);
            break;

        case MQTT_EVENT_ERROR:
            printf("[MQTT] Event Error (TLS/TCP Check)");
            break;
            
        default:
            break;
    }
    return AXK_OK;
}

// Hàm khởi động MQTT (Được gọi từ Main khi đã có Wifi + SNTP)
void app_mqtt_start(void)
{
    if (mqtt_client != NULL) {
        blog_warn("[MQTT] Already started.");
        return;
    }

    char mqtt_broker[128], mqtt_user[64], mqtt_pass[64], mqtt_token_device[64];
    
    // 1. Lấy thông tin từ Flash
    if (!storage_get_mqtt_info(mqtt_broker, mqtt_user, mqtt_pass, mqtt_token_device)) {
        blog_error("[MQTT] No config found in Flash!");
        return;
    }

    // 2. Tạo URI chuẩn (Fix lỗi mqtt/mqtts)
    char uri[150];
    if (strstr(mqtt_broker, "://") != NULL) {
         snprintf(uri, sizeof(uri), "%s", mqtt_broker);
    } else {
         // Mặc định dùng SSL port 8883 nếu người dùng quên nhập
         snprintf(uri, sizeof(uri), "mqtts://%s:8883", mqtt_broker);
    }

    // 3. Tạo Topic theo Token (Ví dụ)
    snprintf(g_sub_topic, sizeof(g_sub_topic), "%s/%s/%s/set", 
             MY_COMPANY_CODE,   // BKTech
             MY_DEVICE_CODE,    // 1001);
             mqtt_token_device); // congtaccuacuon

    // 2. TOPIC PUB (BÁO TRẠNG THÁI) -> Phải là "/status"
    // Cửa chạy xong bắn: {"status":"OPENED"} vào đây
    snprintf(g_pub_topic, sizeof(g_pub_topic), "%s/%s/%s/status", 
             MY_COMPANY_CODE, 
             MY_DEVICE_CODE,
             mqtt_token_device);
    axk_mqtt_client_config_t mqtt_cfg = {
        .uri = uri,
        .event_handle = mqtt_event_cb,
        .client_id = mqtt_token_device,  
        .keepalive = 60,
        .cert_pem = NULL,       // Mặc định NULL
        .client_cert_pem = NULL, // Mặc định NULL
        .client_key_pem = NULL,  // Mặc định NULL
    };

    // 2. Kiểm tra Logic SSL: Nếu URI chứa "mqtts://"
    if (strstr(uri, "mqtts://") != NULL) {
        printf("[MQTT] Phat hien giao thuc MQTTS (SSL) -> Dang nap chung chi...\r\n");
        
        // Gán chứng chỉ CA (Bắt buộc với SSL để xác thực Server)
        mqtt_cfg.cert_pem = emqx_ca_cert;
        
    } else {
        printf("[MQTT] Phat hien giao thuc MQTT thuong (Plain) -> Khong dung SSL.\r\n");
    }
     // SỬ DỤNG USER/PASS ĐÃ LẤY TỪ FLASH:
     if (strlen(mqtt_user) > 0) {
        mqtt_cfg.username = mqtt_user;
        printf("[MQTT] Use Username: %s\r\n", mqtt_user);
    }
        mqtt_cfg.password = mqtt_pass;
        printf("[MQTT] Password set (hidden)\r\n");
    
    // 5. Khởi tạo và Chạy
    mqtt_client = axk_mqtt_client_init(&mqtt_cfg);
    axk_mqtt_client_start(mqtt_client);
}

// Hàm public trạng thái cửa (Gọi từ App Task)
void app_mqtt_pub_status(const char* status) {
    if (mqtt_client) {
        // QoS 1 để đảm bảo tin đến được server
        axk_mqtt_client_publish(mqtt_client, g_pub_topic, status, 0, 1, 0);
    }
}