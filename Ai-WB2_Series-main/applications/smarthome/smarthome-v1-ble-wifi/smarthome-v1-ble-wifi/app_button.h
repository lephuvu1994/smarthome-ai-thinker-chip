#ifndef __APP_BUTTON_H__
#define __APP_BUTTON_H__

// Các sự kiện nút bấm trả về cho Main
typedef enum {
    BTN_EVENT_OPEN,
    BTN_EVENT_CLOSE,
    BTN_EVENT_STOP,
    BTN_EVENT_LOCK_PRESS
} btn_event_t;

// Định nghĩa hàm callback
typedef void (*button_callback_t)(btn_event_t event);

void app_button_init(button_callback_t cb);

#endif
