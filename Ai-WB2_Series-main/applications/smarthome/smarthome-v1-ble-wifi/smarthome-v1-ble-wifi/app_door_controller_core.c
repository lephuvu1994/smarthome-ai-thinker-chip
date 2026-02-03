#include <stdio.h>
#include <string.h>
#include "app_door_controller_core.h"
#include "app_output_relay.h"  // Gọi xuống tầng Driver Relay
#include "app_mqtt.h"   // Gọi sang tầng MQTT để báo status

static int g_lock_active = 0; // Biến trạng thái khóa (Private)

void app_door_controller_core_init(void) {
    // Logic khởi tạo nếu cần (ví dụ đọc trạng thái khóa từ Flash)
    g_lock_active = 0;
}

// Hàm xử lý trung tâm (Core Logic)
void app_door_controller_core_execute_cmd_string(const char* cmd) {
    printf("[CORE] Executing: %s\r\n", cmd);

    if (strcmp(cmd, "OPEN") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("LOCKED_ERR");
        } else {
            app_relay_pulse_open();
            app_mqtt_pub_status("OPENING");
        }
    }
    else if (strcmp(cmd, "CLOSE") == 0) {
        if (g_lock_active) {
            app_mqtt_pub_status("LOCKED_ERR");
        } else {
            app_relay_pulse_close();
            app_mqtt_pub_status("CLOSING");
        }
    }
    else if (strcmp(cmd, "STOP") == 0) {
        app_relay_pulse_stop();
        app_mqtt_pub_status("STOPPED");
    }
    else if (strcmp(cmd, "LOCK") == 0) {
        g_lock_active = 1;
//        app_relay_pulse_lock();
        app_mqtt_pub_status("LOCKED");
    }
    else if (strcmp(cmd, "UNLOCK") == 0) {
        g_lock_active = 0;
        app_relay_pulse_stop(); // Thường stop cũng là mở khóa
        app_mqtt_pub_status("UNLOCKED");
    }
}

// Hàm chuyển đổi sự kiện nút bấm sang lệnh
void app_door_controller_core_handle_button_event(btn_event_t event) {
    switch (event) {
        case BTN_EVENT_OPEN:       app_door_controller_core_execute_cmd_string("OPEN"); break;
        case BTN_EVENT_CLOSE:      app_door_controller_core_execute_cmd_string("CLOSE"); break;
        case BTN_EVENT_STOP:       app_door_controller_core_execute_cmd_string("STOP"); break;
        case BTN_EVENT_LOCK_PRESS:
            // Logic nút cứng: Toggle khóa
            if (g_lock_active) app_door_controller_core_execute_cmd_string("UNLOCK");
            else app_door_controller_core_execute_cmd_string("LOCK");
            break;
    }
}
