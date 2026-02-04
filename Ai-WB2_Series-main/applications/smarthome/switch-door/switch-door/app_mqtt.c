#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>

// --- THƯ VIỆN SDK ---
#include <mqtt_client.h> 
#include "blog.h"
#include <cJSON.h>

// --- MODULES ---
#include "app_conf.h"
#include "app_storage.h"
#include "app_mqtt.h"

static axk_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_callback_t g_mqtt_cb = NULL;

// Các buffer lưu thông tin
static char g_topic_sub[128];
static char g_topic_pub[128];
static char g_token[64] = {0}; // Token dùng cho Topic

void app_mqtt_init(mqtt_callback_t cb) {
    g_mqtt_cb = cb;
}

void app_mqtt_pub_status(const char* status) {
    if (mqtt_client == NULL) return;
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
    axk_mqtt_client_publish(mqtt_client, g_topic_pub, payload, 0, MQTT_QOS, 0);
}

// --- XỬ LÝ DATA ---
static void handle_mqtt_data(const char *topic, const char *data, int len) {
    if (len <= 0) return;

    // 1. Tạo buffer tạm và thêm \0
    char *payload = pvPortMalloc(len + 1);
    if (!payload) return;

    memcpy(payload, data, len);
    payload[len] = '\0'; // Đây là "chốt chặn" để printf không in ra rác

    // 2. IN LOG SAU KHI ĐÃ CÓ PAYLOAD SẠCH
    // Thay vì in 'data', hãy in 'payload'
    printf("[MQTT] Topic: %s\r\n", topic);
    printf("[MQTT] Payload: %s\r\n", payload);

    // 3. Parse JSON
    cJSON *root = cJSON_Parse(payload);
    if (root) {
        cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
        if (cmd_item && cmd_item->type == cJSON_String) {
            printf("[MQTT] Lenh thuc thi: %s\r\n", cmd_item->valuestring);
            if (g_mqtt_cb != NULL) {
                g_mqtt_cb(cmd_item->valuestring);
            }
        }
        cJSON_Delete(root);
    }

    // 4. Giải phóng bộ nhớ
    vPortFree(payload);
}

// --- EVENT HANDLER ---
static axk_err_t mqtt_event_handler(axk_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("[MQTT] Connected!\r\n");
            
            // Subscribe vào Topic chứa Token
            int msg_id = axk_mqtt_client_subscribe(event->client, g_topic_sub, MQTT_QOS);
            printf("[MQTT] Subscribed: %s (QoS=%d)\r\n", g_topic_sub, MQTT_QOS);
            
            app_mqtt_pub_status("ONLINE");
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("[MQTT] Disconnected!\r\n");
            break;

        case MQTT_EVENT_DATA:
            handle_mqtt_data(event->topic, event->data, event->data_len);
            break;
            
        case MQTT_EVENT_ERROR:
            printf("[MQTT] Error!\r\n");
            break;
        default: break;
    }
    return AXK_OK;
}

// --- START FUNCTION ---
void app_mqtt_start(void) {
    // Khai báo bộ đệm để chứa dữ liệu lấy từ Flash
    char broker[64], user[64], pass[64];
    
    // 1. Lấy thông tin từ Storage (Hàm này sẽ đọc NVS/Flash)
    if (!storage_get_mqtt_info(broker, user, pass, g_token)) {
        printf("[MQTT] No Config in Storage!\r\n");
        return;
    }

    // 2. Tạo Topic dựa trên Token vừa lấy được
    // Ví dụ: device/TOKEN/command
    snprintf(g_topic_sub, sizeof(g_topic_sub), "device/%s/cmd", g_token);
    snprintf(g_topic_pub, sizeof(g_topic_pub), "device/%s/status", g_token);

    // 3. Xử lý URI (Địa chỉ Server)
    char uri[128];
    if (strstr(broker, "mqtt://") == NULL) {
         snprintf(uri, sizeof(uri), "mqtt://%s:1883", broker);
    } else {
         strncpy(uri, broker, sizeof(uri));
    }

    // 4. Khởi tạo cấu hình MQTT
    axk_mqtt_client_config_t mqtt_cfg = {
        .uri = uri,
        .event_handle = mqtt_event_handler,
        .client_id = g_token,       // Thường dùng token làm ClientID duy nhất
        .keepalive = 60,
    };
    
    // SỬ DỤNG USER/PASS ĐÃ LẤY TỪ FLASH:
    if (strlen(user) > 0) {
        mqtt_cfg.username = user;
        printf("[MQTT] Use Username: %s\r\n", user);
    }
    if (strlen(pass) > 0) {
        mqtt_cfg.password = pass;
        printf("[MQTT] Password set (hidden)\r\n");
    }

    // 5. Khởi động Client
    mqtt_client = axk_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client) {
        axk_mqtt_client_start(mqtt_client);
    }
}
