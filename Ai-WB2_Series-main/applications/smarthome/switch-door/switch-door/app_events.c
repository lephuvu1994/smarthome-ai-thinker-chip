#include "app_events.h"
#include "app_door_controller_core.h"
#include "app_rf.h"
#include <string.h>
#include <stdio.h>

// 1. Xử lý sự kiện nút bấm
void on_button_event_bridge(btn_event_t event) {
    app_door_controller_core_handle_button_event(event);
}

// 2. Xử lý sự kiện RF
void on_rf_event_bridge(rf_action_t action) {
    switch (action) {
        case RF_ACTION_OPEN:
            app_door_controller_core_execute_cmd_string("open");
            break;
        case RF_ACTION_CLOSE:
            app_door_controller_core_execute_cmd_string("close");
            break;
        case RF_ACTION_STOP:
            app_door_controller_core_execute_cmd_string("stop");
            break;
        case RF_ACTION_LOCK:
            app_door_controller_core_execute_cmd_string("lock");
            break;
        default: break;
    }
}

// 3. Xử lý lệnh MQTT
void on_mqtt_cmd_bridge(const char* cmd) {
    printf("[Bridge] MQTT Cmd: %s\r\n", cmd);

    if (strcmp(cmd, "learn_open") == 0) {
        app_rf_start_learning(RF_ACTION_OPEN);
    }
    else if (strcmp(cmd, "learn_close") == 0) {
        app_rf_start_learning(RF_ACTION_CLOSE);
    }
    else if (strcmp(cmd, "learn_stop") == 0) {
        app_rf_start_learning(RF_ACTION_STOP);
    }
    else if (strcmp(cmd, "learn_lock") == 0) {
        app_rf_start_learning(RF_ACTION_LOCK);
    }
    else {
        app_door_controller_core_execute_cmd_string(cmd);
    }
}
