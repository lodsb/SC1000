// PIC input processor interface

#include "pic.h"
#include "i2c.h"

#include <cstdio>
#include "../util/log.h"

namespace sc {
namespace platform {

// PIC I2C address
constexpr uint8_t PIC_ADDR = 0x69;

bool pic_init(PicState* state)
{
    state->i2c_fd = i2c_open("/dev/i2c-2", PIC_ADDR);
    if (state->i2c_fd < 0) {
        LOG_WARN("Couldn't init input processor (PIC)");
        state->present = false;
        return false;
    }

    state->present = true;
    return true;
}

PicReadings pic_read_all(PicState* state)
{
    PicReadings readings = {};

    if (!state->present) return readings;

    unsigned char result;

    // Read ADC low bytes (registers 0x00-0x03)
    i2c_read_reg(state->i2c_fd, 0x00, &result);
    readings.adc[0] = result;
    i2c_read_reg(state->i2c_fd, 0x01, &result);
    readings.adc[1] = result;
    i2c_read_reg(state->i2c_fd, 0x02, &result);
    readings.adc[2] = result;
    i2c_read_reg(state->i2c_fd, 0x03, &result);
    readings.adc[3] = result;

    // Read ADC high bits (register 0x04, packed)
    i2c_read_reg(state->i2c_fd, 0x04, &result);
    readings.adc[0] |= static_cast<uint16_t>((result & 0x03) << 8);
    readings.adc[1] |= static_cast<uint16_t>((result & 0x0C) << 6);
    readings.adc[2] |= static_cast<uint16_t>((result & 0x30) << 4);
    readings.adc[3] |= static_cast<uint16_t>((result & 0xC0) << 2);

    // Read buttons and capsense (register 0x05)
    i2c_read_reg(state->i2c_fd, 0x05, &result);
    readings.buttons[0] = !(result & 0x01);
    readings.buttons[1] = !((result >> 1) & 0x01);
    readings.buttons[2] = !((result >> 2) & 0x01);
    readings.buttons[3] = !((result >> 3) & 0x01);
    readings.cap_touched = (result >> 4) & 0x01;

    return readings;
}

} // namespace platform
} // namespace sc
