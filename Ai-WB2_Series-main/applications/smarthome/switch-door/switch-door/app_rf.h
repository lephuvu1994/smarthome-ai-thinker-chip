#ifndef APP_RF_H
#define APP_RF_H

#include <stdint.h>

typedef enum {
    RF_CMD_UNKNOWN = 0,
    RF_ACTION_OPEN,
    RF_ACTION_CLOSE,
    RF_ACTION_STOP,
    RF_ACTION_LOCK
} rf_action_t;

typedef void (*rf_event_cb_t)(rf_action_t action);

void app_rf_init(rf_event_cb_t callback);
void app_rf_loop(void);
void app_rf_start_learning(rf_action_t action_type);

// [NEW] Thêm dòng này để Main gọi
void app_rf_start_task(uint32_t stack_size, int priority, rf_event_cb_t callback);

#endif
