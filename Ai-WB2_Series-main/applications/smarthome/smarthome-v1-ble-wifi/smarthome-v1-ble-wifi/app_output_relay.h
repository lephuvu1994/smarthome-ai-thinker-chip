#ifndef __APP_RELAY_H__
#define __APP_RELAY_H__

// Khởi tạo các chân GPIO Output
void app_relay_init(void);

// Các hàm điều khiển vật lý (Pulse 0.5s)
//void app_relay_pulse_open(void);
void app_relay_pulse_close(void);
//void app_relay_pulse_stop(void);
//void app_relay_pulse_lock(void);

#endif
