#ifndef RCSWITCH_H
#define RCSWITCH_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// --- CẤU HÌNH CHO BL602 / SDK ---
// Bạn cần định nghĩa các macro này để map với hàm của chip bạn đang dùng
// Ví dụ với BL602 (giả định hàm):
// #define RCS_DELAY_US(x)      bl_timer_delay_us(x)
// #define RCS_GET_MICROS()     bl_timer_now_us()
// #define RCS_GPIO_WRITE(p, v) bl_gpio_output_set(p, v)

// Để trống để bạn điền hàm vào file .c hoặc định nghĩa ở đây
extern void RCS_DelayUs(uint32_t us);
extern uint32_t RCS_GetMicros(void);
extern void RCS_GpioWrite(int pin, int val);

#define RCSWITCH_MAX_CHANGES 67

typedef struct {
    uint16_t pulseLength;
    struct { uint8_t high; uint8_t low; } syncFactor;
    struct { uint8_t high; uint8_t low; } zero;
    struct { uint8_t high; uint8_t low; } one;
    bool invertedSignal;
} RCSwitch_Protocol_t;

typedef struct {
    // Transmitter
    int nTransmitterPin;
    int nRepeatTransmit;
    int nProtocolIndex;
    RCSwitch_Protocol_t protocol;

    // Receiver
    int nReceiverInterruptPin;
    int nReceiveTolerance;
    volatile uint32_t nReceivedValue;
    volatile uint32_t nReceivedBitlength;
    volatile uint32_t nReceivedDelay;
    volatile uint32_t nReceivedProtocol;
    volatile uint32_t timings[RCSWITCH_MAX_CHANGES];

    // Internal state for ISR
    uint32_t lastTime;
    uint32_t changeCount;
    uint32_t repeatCount;
} RCSwitch_t;

// Init
void RCSwitch_Init(RCSwitch_t *rcs);

// Transmitter Functions
void RCSwitch_EnableTransmit(RCSwitch_t *rcs, int pin);
void RCSwitch_DisableTransmit(RCSwitch_t *rcs);
void RCSwitch_SetProtocol(RCSwitch_t *rcs, int nProtocol);
void RCSwitch_SetPulseLength(RCSwitch_t *rcs, int nPulseLength);
void RCSwitch_Send(RCSwitch_t *rcs, uint32_t code, uint32_t length);
void RCSwitch_SendTriState(RCSwitch_t *rcs, const char* sCodeWord);

// Receiver Functions
void RCSwitch_ResetAvailable(RCSwitch_t *rcs);
bool RCSwitch_Available(RCSwitch_t *rcs);
uint32_t RCSwitch_GetReceivedValue(RCSwitch_t *rcs);

// Hàm xử lý ngắt (Gọi hàm này trong GPIO ISR của chip)
void RCSwitch_HandleInterrupt(RCSwitch_t *rcs);

#endif // RCSWITCH_H
