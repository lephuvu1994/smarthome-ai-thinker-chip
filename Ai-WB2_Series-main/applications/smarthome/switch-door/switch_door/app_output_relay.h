#ifndef __APP_OUTPUT_RELAY_H__
#define __APP_OUTPUT_RELAY_H__

void app_relay_init(void);
void app_relay_stop_all(void);

// Hàm chính để điều khiển: Kích 1 cái rồi tự tắt
void app_relay_pulse(int pin);

#endif