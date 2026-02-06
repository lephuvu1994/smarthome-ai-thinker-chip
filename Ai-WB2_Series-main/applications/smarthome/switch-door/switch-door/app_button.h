#ifndef __APP_BUTTON_H__
#define __APP_BUTTON_H__

// Danh sách các sự kiện nút bấm
typedef enum {
    BTN_EVENT_OPEN,
    BTN_EVENT_CLOSE,
    BTN_EVENT_STOP,
    BTN_EVENT_LOCK_PRESS,
    BTN_EVENT_LEARN_TRAVEL_TRIGGER, // Hoc hanhf trình
    BTN_EVENT_LEARN_RF_TRIGGER, // Hoc RF,
    BTN_EVENT_WIFI_RESET_TRIGGER
} btn_event_t;

// Định nghĩa hàm Callback
typedef void (*button_callback_t)(btn_event_t event);

/**
 * @brief Khởi tạo module nút bấm
 * Hàm này sẽ tự động:
 * 1. Cấu hình GPIO Input (Pull-up)
 * 2. Tạo Task Polling chạy ngầm
 * * @param cb Hàm nhận sự kiện trả về Main
 */
void app_button_init(button_callback_t cb);

#endif