// AS5600 magnetic rotary encoder interface
#ifndef PLATFORM_ENCODER_H
#define PLATFORM_ENCODER_H

#include <cstdint>

namespace sc {
namespace platform {

struct EncoderState {
    int i2c_fd = -1;
    bool present = false;
};

// Initialize rotary encoder on I2C bus
bool encoder_init(EncoderState* state);

// Read current angle (12-bit, 0-4095)
uint16_t encoder_read_angle(EncoderState* state);

} // namespace platform
} // namespace sc

#endif
