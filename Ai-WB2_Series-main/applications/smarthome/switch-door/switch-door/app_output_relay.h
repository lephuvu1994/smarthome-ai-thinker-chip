#ifndef __APP_OUTPUT_RELAY_H__
#define __APP_OUTPUT_RELAY_H__

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* DEFINITIONS                                */
/* -------------------------------------------------------------------------- */
// Lưu ý: Đảm bảo GPIO_OUT_CLOSE và GPIO_OUT_OPEN đã được định nghĩa
// trong "app_conf.h" hoặc bạn có thể define tạm ở đây nếu chưa có:
// #define GPIO_OUT_OPEN   4
// #define GPIO_OUT_CLOSE  5

/* -------------------------------------------------------------------------- */
/* FUNCTION PROTOTYPES                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Khởi tạo các chân GPIO cho Relay (Mức mặc định: Tắt)
 */
void app_relay_init(void);

/**
 * @brief Tắt toàn bộ Relay (Dừng động cơ / Safety)
 * Cả chân OPEN và CLOSE đều về mức 0.
 */
void app_relay_stop_all(void);

/**
 * @brief Bật Relay Mở và Giữ trạng thái (Hold Open)
 * Hàm này có cơ chế Interlock (Khóa chéo): Tắt Close trước rồi mới bật Open.
 */
void app_relay_hold_open(void);

/**
 * @brief Bật Relay Đóng và Giữ trạng thái (Hold Close)
 * Hàm này có cơ chế Interlock (Khóa chéo): Tắt Open trước rồi mới bật Close.
 */
void app_relay_hold_close(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_OUTPUT_RELAY_H__ */