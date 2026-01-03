// PIC input processor interface (ADCs, buttons, capsense)
#ifndef PLATFORM_PIC_H
#define PLATFORM_PIC_H

#include <cstdint>

namespace sc {
namespace platform {

struct PicState {
    int i2c_fd = -1;
    bool present = false;
};

// Raw readings from PIC
struct PicReadings {
    uint16_t adc[4];       // 10-bit ADC values (faders, pots)
    bool buttons[4];       // Button states
    bool cap_touched;      // Capacitive touch sensor
};

// Initialize PIC input processor on I2C bus
bool pic_init(PicState* state);

// Read all inputs from PIC
PicReadings pic_read_all(PicState* state);

} // namespace platform
} // namespace sc

#endif
