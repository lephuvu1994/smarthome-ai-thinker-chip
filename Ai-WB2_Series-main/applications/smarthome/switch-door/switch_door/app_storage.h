#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#define KEY_TRAVEL_TIME "cfg_travel_time"
#define MY_MQTT_BROKER_KEY "my_mqtt_broker"
#define MY_MQTT_USER_KEY "my_mqtt_user"
#define MY_MQTT_PASSWORD_KEY "my_mqtt_password"
#define MY_MQTT_TOKEN_DEVICE_KEY "my_mqtt_token_device"

#define KEY_RF_OPEN "rf_code_open"
#define KEY_RF_CLOSE "rf_code_close"
#define KEY_RF_STOP "rf_code_stop"
#define KEY_RF_LOCK "rf_code_lock"

// WIFI
#define MY_WIFI_SSID_KEY "my_wifi_ssid"
#define MY_WIFI_PASSWORD_KEY "my_wifi_password"

// Khởi tạo thư viện EasyFlash
void storage_init(void);

// --- WIFI STORAGE ---

// Lấy thông tin Wifi đã lưu
// Trả về: 1 nếu có Wifi, 0 nếu chưa có
int storage_get_wifi(char *wifi_ssid_out, char *wifi_pass_out);

// Lưu Wifi và tự động khởi động lại chip để áp dụng
void storage_save_wifi_reboot(char* wifi_ssid, char* wifi_pass);

// --- MQTT & PROVISIONING STORAGE (NEW) ---

// Kiểm tra xem thiết bị đã được đăng ký (có thông tin MQTT) chưa
// Trả về: 1 nếu đã có config, 0 nếu chưa
int storage_has_mqtt_config(void);

// Lưu thông tin MQTT lấy được từ API Server
void storage_save_mqtt_info(char* mqtt_broker, char* mqtt_user, char* mqtt_pass, char* mqtt_token_device);

// Lấy thông tin MQTT để kết nối
// Trả về: 1 nếu lấy thành công, 0 nếu lỗi
int storage_get_mqtt_info(char* nqtt_broker_out, char* mqtt_user_out, char* mqtt_pass_out, char* mqtt_token_device_out);

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


// --- [MỚI] RF CODES STORAGE ---
/**
 * @brief Lưu mảng 4 mã RF vào Flash
 * @param codes Mảng chứa 4 phần tử uint32_t (Open, Stop, Close, Lock)
 */
void storage_save_rf_codes(uint32_t *codes);

/**
 * @brief Đọc 4 mã RF từ Flash ra biến
 * @param codes Mảng đầu ra để chứa dữ liệu
 * @return 1 nếu đọc thành công ít nhất 1 mã, 0 nếu chưa có gì
 */
int storage_get_rf_codes(uint32_t *codes);

// [MỚI] CẤU HÌNH CỬA (CLICK & TIME)
void storage_save_door_settings(int open, int close, int def_open, int def_close, int mode, int start, int end);
void storage_get_door_settings(int *open, int *close, int *def_open, int *def_close, int *mode, int *start, int *end);

#endif
