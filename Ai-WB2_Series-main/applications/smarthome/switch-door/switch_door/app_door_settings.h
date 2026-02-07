#ifndef __APP_DOOR_SETTINGS_H__
#define __APP_DOOR_SETTINGS_H__

// Định nghĩa hàm Callback: Settings gọi ngược lại Core khi đếm đủ số Click
typedef void (*cmd_execute_cb_t)(const char* cmd);

// Khởi tạo Module Settings
void door_settings_init(cmd_execute_cb_t callback);

// Hàm nhận lệnh để xử lý Multi-Click
// Trả về: 1 (Chạy ngay), 0 (Đang đếm click, chờ tí)
int door_settings_process_cmd(const char* cmd);

// Hàm Reset bộ đếm click (Dùng khi bấm STOP)
void door_settings_reset_click(void);

// Hàm Update cấu hình từ MQTT
void door_settings_update_from_mqtt(int open, int close, int def_open, int def_close, int mode, int start, int end);

// Load cấu hình từ Flash
void door_settings_load(void);

#endif