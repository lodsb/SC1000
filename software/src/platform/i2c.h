// I2C communication primitives for SC1000 hardware
#ifndef PLATFORM_I2C_H
#define PLATFORM_I2C_H

namespace sc {
namespace platform {

// Open an I2C device and set slave address
// Returns file descriptor on success, -1 on failure
int i2c_open(const char* path, unsigned char address);

// Read a single byte from an I2C register
void i2c_read_reg(int fd, unsigned char reg, unsigned char* result);

// Write a single byte to an I2C register
// Returns 1 on success, 0 on failure
int i2c_write_reg(int fd, unsigned char reg, unsigned char value);

} // namespace platform
} // namespace sc

#endif
