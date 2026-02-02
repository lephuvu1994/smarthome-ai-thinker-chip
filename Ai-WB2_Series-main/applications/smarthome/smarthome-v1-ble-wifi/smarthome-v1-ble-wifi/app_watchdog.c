#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <bl_wdt.h> 

#include "app_conf.h"
#include "app_watchdog.h"

static TimerHandle_t g_wdt_timer = NULL;

static void wdt_feed_callback(TimerHandle_t xTimer) {
    bl_wdt_feed();
}

void app_watchdog_init(void) {
    printf("[WDT] Init HW: %d ms (%ds). Feed every %d ms\r\n", WDT_TIMEOUT_SEC, WDT_FEED_MS);

    // [QUAN TRỌNG] Truyền vào 10000 chứ không phải 10
    bl_wdt_init(WDT_TIMEOUT_SEC); 
    
    // Cho ăn phát đầu cho chắc
    bl_wdt_feed();

    // 2. Tạo Timer Software để nuôi chó tự động
    g_wdt_timer = xTimerCreate("WdtTmr",
                               pdMS_TO_TICKS(WDT_FEED_MS), 
                               pdTRUE,                 
                               (void *)0,
                               wdt_feed_callback);

    if (g_wdt_timer != NULL) {
        if (xTimerStart(g_wdt_timer, 0) != pdPASS) {
             printf("[WDT] Error: Cannot start timer!\r\n");
        } else {
             printf("[WDT] Timer Started. System Protected.\r\n");
        }
    }
}