#ifndef APP_RF_H
#define APP_RF_H

#include <stdint.h>

// [GIỮ NGUYÊN] Enum cũ của bạn
typedef enum {
    RF_CMD_UNKNOWN = 0,
    RF_ACTION_OPEN,
    RF_ACTION_CLOSE,
    RF_ACTION_STOP,
    RF_ACTION_LOCK
} rf_action_t;

// [SỬA NHẸ] Callback nhận Code (uint32_t) và độ rộng xung (int)
// Để Main có thể học được lệnh mới
typedef void (*rf_event_cb_t)(uint32_t code);

// Hàm khởi tạo cũ (giữ để tương thích nếu cần)
void app_rf_init(rf_event_cb_t callback);
void app_rf_loop(void);
void app_rf_start_learning(rf_action_t action_type);

// [HÀM MỚI] Main gọi hàm này
void app_rf_start_task(uint32_t stack_size, int priority, rf_event_cb_t callback);

#endif