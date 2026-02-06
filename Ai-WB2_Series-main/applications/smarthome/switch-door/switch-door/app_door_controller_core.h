#ifndef __APP_DOOR_CONTROLLER_CORE_H__
#define __APP_DOOR_CONTROLLER_CORE_H__

#include <stdint.h>
#include "app_button.h" // Để biết kiểu dữ liệu btn_event_t

// ==============================================================================
// DANH SÁCH LỆNH (COMMAND LIST)
// ==============================================================================
/* Các lệnh String hỗ trợ cho hàm app_door_controller_core_execute_cmd_string:
   
   1. "OPEN"          : Mở cửa (Relay giữ mức 1 đến khi hết giờ đã học hoặc bấm STOP)
   2. "CLOSE"         : Đóng cửa (Relay giữ mức 1 đến khi hết giờ đã học hoặc bấm STOP)
   3. "STOP"          : Dừng mọi Relay ngay lập tức (Lưu thời gian nếu đang ở chế độ học)
   4. "LOCK"          : Khóa cửa (Ngắt toàn bộ, vô hiệu hóa lệnh OPEN/CLOSE)
   5. "UNLOCK"        : Mở khóa (Cho phép điều khiển lại)
   6. "LEARN_MODE_ON" : Bật chế độ học hành trình (Đo thời gian mở/đóng thực tế)
*/

// ==============================================================================
// PUBLIC API
// ==============================================================================

/**
 * @brief Khởi tạo logic điều khiển cửa
 * - Load thời gian hành trình từ Flash (nếu có)
 * - Khởi tạo Timer đếm ngược (Auto-stop)
 * - Reset trạng thái khóa
 */
void app_door_controller_core_init(void);

/**
 * @brief Xử lý lệnh điều khiển dạng chuỗi (Dùng cho MQTT hoặc BLE)
 * @param cmd Chuỗi lệnh (Xem danh sách lệnh bên trên)
 */
void app_door_controller_core_execute_cmd_string(const char* cmd);

/**
 * @brief Xử lý sự kiện từ nút bấm vật lý (Dùng cho Button Task)
 * - Ánh xạ sự kiện nút bấm sang lệnh tương ứng
 * @param event Sự kiện: BTN_EVENT_OPEN, BTN_EVENT_CLOSE, BTN_EVENT_STOP...
 */
void app_door_controller_core_handle_button_event(btn_event_t event);

#endif // __APP_DOOR_CONTROLLER_CORE_H__