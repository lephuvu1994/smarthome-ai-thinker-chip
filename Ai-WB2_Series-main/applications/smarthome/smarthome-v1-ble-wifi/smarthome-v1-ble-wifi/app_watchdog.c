#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <bl_wdt.h> // Thư viện phần cứng

#include "app_conf.h"
#include "app_watchdog.h"

static TimerHandle_t g_wdt_timer = NULL;

// Hàm thực thi nuôi chó (được gọi bởi Timer)
static void wdt_feed_callback(TimerHandle_t xTimer) {
    // Gọi hàm phần cứng để reset bộ đếm
    bl_wdt_feed();
    // printf("."); // Uncomment nếu muốn thấy nhịp tim trên log
}

void app_watchdog_feed(void) {
    bl_wdt_feed();
}

void app_watchdog_init(void) {
    printf("[WDT] Init Watchdog: Timeout %ds, Feed every %dms\r\n", WDT_TIMEOUT_SEC, WDT_FEED_MS);

    // 1. Khởi tạo phần cứng (Set thời gian timeout)
    bl_wdt_init(WDT_TIMEOUT_SEC);

    // 2. Cho ăn phát đầu tiên cho chắc ăn
    bl_wdt_feed();

    // 3. Tạo Timer chạy ngầm để tự động cho ăn
    g_wdt_timer = xTimerCreate("WdtFeed",
                               pdMS_TO_TICKS(WDT_FEED_MS),
                               pdTRUE, // Auto Reload (Lặp lại mãi mãi)
                               (void *)0,
                               wdt_feed_callback);

    if (g_wdt_timer != NULL) {
        xTimerStart(g_wdt_timer, 0);
    } else {
        printf("[WDT] Critical Error: Cannot create feed timer!\r\n");
    }
}
