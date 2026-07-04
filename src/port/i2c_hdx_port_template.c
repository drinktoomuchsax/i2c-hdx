#include "i2c_hdx_port.h"

/*
 * 移植模板 — 复制此文件并替换为你的平台 I2C HAL 调用。
 *
 * 示例基于伪代码，替换为实际 HAL:
 *   STM32: HAL_I2C_Master_Transmit / HAL_I2C_Master_Receive
 *   ESP-IDF: i2c_master_write_to_device / i2c_master_read_from_device
 *   RP2040: i2c_write_blocking / i2c_read_blocking
 */

#define SLAVE_ADDR 0x20  /* 你的 slave I2C 地址 */

void i2c_hdx_port_write(const uint8_t *data, uint16_t len) {
    /* TODO: 替换为平台 I2C master write */
    /* 例: HAL_I2C_Master_Transmit(&hi2c1, SLAVE_ADDR << 1, data, len, 100); */
    (void)data;
    (void)len;
}

uint16_t i2c_hdx_port_read(uint8_t *buf, uint16_t max_len) {
    /* TODO: 替换为平台 I2C master read */
    /* 例: HAL_I2C_Master_Receive(&hi2c1, SLAVE_ADDR << 1, buf, max_len, 100); */
    (void)buf;
    (void)max_len;
    return 0;
}
