#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <hal_wdt.h>
#include <hal_sys.h>
#include <hal_gpio.h>
#include <wifi_mgmr_ext.h>
#include "app_conf.h"

// Import functions
extern void storage_init();
extern void storage_load_all();
extern void storage_factory_reset();
extern void app_ble_start(); // Hàm start BLE advertising
extern void app_mqtt_start(); // Hàm connect MQTT
extern int app_api_bootstrap();
extern int app_api_activate(char*);

// Biến trạng thái
static int wifi_connected = 0;

// Callback khi Wifi thay đổi trạng thái
void wifi_event_handler(int event) {
    if (event == WIFI_EVENT_CONNECTED) {
        printf("[WIFI] Connected!\r\n");
        wifi_connected = 1;
    } else if (event == WIFI_EVENT_DISCONNECTED) {
        printf("[WIFI] Disconnected! Reconnecting...\r\n");
        wifi_connected = 0;
        // Mất mạng -> Bật BLE để điều khiển offline
        app_ble_start();
    }
}

// Điều khiển Relay
void control_relay(int on) {
    hal_gpio_output_set(RELAY_PIN, on ? 1 : 0);
    g_cfg.relay_state = on;
    printf("[RELAY] State: %d\r\n", on);
}

// Check nút Reset (Nhấn giữ 5s)
void check_factory_reset() {
    if (hal_gpio_input_get(FACTORY_RESET_PIN) == 0) { // Giả sử nhấn là LOW
        vTaskDelay(5000); // Đợi 5s
        if (hal_gpio_input_get(FACTORY_RESET_PIN) == 0) {
            printf("!!! FACTORY RESET TRIGGERED !!!\r\n");
            storage_factory_reset();
            hal_reboot();
        }
    }
}

// TASK CHÍNH
void main_task(void *pv) {
    // 1. Init
    storage_init();
    storage_load_all();
    hal_wdt_init(WDT_TIMEOUT_SECONDS * 1000); // Bật Watchdog

    // Config GPIO
    hal_gpio_init(RELAY_PIN, GPIO_OUTPUT, GPIO_PULL_UP, 0);
    hal_gpio_init(FACTORY_RESET_PIN, GPIO_INPUT, GPIO_PULL_UP, 0);

    // 2. Logic Wifi
    if (strlen(g_cfg.ssid) > 0) {
        printf("[MAIN] Connecting to Wifi: %s\r\n", g_cfg.ssid);
        wifi_mgmr_sta_connect(g_cfg.ssid, g_cfg.pass, NULL, NULL);
    } else {
        printf("[MAIN] No Wifi Config. Starting BLE Provisioning...\r\n");
        app_ble_start(); // Chỉ chạy BLE chờ App
    }

    // 3. Vòng lặp vô tận (Run 24/7)
    while (1) {
        // --- A. FEED WATCHDOG (Cho chó ăn) ---
        hal_wdt_feed();

        // --- B. CHECK NÚT RESET ---
        check_factory_reset();

        // --- C. XỬ LÝ KHI CÓ MẠNG ---
        if (wifi_connected) {

            // C1. Nếu chưa có config server -> Gọi Bootstrap
            if (strlen(g_cfg.api_host) == 0) {
                app_api_bootstrap();
                continue;
            }

            // C2. Nếu chưa Active -> Gọi Active
            if (g_cfg.is_activated == 0) {
                // Lấy MAC
                uint8_t mac[6]; char mac_str[18];
                wifi_mgmr_sta_mac_get(mac);
                sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                if (app_api_activate(mac_str)) {
                    printf("[MAIN] Device Activated! Rebooting...\r\n");
                    hal_reboot();
                } else {
                    printf("[MAIN] Activate Failed (Quota?)\r\n");
                    vTaskDelay(5000); // Chờ thử lại
                }
                continue;
            }

            // C3. Đã Active + Có Config -> Chạy MQTT
            // Logic: Nếu MQTT chưa connect thì connect, nếu rồi thì keep-alive
            // app_mqtt_process();
        }
        else {
            // --- D. XỬ LÝ KHI MẤT MẠNG ---
            // Đảm bảo BLE đang chạy để nhận lệnh Offline
            // (Đã bật ở event handler)
        }

        vTaskDelay(100); // Nghỉ 100ms để nhường CPU cho các task Wifi/BLE
    }
}

void main() {
    bl_uart_init(0, 16, 7, 255, 255, 2 * 1000 * 1000);
    printf("--- SYSTEM BOOT ---\r\n");
    xTaskCreate(main_task, "main", 4096, NULL, 10, NULL);
}
