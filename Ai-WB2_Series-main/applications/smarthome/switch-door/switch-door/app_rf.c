#include "app_rf.h"
#include "rcswitch.h"
#include <easyflash.h>
#include <bl_gpio.h>
#include <hal_gpio.h>
#include <bl_timer.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <app_conf.h>

static RCSwitch_t mySwitch;
static rf_event_cb_t g_rf_callback = NULL;

// Biến cấu hình ngắt
static gpio_ctx_t rf_gpio_ctx;

static rf_action_t learning_mode = RF_CMD_UNKNOWN;
static uint32_t learn_timeout = 0;

static uint32_t code_open = 0;
static uint32_t code_close = 0;
static uint32_t code_stop = 0;
static uint32_t code_lock = 0;

// --- HAL Wrapper cho RCSwitch ---
void RCS_DelayUs(uint32_t us) {
    extern void bl_timer_delay_us(unsigned long us);
    bl_timer_delay_us(us);
}

uint32_t RCS_GetMicros(void) {
    return (uint32_t)bl_timer_now_us();
}

void RCS_GpioWrite(int pin, int val) {
    bl_gpio_output_set(pin, val ? 1 : 0);
}

// Hàm xử lý ngắt
static void rf_isr(void *arg) {
    RCSwitch_HandleInterrupt(&mySwitch);
}

// Load code từ Flash
static void load_codes() {
    size_t len;
    ef_get_env_blob("rf_open", &code_open, 4, &len);
    ef_get_env_blob("rf_close", &code_close, 4, &len);
    ef_get_env_blob("rf_stop", &code_stop, 4, &len);
    ef_get_env_blob("rf_lock", &code_lock, 4, &len);
}

static void save_code(const char* key, uint32_t *var, uint32_t val) {
    *var = val;
    ef_set_env_blob(key, &val, 4);
    ef_save_env();
}

// --- INIT & SETUP ---

void app_rf_init(rf_event_cb_t callback) {
    g_rf_callback = callback;

    RCSwitch_Init(&mySwitch);
    load_codes();

    // 1. Cấu hình chân GPIO là Input
    bl_gpio_enable_input(RF_PIN, 0, 0);

    // 2. Clear các cờ ngắt cũ (Dùng số 1 thay vì macro nếu thiếu)
    bl_gpio_int_clear(RF_PIN, 1);

    // 3. Cho phép ngắt (Unmask) - LÀM Ở ĐÂY THAY VÌ TRONG STRUCT
    bl_gpio_intmask(RF_PIN, 0); // 0 = Unmask (Enable Interrupt)

    // 4. Đăng ký ngắt vào hệ thống
    rf_gpio_ctx.gpioPin = RF_PIN;

    // Lưu ý: Nếu SDK báo lỗi GPIO_INT_CONTROL_ASYNC, hãy thử đổi thành GLB_GPIO_INT_CONTROL_ASYNC_EDGE
    rf_gpio_ctx.intCtrlMod = GPIO_INT_CONTROL_ASYNC;
    rf_gpio_ctx.gpio_handler = rf_isr;
    rf_gpio_ctx.arg = NULL;
    rf_gpio_ctx.next = NULL;
    // [ĐÃ SỬA] Xóa dòng rf_gpio_ctx.intMask = 0; gây lỗi

    bl_gpio_register(&rf_gpio_ctx);
}

void app_rf_start_learning(rf_action_t action_type) {
    learning_mode = action_type;
    learn_timeout = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
    printf("[RF] Start Learning Mode: %d\n", action_type);
}

void app_rf_loop(void) {
    // 1. Check Timeout
    if (learning_mode != RF_CMD_UNKNOWN) {
        if (xTaskGetTickCount() > learn_timeout) {
            printf("[RF] Learn Timeout.\n");
            learning_mode = RF_CMD_UNKNOWN;
        }
    }

    // 2. Check RF Signal
    if (RCSwitch_Available(&mySwitch)) {
        uint32_t value = RCSwitch_GetReceivedValue(&mySwitch);

        if (learning_mode != RF_CMD_UNKNOWN) {
            printf("[RF] Learned Code: %lu\n", value);
            switch(learning_mode) {
                case RF_ACTION_OPEN: save_code("rf_open", &code_open, value); break;
                case RF_ACTION_CLOSE: save_code("rf_close", &code_close, value); break;
                case RF_ACTION_STOP: save_code("rf_stop", &code_stop, value); break;
                case RF_ACTION_LOCK: save_code("rf_lock", &code_lock, value); break;
                default: break;
            }
            learning_mode = RF_CMD_UNKNOWN;
        }
        else {
            rf_action_t action = RF_CMD_UNKNOWN;
            if (value == code_open && code_open != 0) action = RF_ACTION_OPEN;
            else if (value == code_close && code_close != 0) action = RF_ACTION_CLOSE;
            else if (value == code_stop && code_stop != 0) action = RF_ACTION_STOP;
            else if (value == code_lock && code_lock != 0) action = RF_ACTION_LOCK;

            if (action != RF_CMD_UNKNOWN && g_rf_callback != NULL) {
                static uint32_t last_time = 0;
                if (xTaskGetTickCount() - last_time > pdMS_TO_TICKS(500)) {
                    printf("[RF] Match Action: %d\n", action);
                    g_rf_callback(action);
                    last_time = xTaskGetTickCount();
                }
            }
        }
        RCSwitch_ResetAvailable(&mySwitch);
    }
}

// Hàm chạy Task
static void rf_task_entry(void *pvParameters) {
    while (1) {
        app_rf_loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Hàm khởi tạo Task cho Main gọi
void app_rf_start_task(uint32_t stack_size, int priority, rf_event_cb_t callback) {
    app_rf_init(callback);
    xTaskCreate(rf_task_entry, "rf_task", stack_size, NULL, priority, NULL);
    printf("[RF] Task Started\r\n");
}
