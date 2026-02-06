#ifndef __APP_EVENTS_H__
#define __APP_EVENTS_H__

#include <stdint.h>
#include <string.h>
#include <FreeRTOS.h>
#include <queue.h>

// Các loại sự kiện hệ thống
typedef enum {
    // Sự kiện Wifi
    APP_EVENT_WIFI_CONNECTED,       // Đã có IP
    APP_EVENT_WIFI_DISCONNECTED,    // Mất kết nối
    APP_EVENT_WIFI_FATAL_ERROR,     // Lỗi không thể hồi phục -> Bật BLE

    // Sự kiện MQTT
    APP_EVENT_MQTT_DATA_RX,         // Nhận lệnh từ Server (OPEN/CLOSE...)
    
    // Sự kiện khác
    APP_EVENT_BLE_CONFIG_DONE       // Cấu hình xong
} app_event_type_t;

// Cấu trúc bản tin trong Queue
typedef struct {
    app_event_type_t type;
    char data[64]; // Buffer chứa dữ liệu (VD: lệnh MQTT, SSID...)
} app_msg_t;

// Khai báo Queue toàn cục (định nghĩa thực tế nằm ở main.c)
extern QueueHandle_t g_app_queue;

// Hàm gửi sự kiện an toàn (Dùng được trong cả ngắt và task thường)
static inline void app_send_event(app_event_type_t type, const char* str_data) {
    if (g_app_queue != NULL) {
        app_msg_t msg;
        msg.type = type;
        if (str_data) {
            strncpy(msg.data, str_data, sizeof(msg.data) - 1);
            msg.data[sizeof(msg.data) - 1] = '\0';
        } else {
            msg.data[0] = '\0';
        }
        // Gửi không chờ (Non-blocking)
        xQueueSend(g_app_queue, &msg, 0);
    }
}

#endif /* __APP_EVENTS_H__ */