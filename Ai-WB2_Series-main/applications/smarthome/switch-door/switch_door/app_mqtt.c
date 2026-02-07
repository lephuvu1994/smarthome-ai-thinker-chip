#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <mqtt_client.h>
#include "blog.h"
#include "app_mqtt.h"
#include "app_storage.h"
#include "app_events.h"
#include "app_conf.h"

// ============================================================================
// CHỨNG CHỈ CA (DigiCert Global Root G2 - Dùng cho EMQX / AWS / Azure)
// ============================================================================
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

// ============================================================================
// BIẾN TOÀN CỤC
// ============================================================================
static axk_mqtt_client_handle_t mqtt_client = NULL;
static char g_sub_topic[128];
static char g_pub_topic[128];

// Biến quản lý trạng thái kết nối và callback
static mqtt_connect_cb_t g_connect_cb = NULL;
static int g_mqtt_connected_flag = 0;

// ============================================================================
// HÀM HỖ TRỢ (HELPER FUNCTIONS)
// ============================================================================

// Kiểm tra trạng thái kết nối MQTT (Dùng cho Core Logic)
int app_mqtt_is_connected(void) {
    return g_mqtt_connected_flag;
}

// Đăng ký hàm callback khi kết nối thành công (Dùng cho Sync Offline)
void app_mqtt_on_connect(mqtt_connect_cb_t cb) {
    g_connect_cb = cb;
}

// Gửi trạng thái lên Server (Publish)
void app_mqtt_pub_status(const char* status) {
    if (mqtt_client && g_mqtt_connected_flag) {
        // QoS 1 để đảm bảo tin đến được server ít nhất 1 lần
        axk_mqtt_client_publish(mqtt_client, g_pub_topic, status, 0, MQTT_QOS, 0);
        printf("[MQTT] Pub Status: %s -> %s\r\n", status, g_pub_topic);
    } else {
        printf("[MQTT] Pub Failed (Client NULL or Disconnected)\r\n");
    }
}

// ============================================================================
// MQTT EVENT CALLBACK (XỬ LÝ SỰ KIỆN TỪ THƯ VIỆN)
// ============================================================================
static axk_err_t mqtt_event_cb(axk_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("[MQTT] Connected to Broker!\r\n");
            
            // 1. Cập nhật cờ trạng thái
            g_mqtt_connected_flag = 1;
            
            // 2. Subscribe Topic điều khiển
            axk_mqtt_client_subscribe(event->client, g_sub_topic, 1);
            printf("[MQTT] Subscribed to: %s\r\n", g_sub_topic);
            
            // 3. Gọi callback (nếu có) để Core đồng bộ trạng thái cửa
            if (g_connect_cb) {
                g_connect_cb();
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("[MQTT] Disconnected. Library will auto-reconnect...\r\n");
            g_mqtt_connected_flag = 0;
            break;

        case MQTT_EVENT_DATA:
            printf("[MQTT] Data Received on topic: %.*s\r\n", event->topic_len, event->topic);
            
            // Copy data nhận được vào buffer tạm thời
            // Lưu ý: data_len có thể lớn, cần cẩn thận tràn bộ nhớ
            char tmp_buf[256] = {0}; 
            int len = event->data_len;
            if (len > 255) len = 255; // Giới hạn kích thước buffer
            
            memcpy(tmp_buf, event->data, len);
            tmp_buf[len] = '\0';

            // Gửi sự kiện về App Task (Main) xử lý JSON
            app_send_event(APP_EVENT_MQTT_DATA_RX, tmp_buf);
            break;

        case MQTT_EVENT_ERROR:
            printf("[MQTT] Event Error (TLS/TCP Check)\r\n");
            break;
            
        default:
            break;
    }
    return AXK_OK;
}

// ============================================================================
// HÀM KHỞI ĐỘNG MQTT (START)
// ============================================================================
void app_mqtt_start(void)
{
    if (mqtt_client != NULL) {
        blog_warn("[MQTT] Already started.");
        return;
    }

    // Buffer lớn để chứa thông tin cấu hình
    char mqtt_broker[128], mqtt_user[64], mqtt_pass[64], mqtt_token_device[64];
    
    // 1. Lấy thông tin từ Flash
    if (!storage_get_mqtt_info(mqtt_broker, mqtt_user, mqtt_pass, mqtt_token_device)) {
        blog_error("[MQTT] No config found in Flash!");
        return;
    }

    // 2. Xây dựng URI kết nối
    char uri[256];
    // Nếu trong Flash đã lưu "mqtt://..." hoặc "mqtts://..." thì dùng luôn
    if (strstr(mqtt_broker, "://") != NULL) {
         snprintf(uri, sizeof(uri), "%s", mqtt_broker);
    } else {
         // Nếu chưa có, mặc định dùng SSL port 8883 cho an toàn
         snprintf(uri, sizeof(uri), "mqtts://%s:8883", mqtt_broker);
    }
    printf("[MQTT] URI: %s\r\n", uri);

    // 3. Xây dựng Topic Subscribe (Nhận lệnh)
    // Format: cmd/COMPANY_CODE/DEVICE_CODE/TOKEN_DEVICE
    snprintf(g_sub_topic, sizeof(g_sub_topic), MQTT_TOPIC_SUB_CMD, 
             MY_COMPANY_CODE,   
             MY_DEVICE_CODE,    
             mqtt_token_device); 

    // 4. Xây dựng Topic Publish (Báo trạng thái)
    // Format: status/COMPANY_CODE/DEVICE_CODE/TOKEN_DEVICE
    snprintf(g_pub_topic, sizeof(g_pub_topic), MQTT_TOPIC_PUB_STATUS, 
             MY_COMPANY_CODE, 
             MY_DEVICE_CODE,
             mqtt_token_device);

    // 5. Cấu hình Client
    axk_mqtt_client_config_t mqtt_cfg = {
        .uri = uri,
        .event_handle = mqtt_event_cb,
        .client_id = mqtt_token_device,  // Dùng Token làm ClientID luôn cho tiện
        .keepalive = MQTT_KEEP_ALIVE,
        .cert_pem = NULL,       
        .client_cert_pem = NULL, 
        .client_key_pem = NULL,  
    };

    // 6. Kiểm tra & Kích hoạt SSL (Nếu dùng mqtts://)
    if (strstr(uri, "mqtts://") != NULL) {
        printf("[MQTT] SSL/TLS Enabled. Loading CA Cert...\r\n");
        mqtt_cfg.cert_pem = emqx_ca_cert; // Gán chứng chỉ CA
    } else {
        printf("[MQTT] Plain MQTT (No SSL).\r\n");
    }

    // 7. Cấu hình Username / Password
    if (strlen(mqtt_user) > 0) {
        mqtt_cfg.username = mqtt_user;
        printf("[MQTT] User: %s\r\n", mqtt_user);
    }
    if (strlen(mqtt_pass) > 0) {
        mqtt_cfg.password = mqtt_pass;
        printf("[MQTT] Pass: [HIDDEN]\r\n");
    }
    
    // 8. Khởi tạo và Kết nối
    mqtt_client = axk_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client) {
        axk_mqtt_client_start(mqtt_client);
    } else {
        printf("[MQTT] Init Failed!\r\n");
    }
}