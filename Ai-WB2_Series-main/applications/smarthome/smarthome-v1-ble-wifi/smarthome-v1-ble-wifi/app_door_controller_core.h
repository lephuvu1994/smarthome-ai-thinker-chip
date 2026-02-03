#ifndef __APP_DOOR_CONTROLLER_CORE_H__
#define __APP_DOOR_CONTROLLER_CORE_H__

#include "app_button.h" // Để biết kiểu dữ liệu btn_event_t

// Khởi tạo logic cửa
void app_door_controller_core_init(void);

// Xử lý lệnh từ chuỗi (Dùng cho MQTT)
// VD: cmd = "OPEN", "CLOSE"
void app_door_controller_core_execute_cmd_string(const char* cmd);

// Xử lý sự kiện từ nút bấm (Dùng cho Button Task)
void app_door_controller_core_handle_button_event(btn_event_t event);

#endif