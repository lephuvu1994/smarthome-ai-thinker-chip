#include <stdio.h>
#include <wifi_mgmr_ext.h>
#include "app_storage.h"
#include "app_led.h"
#include "app_conf.h"

void wifi_connect_stored(void) {
    char ssid[33];
    char pass[64];

    if (get_wifi_safe(ssid, pass)) {
        printf("[WIFI] Connecting to [%s]...\r\n", ssid);
        set_led_mode(LED_BLINK_FAST);
        wifi_interface_t wifi_interface = wifi_mgmr_sta_enable();
        wifi_mgmr_sta_connect(wifi_interface, ssid, pass, NULL, NULL, 0, 0);
    } else {
        printf("[WIFI] Err: Empty Config.\r\n");
    }
}

void wifi_force_disconnect(void) {
    wifi_mgmr_sta_disconnect();
}
