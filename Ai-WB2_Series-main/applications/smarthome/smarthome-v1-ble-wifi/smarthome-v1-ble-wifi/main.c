/*
 * main.c - Smarthome V1 (Wifi Only Version)
 * Bỏ qua BLE để fix lỗi biên dịch
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <string.h>

#include "bl_gpio.h"
#include "bl_uart.h"
#include "bl_sys.h"
#include "bl_wifi.h"

// Thư viện Wifi mà chúng ta đã fix đường dẫn
#include "wifi_mgmr_ext.h"
#include "wifi_mgmr.h"

// Define chân LED (Thay đổi tùy Kit của bạn, Kit Ai-WB2 thường là 14 hoặc 4)
#define LED_PIN 14

// Biến fix lỗi linker coredump (nếu SDK yêu cầu)
uint32_t _sp_base;

/* Hàm khởi tạo Wifi cơ bản */
void wifi_init_process(void)
{
    printf("--- Init Wifi Manager ---\r\n");
    wifi_conf_t conf = {
        .country_code = "CN",
    };
    wifi_mgmr_drv_init(&conf);
}

/* Task chính: Nháy LED và in log */
void main_task(void *pvParameters)
{
    // Init GPIO cho LED
    bl_gpio_enable_output(LED_PIN, 0, 0);

    // Init Wifi
    wifi_init_process();

    printf("Build Success! BLE removed.\r\n");

    while (1) {
        bl_gpio_output_set(LED_PIN, 1); // LED ON
        printf("System Running... Tick: %lu\r\n", xTaskGetTickCount());
        vTaskDelay(1000);

        bl_gpio_output_set(LED_PIN, 0); // LED OFF
        vTaskDelay(1000);
    }
}

/* Hàm Main của hệ thống */
void main(void)
{
    // Tạo Task
    xTaskCreate(main_task, "main_task", 1024, NULL, 15, NULL);
}
