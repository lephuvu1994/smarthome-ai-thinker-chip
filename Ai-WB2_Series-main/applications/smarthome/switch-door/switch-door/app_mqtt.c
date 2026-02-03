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
    char *payload = pvPortMalloc(len + 1);
    if (!payload) return;
    memcpy(payload, data, len);
    payload[len] = '\0';

    printf("[MQTT] Recv on %s: %s\r\n", topic, payload);

    cJSON *root = cJSON_Parse(payload);
    if (root) {
        cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
        if (cmd_item && cmd_item->type == cJSON_String) {
             if (g_mqtt_cb != NULL) {
                g_mqtt_cb(cmd_item->valuestring); // Báo về Main
            }
        }
        cJSON_Delete(root);
    } 
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
    char broker[64], user[32], pass[64];
    
    // 1. Lấy thông tin từ Flash
    // Hàm này giả định trả về: Broker URL, Username, Password, và Token
    if (!storage_get_mqtt_info(broker, user, pass, g_token)) {
        printf("[MQTT] No Config in Storage!\r\n");
        return;
    }

    // 2. Tạo Topic từ TOKEN (Theo yêu cầu của bạn)
    snprintf(g_topic_sub, sizeof(g_topic_sub), MQTT_TOPIC_SUB_CMD, g_token);
    snprintf(g_topic_pub, sizeof(g_topic_pub), MQTT_TOPIC_PUB_STATUS, g_token);

    // 3. Tạo URI
    char uri[128];
    // Nếu broker lưu trong flash chưa có prefix thì thêm vào
    if (strstr(broker, "mqtt://") == NULL) {
         sprintf(uri, "mqtt://%s:1883", broker); 
    } else {
         strcpy(uri, broker);
    }
    
    printf("[MQTT] URI: %s\r\n", uri);
    printf("[MQTT] User: %s, Token (Topic ID): %s\r\n", user, g_token);

    // 4. Config Client
    axk_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.uri = uri;
    mqtt_cfg.event_handle = mqtt_event_handler;
    mqtt_cfg.client_id = g_token; // Dùng Token làm ClientID luôn cho tiện trùng khớp
    mqtt_cfg.keepalive = MQTT_KEEP_ALIVE;
    
    // Auth bằng Username / Password thật
    if (strlen(user) > 0) mqtt_cfg.username = user;
    if (strlen(pass) > 0) mqtt_cfg.password = pass; 
    
    // 5. Start
    mqtt_client = axk_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client) {
        axk_mqtt_client_start(mqtt_client);
    }
}
