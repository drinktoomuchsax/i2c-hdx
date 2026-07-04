#ifndef I2C_HDX_PORT_H
#define I2C_HDX_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 平台适配接口 — 移植时实现以下函数即可。
 */

/* Master 端: 向 slave 写入 data (len 字节) */
void i2c_hdx_port_write(const uint8_t *data, uint16_t len);

/* Master 端: 从 slave 读取最多 max_len 字节到 buf, 返回实际读到的字节数 */
uint16_t i2c_hdx_port_read(uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* I2C_HDX_PORT_H */
