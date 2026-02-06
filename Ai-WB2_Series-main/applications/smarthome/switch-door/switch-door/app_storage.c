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

int storage_get_wifi(char *ssid_out, char *pass_out) {
	// 1. Xóa sạch bộ đệm đích trước
	    if(ssid_out) memset(ssid_out, 0, 33);
	    if(pass_out) memset(pass_out, 0, 64);

	    // 2. LẤY SSID VÀ COPY NGAY LẬP TỨC
	    // Phải copy xong mới được gọi ef_get_env lần nữa
	    char *temp_ssid = ef_get_env("my_ssid");

	    if (temp_ssid && strlen(temp_ssid) > 0) {
	        strcpy(ssid_out, temp_ssid); // <--- Copy ngay tại đây!
	    } else {
	        return 0; // Không có SSID thì trả về 0 luôn
	    }

	    // 3. LẤY PASSWORD SAU KHI ĐÃ COPY SSID XONG
	    char *temp_pass = ef_get_env("my_pass");
	    if (temp_pass && strlen(temp_pass) > 0) {
	        strcpy(pass_out, temp_pass);
	    }

	    // Debug log để kiểm tra
	    printf("[STORAGE] Read: SSID=[%s], PASS=[%s]\r\n", ssid_out, pass_out);

	    return 1;
}

void storage_save_wifi_reboot(char* ssid, char* pass) {
    trim_str(ssid);
    trim_str(pass);

    printf(">>> [STORAGE] Saving Wifi: [%s] -> REBOOTING...\r\n", ssid);

    ef_set_env("my_ssid", ssid);
    ef_set_env("my_pass", pass);
    ef_save_env(); // Ghi xuống Flash ngay lập tức

    vTaskDelay(500); // Đợi Flash ghi xong
    bl_sys_reset_por(); // Reset chip
}

// --- MQTT & PROVISIONING FUNCTIONS (NEW) ---

int storage_has_mqtt_config(void) {
    // Kiểm tra xem Broker URL có tồn tại không
    char *val = ef_get_env("mqtt_broker");
    if (val && strlen(val) > 0) {
        return 1; // Đã có cấu hình
    }
    return 0; // Chưa có
}

void storage_save_mqtt_info(char* broker, char* user, char* pass, char* token) {
    // Clean input
    trim_str(broker);
    trim_str(user);
    trim_str(pass);
    trim_str(token);

    printf("[STORAGE] Saving MQTT Config:\r\n");
    printf(" - Broker: %s\r\n", broker);
    printf(" - User:   %s\r\n", user);
    // Không in pass/token ra log để bảo mật

    ef_set_env("mqtt_broker", broker);
    ef_set_env("mqtt_user", user);
    ef_set_env("mqtt_pass", pass);
    ef_set_env("device_token", token);

    ef_save_env();
    printf("[STORAGE] MQTT Save Done.\r\n");
}

int storage_get_mqtt_info(char* broker_out, char* user_out, char* pass_out, char* token_out) {
    char *val;

    // 1. Get Broker
    val = ef_get_env("mqtt_broker");
    if (val && strlen(val) > 0) {
        if(broker_out) strcpy(broker_out, val);
    } else {
        return 0; // Bắt buộc phải có Broker
    }

    // 2. Get User
    val = ef_get_env("mqtt_user");
    if (val && user_out) strcpy(user_out, val);

    // 3. Get Pass
    val = ef_get_env("mqtt_pass");
    if (val && pass_out) strcpy(pass_out, val);

    // 4. Get Token
    val = ef_get_env("device_token");
    if (val && token_out) strcpy(token_out, val);

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
