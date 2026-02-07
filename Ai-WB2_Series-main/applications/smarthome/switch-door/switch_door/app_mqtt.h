#ifndef __APP_MQTT_H__
#define __APP_MQTT_H__

#ifdef __cplusplus
extern "C" {
#endif

// 1. Định nghĩa kiểu Callback
typedef void (*mqtt_callback_t)(const char* cmd_str);

// 2. Khai báo các hàm (Prototype) để các file khác gọi được
void app_mqtt_init(mqtt_callback_t cb);

void app_mqtt_start(void);

// ---> DÒNG NÀY ĐANG BỊ THIẾU, BẠN CẦN THÊM VÀO <---
void app_mqtt_pub_status(const char* status);

int app_mqtt_is_connected(void);
typedef void (*mqtt_connect_cb_t)(void);
void app_mqtt_on_connect(mqtt_connect_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MQTT_H__ */
