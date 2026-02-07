#ifndef __APP_BLE_H__
#define __APP_BLE_H__

#include <stdint.h>

/**
 * @brief Khởi tạo Stack BLE (Controller, HCI, Enable)
 */
void app_ble_init(void);

/**
 * @brief Bật quảng bá (Advertising) để điện thoại tìm thấy
 */
void app_ble_start_adv(void);

/**
 * @brief Tắt quảng bá (Tiết kiệm điện hoặc khi đã có WiFi)
 */
void app_ble_stop_adv(void);

/**
 * @brief Cập nhật dữ liệu quảng bá (Dùng cho tính năng Sync Offline)
 * @param status_json Chuỗi JSON trạng thái cửa (vd: {"state":"OPEN"})
 */
void app_ble_set_adv_data(const char* status_json);

#endif