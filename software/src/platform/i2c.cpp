// I2C communication primitives for SC1000 hardware

#include "i2c.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

namespace sc {
namespace platform {

int i2c_open(const char* path, unsigned char address)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("%s - Failed to open\n", path);
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        printf("%s - Failed to acquire bus access and/or talk to slave.\n", path);
        close(fd);
        return -1;
    }

    return fd;
}

void i2c_read_reg(int fd, unsigned char reg, unsigned char* result)
{
    *result = reg;
    if (write(fd, result, 1) != 1) {
        printf("I2C read error (write)\n");
    }

    if (read(fd, result, 1) != 1) {
        printf("I2C read error\n");
    }
}

int i2c_write_reg(int fd, unsigned char reg, unsigned char value)
{
    char buf[2];
    buf[0] = static_cast<char>(reg);
    buf[1] = static_cast<char>(value);
    if (write(fd, buf, 2) != 2) {
        printf("I2C Write Error\n");
        return 0;
    }
    return 1;
}

} // namespace platform
} // namespace sc
