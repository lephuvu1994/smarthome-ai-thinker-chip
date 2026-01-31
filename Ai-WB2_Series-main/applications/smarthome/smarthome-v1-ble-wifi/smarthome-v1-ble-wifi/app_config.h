#ifndef APP_CONF_H
#define APP_CONF_H

// --- HARDWARE PIN ---
#define LED_PIN_1           4
#define LED_PIN_2           14
// #define RELAY_PIN        12 // Dành cho V28

// --- TIMING & LOGIC ---
#define MAX_WIFI_RETRY      5       // Thử 5 lần
#define CHECK_INTERVAL_MS   60000   // 60 giây check lại Wifi
#define LED_BLINK_SLOW      1000
#define LED_BLINK_FAST      200
#define LED_ON_MODE         0

// --- BLE CONFIG ---
#define BLE_NAME            "Ai-WB2-SMART"
// UUID Service
#define UUID_SVC_DEF        0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455
// UUID RX Characteristic
#define UUID_RX_DEF         0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616

#endif
