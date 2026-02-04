#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include "app_button.h"
#include "app_rf.h"

// Khai báo các hàm cầu nối để Main truyền vào hàm Init của các module
void on_button_event_bridge(btn_event_t event);
void on_rf_event_bridge(rf_action_t action);
void on_mqtt_cmd_bridge(const char* cmd);

#endif
