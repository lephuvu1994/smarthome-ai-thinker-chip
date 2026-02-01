#ifndef BLE_INTERFACE_H
#define BLE_INTERFACE_H

#include <stdint.h>

// Định nghĩa kiểu hàm callback: void func(int err)
typedef void (*ble_init_complete_cb_t)(int err);

// Hàm khởi động Stack, nhận vào 1 callback
void ble_stack_start(ble_init_complete_cb_t cb);

// Hàm tắt Stack (nếu cần dùng)
void ble_stack_stop(void);

#endif
