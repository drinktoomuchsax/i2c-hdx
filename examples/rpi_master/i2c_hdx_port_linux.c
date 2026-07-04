#include "../../src/port/i2c_hdx_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

static int i2c_fd = -1;
static uint8_t slave_addr = 0x20;

int i2c_hdx_port_linux_init(const char *device, uint8_t addr) {
    slave_addr = addr;

    i2c_fd = open(device, O_RDWR);
    if (i2c_fd < 0) {
        perror("open i2c device");
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, slave_addr) < 0) {
        perror("ioctl I2C_SLAVE");
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    return 0;
}

void i2c_hdx_port_linux_deinit(void) {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}

void i2c_hdx_port_write(const uint8_t *data, uint16_t len) {
    if (i2c_fd < 0) return;

    ssize_t ret = write(i2c_fd, data, len);
    if (ret != (ssize_t)len) {
        perror("i2c write");
    }
}

uint16_t i2c_hdx_port_read(uint8_t *buf, uint16_t max_len) {
    if (i2c_fd < 0) return 0;

    ssize_t ret = read(i2c_fd, buf, max_len);
    if (ret < 0) {
        perror("i2c read");
        return 0;
    }

    return (uint16_t)ret;
}
