#ifndef I2C_HDX_PORT_LINUX_H
#define I2C_HDX_PORT_LINUX_H

#include <stdint.h>

int i2c_hdx_port_linux_init(const char *device, uint8_t addr);
void i2c_hdx_port_linux_deinit(void);

#endif /* I2C_HDX_PORT_LINUX_H */
