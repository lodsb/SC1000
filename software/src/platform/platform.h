// Platform abstraction layer for SC1000 hardware
// This is the fixed hardware interface - components here won't change
#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include "i2c.h"
#include "gpio.h"
#include "encoder.h"
#include "pic.h"

namespace sc {
namespace platform {

// Combined hardware state for the SC1000 board
struct HardwareState {
    GpioState gpio;
    EncoderState encoder;
    PicState pic;
};

// Initialize all platform hardware
// Returns true if critical components initialized successfully
inline bool platform_init(HardwareState* hw)
{
    bool ok = true;

    // GPIO is optional but nice to have
    gpio_init_mcp23017(&hw->gpio);
    gpio_init_a13_mmap(&hw->gpio);

    // Encoder is critical for scratch control
    if (!encoder_init(&hw->encoder)) {
        ok = false;
    }

    // PIC is critical for ADCs and buttons
    if (!pic_init(&hw->pic)) {
        ok = false;
    }

    return ok;
}

} // namespace platform
} // namespace sc

#endif
