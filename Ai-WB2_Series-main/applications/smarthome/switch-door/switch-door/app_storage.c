#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <easyflash.h>
#include <bl_sys.h>
#include <FreeRTOS.h>
#include <task.h>
#include "app_storage.h"

// Hàm cắt khoảng trắng thừa ở đầu và cuối chuỗi (tránh lỗi copy paste)
static void trim_str(char *str) {
    if (str == NULL) return;
    char *p = str;
    int l = strlen(p);
    while(l > 0 && (isspace((unsigned char)p[l - 1]) || p[l - 1] == '\r' || p[l - 1] == '\n')) p[--l] = 0;
    while(*p && (isspace((unsigned char)*p) || *p == '\r' || *p == '\n')) p++;
    if(p != str) memmove(str, p, strlen(p) + 1);
}

// --- INIT ---

void storage_init(void) {
    easyflash_init();
    printf("[STORAGE] EasyFlash Initialized.\r\n");
}

// --- WIFI FUNCTIONS ---

int storage_get_wifi(char *wifi_ssid_out, char *wifi_pass_out) {
	// 1. Xóa sạch bộ đệm đích trước
	    if(wifi_ssid_out) memset(wifi_ssid_out, 0, 33);
	    if(wifi_pass_out) memset(wifi_pass_out, 0, 64);

	    // 2. LẤY SSID VÀ COPY NGAY LẬP TỨC
	    // Phải copy xong mới được gọi ef_get_env lần nữa
	    char *temp_ssid = ef_get_env(MY_WIFI_SSID_KEY);

	    if (temp_ssid && strlen(temp_ssid) > 0) {
	        strcpy(wifi_ssid_out, temp_ssid); // <--- Copy ngay tại đây!
	    } else {
	        return 0; // Không có SSID thì trả về 0 luôn
	    }

	    // 3. LẤY PASSWORD SAU KHI ĐÃ COPY SSID XONG
	    char *temp_pass = ef_get_env(MY_WIFI_PASSWORD_KEY);
	    if (temp_pass && strlen(temp_pass) > 0) {
	        strcpy(wifi_pass_out, temp_pass);
	    }

	    // Debug log để kiểm tra
	    printf("[STORAGE] Read: SSID=[%s], PASS=[%s]\r\n", wifi_ssid_out, wifi_pass_out);

	    return 1;
}

void storage_save_wifi_reboot(char* wifi_ssid, char* wifi_pass) {
    trim_str(wifi_ssid);
    trim_str(wifi_pass);

    printf(">>> [STORAGE] Saving Wifi: [%s] -> REBOOTING...\r\n", wifi_ssid);

    ef_set_env(MY_WIFI_SSID_KEY, wifi_ssid);
    ef_set_env(MY_WIFI_PASSWORD_KEY, wifi_pass);
    ef_save_env(); // Ghi xuống Flash ngay lập tức

    vTaskDelay(500); // Đợi Flash ghi xong
    bl_sys_reset_por(); // Reset chip
}

// --- MQTT & PROVISIONING FUNCTIONS (NEW) ---

int storage_has_mqtt_config(void) {
    // Kiểm tra xem Broker URL có tồn tại không
    char *val = ef_get_env(MY_MQTT_BROKER_KEY);
    if (val && strlen(val) > 0) {
        return 1; // Đã có cấu hình
    }
    return 0; // Chưa có
}

void storage_save_mqtt_info(char* mqtt_broker, char* mqtt_user, char* mqtt_pass, char* mqtt_token_device) {
    // Clean input
    trim_str(mqtt_broker);
    trim_str(mqtt_user);
    trim_str(mqtt_pass);
    trim_str(mqtt_token_device);

    printf("[STORAGE] Saving MQTT Config:\r\n");
    printf(" - Broker: %s\r\n", mqtt_broker);
    printf(" - User:   %s\r\n", mqtt_user);
    // Không in pass/token ra log để bảo mật

    ef_set_env(MY_MQTT_BROKER_KEY, mqtt_broker);
    ef_set_env(MY_MQTT_USER_KEY, mqtt_user);
    ef_set_env(MY_MQTT_PASSWORD_KEY, mqtt_pass);
    ef_set_env(MY_MQTT_TOKEN_DEVICE_KEY, mqtt_token_device);

    ef_save_env();
    printf("[STORAGE] MQTT Save Done.\r\n");
}

int storage_get_mqtt_info(char* mqtt_broker_out, char* mqtt_user_out, char* mqtt_pass_out, char* mqtt_token_device_out) {
    char *val;

    // 1. Get Broker
    val = ef_get_env(MY_MQTT_BROKER_KEY);
    if (val && strlen(val) > 0) {
        if(mqtt_broker_out) strcpy(mqtt_broker_out, val);
    } else {
        return 0; // Bắt buộc phải có Broker
    }

    // 2. Get User
    val = ef_get_env(MY_MQTT_USER_KEY);
    if (val && mqtt_user_out) strcpy(mqtt_user_out, val);

    // 3. Get Pass
    val = ef_get_env(MY_MQTT_PASSWORD_KEY);
    if (val && mqtt_pass_out) strcpy(mqtt_pass_out, val);

    // 4. Get Token
    val = ef_get_env(MY_MQTT_TOKEN_DEVICE_KEY);
    if (val && mqtt_token_device_out) strcpy(mqtt_token_device_out, val);

    return 1; // Lấy thành công
}

// Hàm lấy thời gian
void storage_save_travel_time(uint32_t time_ms)
{
    char str_buf[16] = {0};

    // 1. Chuyển số thành chuỗi (EasyFlash lưu string)
    snprintf(str_buf, sizeof(str_buf), "%lu", time_ms);

    // 2. Ghi vào biến môi trường (Environment Variable)
    ef_set_env(KEY_TRAVEL_TIME, str_buf);

    // 3. Lưu xuống Flash ngay lập tức
    ef_save_env();

    printf("[STORAGE] Saved Travel Time: %s ms\r\n", str_buf);
}

int storage_get_travel_time(uint32_t *out_time_ms)
{
    // 1. Đọc giá trị từ Flash bằng Key
    char *value = ef_get_env(KEY_TRAVEL_TIME);

    // 2. Kiểm tra xem có dữ liệu không
    if (value == NULL) {
        printf("[STORAGE] No saved travel time found.\r\n");
        return 0; // Trả về 0 (False) để Core dùng giá trị mặc định
    }

    // 3. Nếu có, chuyển chuỗi thành số và gán vào con trỏ
    *out_time_ms = (uint32_t)atoi(value);
    
    printf("[STORAGE] Loaded Travel Time: %lu ms\r\n", *out_time_ms);
    return 1; // Trả về 1 (True)
}

// -------------------------------------------------------------------------
// HÀM LƯU RF CODES
// -------------------------------------------------------------------------
void storage_save_rf_codes(uint32_t *codes) {
    if (codes == NULL) return;

    char val_str[16]; // Buffer để chứa chuỗi số
    
    // 1. Lưu Code OPEN
    memset(val_str, 0, sizeof(val_str));
    sprintf(val_str, "%lu", (unsigned long)codes[0]); // Chuyển số thành chuỗi
    ef_set_env(KEY_RF_OPEN, val_str);

    // 2. Lưu Code STOP
    memset(val_str, 0, sizeof(val_str));
    sprintf(val_str, "%lu", (unsigned long)codes[1]);
    ef_set_env(KEY_RF_STOP, val_str);

    // 3. Lưu Code CLOSE
    memset(val_str, 0, sizeof(val_str));
    sprintf(val_str, "%lu", (unsigned long)codes[2]);
    ef_set_env(KEY_RF_CLOSE, val_str);

    // 4. Lưu Code LOCK
    memset(val_str, 0, sizeof(val_str));
    sprintf(val_str, "%lu", (unsigned long)codes[3]);
    ef_set_env(KEY_RF_LOCK, val_str);

    // Commit xuống Flash ngay lập tức
    ef_save_env();
    
    printf("[STORAGE] RF Codes Saved: [%lu] [%lu] [%lu] [%lu]\r\n", 
           (unsigned long)codes[0], (unsigned long)codes[1], 
           (unsigned long)codes[2], (unsigned long)codes[3]);
}

// -------------------------------------------------------------------------
// HÀM ĐỌC RF CODES
// -------------------------------------------------------------------------
int storage_get_rf_codes(uint32_t *codes) {
    if (codes == NULL) return 0;

    // Mặc định gán bằng 0 hết trước
    codes[0] = 0;
    codes[1] = 0;
    codes[2] = 0;
    codes[3] = 0;

    char *val_str;
    int found_count = 0;

    // 1. Đọc Code OPEN
    val_str = ef_get_env(KEY_RF_OPEN);
    if (val_str) {
        codes[0] = (uint32_t)strtoul(val_str, NULL, 10); // Chuyển chuỗi về số
        found_count++;
    }

    // 2. Đọc Code STOP
    val_str = ef_get_env(KEY_RF_STOP);
    if (val_str) {
        codes[1] = (uint32_t)strtoul(val_str, NULL, 10);
        found_count++;
    }

    // 3. Đọc Code CLOSE
    val_str = ef_get_env(KEY_RF_CLOSE);
    if (val_str) {
        codes[2] = (uint32_t)strtoul(val_str, NULL, 10);
        found_count++;
    }

    // 4. Đọc Code LOCK
    val_str = ef_get_env(KEY_RF_LOCK);
    if (val_str) {
        codes[3] = (uint32_t)strtoul(val_str, NULL, 10);
        found_count++;
    }

    printf("[STORAGE] RF Codes Loaded: [%lu] [%lu] [%lu] [%lu]\r\n", 
           (unsigned long)codes[0], (unsigned long)codes[1], 
           (unsigned long)codes[2], (unsigned long)codes[3]);

    return (found_count > 0); // Trả về 1 nếu có dữ liệu
}
