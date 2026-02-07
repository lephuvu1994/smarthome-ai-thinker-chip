#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

// Hàm khởi tạo Watchdog (đã bao gồm tự động tạo Timer cho ăn)
void app_watchdog_init(void);

// Hàm cho ăn thủ công (nếu cần dùng ở chỗ khác)
void app_watchdog_feed(void);

#endif
