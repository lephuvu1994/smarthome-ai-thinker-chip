#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>

#include <aos/kernel.h>
#include <aos/yloop.h>
#include <bl_sys.h>
#include <blog.h>
#include <lwip/tcpip.h>
#include <hal_wifi.h>

// --- INCLUDE MODULES ---
#include "app_conf.h"
#include "app_storage.h"
#include "app_led.h"
#include "modules/ble_app_service.h"
#include "wifi_service.h"

// --- VARIABLES ---
static TimerHandle_t g_wifi_check_timer = NULL;
static int g_wifi_retry_cnt = 0;
static wifi_conf_t conf = { .country_code = "CN" };

// ============================================================================
// PING-PONG LOGIC HANDLERS
// ============================================================================

// Callback khi hết 60s -> Tắt BLE, Thử Wifi
static void wifi_check_timer_cb(TimerHandle_t xTimer) {
    printf("\r\n[TIMER] 60s Timeout. Retry Wifi...\r\n");
    ble_stop_advertising();
    vTaskDelay(500);
    g_wifi_retry_cnt = 0; // Reset counter
    wifi_connect_stored();
}

// Xử lý sự kiện Wifi (Logic quan trọng nhất nằm ở đây)
static void event_cb_wifi_event(input_event_t* event, void* private_data) {
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_start_background(&conf);
            break;

        case CODE_WIFI_ON_MGMR_DONE:
            wifi_connect_stored();
            break;

        case CODE_WIFI_ON_GOT_IP:
            printf("\r\n[MAIN] >>> WIFI CONNECTED! <<<\r\n");
            g_wifi_retry_cnt = 0;
            if (xTimerIsTimerActive(g_wifi_check_timer)) xTimerStop(g_wifi_check_timer, 0);

            ble_stop_advertising();   // Dọn dẹp BLE
            set_led_mode(LED_ON_MODE); // Đèn sáng đứng

            // [V28 TODO]: Start MQTT Task here
            // [V28 TODO]: Start Watchdog here
            break;

        case CODE_WIFI_ON_DISCONNECT:
             g_wifi_retry_cnt++;
             printf("[MAIN] Wifi Lost/Fail. Retry %d/%d\r\n", g_wifi_retry_cnt, MAX_WIFI_RETRY);

             if (g_wifi_retry_cnt < MAX_WIFI_RETRY) {
                 vTaskDelay(2000);
                 wifi_connect_stored();
             }
             else {
                 printf("[MAIN] Too many fails. FORCE STOP WIFI -> START BLE.\r\n");
                 wifi_force_disconnect(); // Kill Driver Wifi

                 ble_start_advertising(); // Start BLE

                 // Start Timer 60s
                 if (!xTimerIsTimerActive(g_wifi_check_timer)) {
                     xTimerStart(g_wifi_check_timer, 0);
                 }
             }
             break;
    }
}

// ============================================================================
// MAIN ENTRY
// ============================================================================
static void proc_main_entry(void *pvParameters)
{
    // 1. Init Modules
    storage_init();
    led_init();
    ble_service_init();

    // 2. Setup Timer
    g_wifi_check_timer = xTimerCreate("WfChk", pdMS_TO_TICKS(CHECK_INTERVAL_MS), pdTRUE, (void *)0, wifi_check_timer_cb);

    // 3. Check Boot State
    char ssid_check[33];
    char pass_check[64];

    if (!get_wifi_safe(ssid_check, pass_check)) {
        printf("[BOOT] No Wifi config. Enter BLE Mode.\r\n");
        ble_start_advertising();
    }
    else {
        printf("[BOOT] Wifi config found. Starting Stack.\r\n");
        tcpip_init(NULL, NULL);
        aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);
        hal_wifi_start_firmware_task();
        aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
    }

    vTaskDelete(NULL);
}

void main()
{
    bl_sys_init();
    puts("[OS] Booting Refactored V27...\r\n");
    xTaskCreate(proc_main_entry, "main", 2048, NULL, 15, NULL);
}
