#ifndef __APP_DOOR_CONTROLLER_CORE_H__
#define __APP_DOOR_CONTROLLER_CORE_H__

#include <stdint.h>

#include "../switch_door/app_button.h" // Để biết kiểu dữ liệu btn_event_t

typedef enum {
   DOOR_STATE_STOPPED = 0,
   DOOR_STATE_OPENING,
   DOOR_STATE_CLOSING,
   DOOR_STATE_OPENED,
   DOOR_STATE_CLOSED
} door_state_t;

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

void app_door_core_handle_rf_raw(uint32_t rf_code);

/**
 * @brief Xử lý tin nhắn từ MQTT cho setup số lần bấm
 */
void app_door_core_update_settings(int open, int close, int def_open, int def_close, int mode, int start, int end);

#endif // __APP_DOOR_CONTROLLER_CORE_H__
