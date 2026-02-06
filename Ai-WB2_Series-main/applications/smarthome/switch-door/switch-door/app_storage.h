#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#define KEY_TRAVEL_TIME "cfg_travel_time"

// Khởi tạo thư viện EasyFlash
void storage_init(void);

// --- WIFI STORAGE ---

// Lấy thông tin Wifi đã lưu
// Trả về: 1 nếu có Wifi, 0 nếu chưa có
int storage_get_wifi(char *ssid_out, char *pass_out);

// Lưu Wifi và tự động khởi động lại chip để áp dụng
void storage_save_wifi_reboot(char* ssid, char* pass);

// --- MQTT & PROVISIONING STORAGE (NEW) ---

// Kiểm tra xem thiết bị đã được đăng ký (có thông tin MQTT) chưa
// Trả về: 1 nếu đã có config, 0 nếu chưa
int storage_has_mqtt_config(void);

// Lưu thông tin MQTT lấy được từ API Server
void storage_save_mqtt_info(char* broker, char* user, char* pass, char* token);

// Lấy thông tin MQTT để kết nối
// Trả về: 1 nếu lấy thành công, 0 nếu lỗi
int storage_get_mqtt_info(char* broker_out, char* user_out, char* pass_out, char* token_out);

/**
 * @brief Lưu thời gian hành trình vào bộ nhớ Flash
 * @param time_ms: Thời gian tính bằng mili giây
 */
 void storage_save_travel_time(uint32_t time_ms);

 /**
  * @brief Lấy thời gian hành trình từ Flash
  * @param out_time_ms: Con trỏ để nhận giá trị thời gian đọc được
  * @return int: 1 nếu tìm thấy dữ liệu hợp lệ, 0 nếu chưa có dữ liệu
  */
 int storage_get_travel_time(uint32_t *out_time_ms);
#endif
