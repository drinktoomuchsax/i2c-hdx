#ifndef I2C_HDX_CRC_H
#define I2C_HDX_CRC_H

#include <stdint.h>

/* 简单 sum 校验：所有字节累加取低 8 位 */
static inline uint8_t i2c_hdx_checksum(const uint8_t *data, uint16_t len) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

#endif /* I2C_HDX_CRC_H */
