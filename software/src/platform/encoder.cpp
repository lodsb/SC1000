// AS5600 magnetic rotary encoder interface

#include "encoder.h"
#include "i2c.h"

#include <cstdio>

namespace sc {
namespace platform {

// AS5600 I2C address
constexpr uint8_t AS5600_ADDR = 0x36;

// AS5600 register addresses
constexpr uint8_t AS5600_ANGLE_H = 0x0C;
constexpr uint8_t AS5600_ANGLE_L = 0x0D;

bool encoder_init(EncoderState* state)
{
    state->i2c_fd = i2c_open("/dev/i2c-0", AS5600_ADDR);
    if (state->i2c_fd < 0) {
        printf("Couldn't init rotary sensor (AS5600)\n");
        state->present = false;
        return false;
    }

    state->present = true;
    return true;
}

uint16_t encoder_read_angle(EncoderState* state)
{
    if (!state->present) return 0;

    unsigned char high, low;
    i2c_read_reg(state->i2c_fd, AS5600_ANGLE_H, &high);
    i2c_read_reg(state->i2c_fd, AS5600_ANGLE_L, &low);

    // 12-bit angle: high byte has bits 11:8, low byte has bits 7:0
    return (static_cast<uint16_t>(high & 0x0F) << 8) | low;
}

} // namespace platform
} // namespace sc
