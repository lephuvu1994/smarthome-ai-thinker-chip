#include "rcswitch.h"

// --- Hardware Abstraction Layer (Cần người dùng implement) ---
// Viết đè các hàm này hoặc map macro trong header
__attribute__((weak)) void RCS_DelayUs(uint32_t us) { /* Implement me */ }
__attribute__((weak)) uint32_t RCS_GetMicros(void) { return 0; /* Implement me */ }
__attribute__((weak)) void RCS_GpioWrite(int pin, int val) { /* Implement me */ }

// --- Protocol Definitions ---
static const RCSwitch_Protocol_t proto[] = {
  { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1
  { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
  { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
  { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
  { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
  { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 6 (HT6P20B)
  { 150, {  2, 62 }, {  1,  6 }, {  6,  1 }, false },    // protocol 7 (HS2303-PT)
  { 200, {  3, 130}, {  7, 16 }, {  3,  16}, false},     // protocol 8 Conrad RS-200 RX
  { 200, { 130, 7 }, {  16, 7 }, { 16,  3 }, true},      // protocol 9 Conrad RS-200 TX
  { 365, { 18,  1 }, {  3,  1 }, {  1,  3 }, true },     // protocol 10 (1ByOne Doorbell)
  { 270, { 36,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 11 (HT12E)
  { 320, { 36,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 12 (SM5212)
};

#define NUM_PROTO (sizeof(proto) / sizeof(proto[0]))
#define RECEIVE_TOLERANCE 60
#define SEPARATION_LIMIT 4300

void RCSwitch_Init(RCSwitch_t *rcs) {
    memset(rcs, 0, sizeof(RCSwitch_t));
    rcs->nTransmitterPin = -1;
    rcs->nReceiverInterruptPin = -1;
    rcs->nRepeatTransmit = 10;
    rcs->nReceiveTolerance = RECEIVE_TOLERANCE;
    RCSwitch_SetProtocol(rcs, 1);
}

void RCSwitch_SetProtocol(RCSwitch_t *rcs, int nProtocol) {
    if (nProtocol < 1 || nProtocol > NUM_PROTO) {
        nProtocol = 1;
    }
    rcs->nProtocolIndex = nProtocol;
    rcs->protocol = proto[nProtocol - 1];
}

void RCSwitch_SetPulseLength(RCSwitch_t *rcs, int nPulseLength) {
    rcs->protocol.pulseLength = nPulseLength;
}

void RCSwitch_EnableTransmit(RCSwitch_t *rcs, int pin) {
    rcs->nTransmitterPin = pin;
    // Lưu ý: Cần set pinMode OUTPUT ở ngoài hàm này hoặc thêm hàm HAL
}

void RCSwitch_DisableTransmit(RCSwitch_t *rcs) {
    rcs->nTransmitterPin = -1;
}

static void transmit_pulse(RCSwitch_t *rcs, int high, int low) {
    if (rcs->nTransmitterPin == -1) return;

    int firstLevel = rcs->protocol.invertedSignal ? 0 : 1;
    int secondLevel = rcs->protocol.invertedSignal ? 1 : 0;

    RCS_GpioWrite(rcs->nTransmitterPin, firstLevel);
    RCS_DelayUs(rcs->protocol.pulseLength * high);
    RCS_GpioWrite(rcs->nTransmitterPin, secondLevel);
    RCS_DelayUs(rcs->protocol.pulseLength * low);
}

void RCSwitch_Send(RCSwitch_t *rcs, uint32_t code, uint32_t length) {
    if (rcs->nTransmitterPin == -1) return;

    for (int nRepeat = 0; nRepeat < rcs->nRepeatTransmit; nRepeat++) {
        for (int i = length - 1; i >= 0; i--) {
            if (code & (1L << i))
                transmit_pulse(rcs, rcs->protocol.one.high, rcs->protocol.one.low);
            else
                transmit_pulse(rcs, rcs->protocol.zero.high, rcs->protocol.zero.low);
        }
        transmit_pulse(rcs, rcs->protocol.syncFactor.high, rcs->protocol.syncFactor.low);
    }
    // Disable output
    RCS_GpioWrite(rcs->nTransmitterPin, 0);
}

// --- Receiver Logic ---

static unsigned int diff(int A, int B) {
    return abs(A - B);
}

static bool receiveProtocol(RCSwitch_t *rcs, int p, unsigned int changeCount) {
    const RCSwitch_Protocol_t *pro = &proto[p - 1];
    uint32_t code = 0;

    // Logic tính delay
    unsigned int syncLengthInPulses = ((pro->syncFactor.low) > (pro->syncFactor.high)) ? (pro->syncFactor.low) : (pro->syncFactor.high);
    unsigned int delay = rcs->timings[0] / syncLengthInPulses;
    unsigned int delayTolerance = delay * rcs->nReceiveTolerance / 100;

    unsigned int firstDataTiming = (pro->invertedSignal) ? 2 : 1;

    for (unsigned int i = firstDataTiming; i < changeCount - 1; i += 2) {
        code <<= 1;
        if (diff(rcs->timings[i], delay * pro->zero.high) < delayTolerance &&
            diff(rcs->timings[i + 1], delay * pro->zero.low) < delayTolerance) {
            // zero
        } else if (diff(rcs->timings[i], delay * pro->one.high) < delayTolerance &&
                   diff(rcs->timings[i + 1], delay * pro->one.low) < delayTolerance) {
            // one
            code |= 1;
        } else {
            // Failed
            return false;
        }
    }

    if (changeCount > 7) {
        rcs->nReceivedValue = code;
        rcs->nReceivedBitlength = (changeCount - 1) / 2;
        rcs->nReceivedDelay = delay;
        rcs->nReceivedProtocol = p;
        return true;
    }
    return false;
}

void RCSwitch_HandleInterrupt(RCSwitch_t *rcs) {
    uint32_t time = RCS_GetMicros();
    uint32_t duration = time - rcs->lastTime;

    if (duration > SEPARATION_LIMIT) {
        // Gap detected
        if (diff(duration, rcs->timings[0]) < 200) {
            rcs->repeatCount++;
            if (rcs->repeatCount == 2) {
                for (int i = 1; i <= NUM_PROTO; i++) {
                    if (receiveProtocol(rcs, i, rcs->changeCount)) {
                        break;
                    }
                }
                rcs->repeatCount = 0;
            }
        }
        rcs->changeCount = 0;
    }

    if (rcs->changeCount >= RCSWITCH_MAX_CHANGES) {
        rcs->changeCount = 0;
        rcs->repeatCount = 0;
    }

    rcs->timings[rcs->changeCount++] = duration;
    rcs->lastTime = time;
}

bool RCSwitch_Available(RCSwitch_t *rcs) {
    return rcs->nReceivedValue != 0;
}

uint32_t RCSwitch_GetReceivedValue(RCSwitch_t *rcs) {
    return rcs->nReceivedValue;
}

void RCSwitch_ResetAvailable(RCSwitch_t *rcs) {
    rcs->nReceivedValue = 0;
}
