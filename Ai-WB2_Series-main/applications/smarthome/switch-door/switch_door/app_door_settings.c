#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <easyflash.h>

#include "app_door_settings.h"
#include "app_storage.h"
#include "app_mqtt.h" // Để pub status settings

// --- BIẾN CẤU HÌNH ---
static int g_req_open_clicks = 1;
static int g_req_close_clicks = 1;
static int g_def_open_clicks = 1;
static int g_def_close_clicks = 1;

static int g_time_mode = 0;
static int g_start_hour = 22;
static int g_end_hour = 6;

// --- BIẾN XỬ LÝ CLICK ---
static int g_current_clicks = 0;
static char g_pending_cmd[10] = {0};
static TimerHandle_t g_multi_click_timer = NULL;
static cmd_execute_cb_t g_execute_callback = NULL;

// --- PROTOTYPES ---
static int get_required_clicks(const char* cmd);

// Callback khi hết thời gian chờ Click (500ms)
static void multi_click_timeout_cb(TimerHandle_t xTimer) {
    int required = get_required_clicks(g_pending_cmd);
    printf("[SETTINGS] Check: %s -> Count: %d / Req: %d\r\n", g_pending_cmd, g_current_clicks, required);

    if (g_current_clicks >= required) {
        // ĐỦ SỐ LẦN -> Gọi Core thực thi
        if (g_execute_callback) g_execute_callback(g_pending_cmd);
    } else {
        printf("[SETTINGS] Ignored (Not enough clicks)\r\n");
    }
    
    // Reset
    g_current_clicks = 0;
    memset(g_pending_cmd, 0, sizeof(g_pending_cmd));
}

// Logic kiểm tra giờ
static int get_required_clicks(const char* cmd) {
    int restricted = 1; 
    int normal = 1;

    if (strcmp(cmd, "OPEN") == 0) { restricted = g_req_open_clicks; normal = g_def_open_clicks; }
    else if (strcmp(cmd, "CLOSE") == 0) { restricted = g_req_close_clicks; normal = g_def_close_clicks; }

    if (g_time_mode == 0) return restricted;

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (tm_now->tm_year < 120) return restricted; // Chưa có giờ mạng

    int cur = tm_now->tm_hour;
    int inside = 0;
    if (g_start_hour < g_end_hour) { if (cur >= g_start_hour && cur < g_end_hour) inside = 1; }
    else { if (cur >= g_start_hour || cur < g_end_hour) inside = 1; }

    return inside ? restricted : normal;
}

// ================= PUBLIC FUNCTIONS =================

void door_settings_init(cmd_execute_cb_t callback) {
    g_execute_callback = callback;
    g_multi_click_timer = xTimerCreate("MultiClick", pdMS_TO_TICKS(500), pdFALSE, (void *)0, multi_click_timeout_cb);
}

void door_settings_load(void) {
    storage_get_door_settings(&g_req_open_clicks, &g_req_close_clicks,
                              &g_def_open_clicks, &g_def_close_clicks,
                              &g_time_mode, &g_start_hour, &g_end_hour);
    printf("[SETTINGS] Loaded: Mode %d (%d-%d)\r\n", g_time_mode, g_start_hour, g_end_hour);
}

void door_settings_reset_click(void) {
    if (xTimerIsTimerActive(g_multi_click_timer)) xTimerStop(g_multi_click_timer, 0);
    g_current_clicks = 0;
}

// Trả về 1 nếu cần chạy ngay (req=1), 0 nếu cần chờ click tiếp
int door_settings_process_cmd(const char* cmd) {
    int req = get_required_clicks(cmd);
    
    if (req == 1) return 1; // Chạy ngay

    // Xử lý đếm click
    if (strcmp(cmd, g_pending_cmd) != 0) {
        g_current_clicks = 0;
        strcpy(g_pending_cmd, cmd);
    }
    g_current_clicks++;
    printf("[SETTINGS] Buffer: %s (%d/%d)\r\n", cmd, g_current_clicks, req);

    if(xTimerIsTimerActive(g_multi_click_timer)) xTimerStop(g_multi_click_timer, 0);
    xTimerStart(g_multi_click_timer, 0);
    
    return 0; // Chờ Timer
}

void door_settings_update_from_mqtt(int open, int close, int def_open, int def_close, int mode, int start, int end) {
    int changed = 0;
    if (open >= 1)  { g_req_open_clicks = open; changed = 1; }
    if (close >= 1) { g_req_close_clicks = close; changed = 1; }
    if (def_open >= 1)  { g_def_open_clicks = def_open; changed = 1; }
    if (def_close >= 1) { g_def_close_clicks = def_close; changed = 1; }
    if (mode >= 0)  { g_time_mode = mode; changed = 1; }
    if (start >= 0) { g_start_hour = start; changed = 1; }
    if (end >= 0)   { g_end_hour = end; changed = 1; }

    if (changed) {
        storage_save_door_settings(g_req_open_clicks, g_req_close_clicks, 
                                   g_def_open_clicks, g_def_close_clicks,
                                   g_time_mode, g_start_hour, g_end_hour);
        
        char json_buf[200];
        snprintf(json_buf, sizeof(json_buf), 
                 "{\"settings\":{\"open\":%d,\"close\":%d,\"def_open\":%d,\"def_close\":%d,\"mode\":%d,\"start\":%d,\"end\":%d}}", 
                 g_req_open_clicks, g_req_close_clicks, g_def_open_clicks, g_def_close_clicks,
                 g_time_mode, g_start_hour, g_end_hour);
        app_mqtt_pub_status(json_buf);
    }
}