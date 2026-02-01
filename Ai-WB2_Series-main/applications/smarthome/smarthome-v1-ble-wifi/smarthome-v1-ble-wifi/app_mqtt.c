#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>

// --- THƯ VIỆN CỦA SDK (Theo mẫu demo.c) ---
#include <mqtt_client.h> 
#include "blog.h"

// --- MODULES CỦA CHÚNG TA ---
#include "app_conf.h"
#include "app_storage.h"
#include "app_led.h"
#include <cJSON.h>

static axk_mqtt_client_handle_t mqtt_client = NULL;
static char g_token[64] = {0};

// --- HÀM XỬ LÝ DỮ LIỆU ĐẾN ---
static void handle_mqtt_data(const char *topic, const char *data, int len) {
    // Copy data vào buffer có null-terminator để an toàn
    char *payload = pvPortMalloc(len + 1);
    if (!payload) return;
    memcpy(payload, data, len);
    payload[len] = '\0';

    printf("[MQTT] Received on %s: %s\r\n", topic, payload);

    // 1. Parse JSON: {"led": 1} hoặc {"led": 0} hoặc {"switch": "on"}
    cJSON *root = cJSON_Parse(payload);
    if (root) {
        cJSON *led = cJSON_GetObjectItem(root, "led");
        cJSON *sw  = cJSON_GetObjectItem(root, "switch");

        // Logic điều khiển LED
        if (led && led->type == cJSON_Number) {
            if (led->valueint == 1) led_set_mode(LED_ON_MODE); // Bật
            else led_set_mode(LED_BLINK_SLOW); // Nháy chậm (chế độ chờ)
        } 
        // [FIX] Sửa lỗi cJSON_IsString bằng cách check thủ công
        else if (sw && sw->type == cJSON_String) {
            if (strcasecmp(sw->valuestring, "on") == 0) led_set_mode(LED_ON_MODE);
            else led_set_mode(LED_BLINK_SLOW);
        }
        
        cJSON_Delete(root);
    } else {
        // Fallback: Xử lý chuỗi thô nếu không phải JSON
        if (strncasecmp(payload, "ON", 2) == 0) led_set_mode(LED_ON_MODE);
        else if (strncasecmp(payload, "OFF", 3) == 0) led_set_mode(LED_BLINK_SLOW);
    }

    vPortFree(payload);
}

// --- EVENT CALLBACK (Theo chuẩn axk_mqtt) ---
static axk_err_t mqtt_event_handler(axk_mqtt_event_handle_t event) {
    axk_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("[MQTT] Connected! Subscribing...\r\n");
            
            // Tạo Topic: device/TOKEN/cmd
            char topic_sub[128];
            sprintf(topic_sub, "device/%s/cmd", g_token);
            
            // Subscribe QoS 0
            msg_id = axk_mqtt_client_subscribe(client, topic_sub, 0);
            printf("[MQTT] Sent Subscribe %s, msg_id=%d\r\n", topic_sub, msg_id);
            
            // Gửi thông báo online: device/TOKEN/status
            char topic_pub[128];
            sprintf(topic_pub, "device/%s/status", g_token);
            axk_mqtt_client_publish(client, topic_pub, "{\"status\":\"online\"}", 0, 0, 0);
            
            led_set_mode(LED_ON_MODE); // Đèn sáng đứng báo hiệu đã có mạng + mqtt
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("[MQTT] Disconnected!\r\n");
            // Có thể cho đèn nháy nhanh để báo mất kết nối
            led_set_mode(LED_BLINK_FAST);
            break;

        case MQTT_EVENT_DATA:
            printf("[MQTT] Data Received\r\n");
            // event->topic và event->data không có null-terminator, cần xử lý cẩn thận
            // Tuy nhiên hàm handle của ta đã lo việc malloc copy rồi
            handle_mqtt_data(event->topic, event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            printf("[MQTT] Error occurred\r\n");
            break;
            
        default:
            break;
    }
    return AXK_OK;
}

// --- HÀM START ---
void app_mqtt_start(void) {
    char broker[64], user[32], pass[64];
    
    // 1. Lấy thông tin từ Flash
    if (!storage_get_mqtt_info(broker, user, pass, g_token)) {
        printf("[MQTT] No Config Found in Storage!\r\n");
        return;
    }

    // 2. Tạo URI kết nối
    // axk_mqtt yêu cầu URI dạng: "mqtt://IP:PORT"
    char uri[128];
    // Giả sử broker lưu trong flash là "192.168.1.10" -> thêm prefix
    sprintf(uri, "mqtt://%s:1883", broker); 

    printf("[MQTT] Config: URI=%s, User=%s, Token=%s\r\n", uri, user, g_token);

    // 3. Cấu hình Client
    axk_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.uri = uri;
    mqtt_cfg.event_handle = mqtt_event_handler;
    
    // Nếu có user/pass
    if (strlen(user) > 0) mqtt_cfg.username = user;
    if (strlen(pass) > 0) mqtt_cfg.password = pass;
    
    mqtt_cfg.keepalive = 60;
    mqtt_cfg.client_id = g_token; // Dùng Token làm ClientID luôn cho tiện

    // 4. Init & Start
    mqtt_client = axk_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client) {
        axk_mqtt_client_start(mqtt_client);
    } else {
        printf("[MQTT] Init Failed!\r\n");
    }
}