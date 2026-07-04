#include "i2c_hdx.h"
#include "i2c_hdx_crc.h"
#include "port/i2c_hdx_port.h"
#include <string.h>

i2c_hdx_err_t i2c_hdx_master_send(const uint8_t *payload, uint16_t len) {
    if (len > I2C_HDX_TX_BUF_SIZE) {
        return I2C_HDX_ERR_OVERFLOW;
    }

    uint8_t frame[2 + I2C_HDX_TX_BUF_SIZE + 1];
    frame[0] = (uint8_t)(len >> 8);
    frame[1] = (uint8_t)(len & 0xFF);
    memcpy(&frame[2], payload, len);

    uint8_t sum = i2c_hdx_checksum(frame, 2 + len);
    frame[2 + len] = sum;

    i2c_hdx_port_write(frame, 2 + len + 1);
    return I2C_HDX_OK;
}

i2c_hdx_err_t i2c_hdx_master_poll(uint8_t *out_buf, uint16_t *out_len) {
    uint8_t raw[I2C_HDX_MAX_READ_SIZE];
    uint16_t raw_len = i2c_hdx_port_read(raw, sizeof(raw));

    if (raw_len < 1) return I2C_HDX_ERR_NO_DATA;
    if (raw[0] == I2C_HDX_STATUS_EMPTY) return I2C_HDX_ERR_NO_DATA;
    if (raw[0] == I2C_HDX_STATUS_ERROR) return I2C_HDX_ERR_CHECKSUM;

    /* raw: [STATUS 1B] [LEN 2B] [PAYLOAD NB] [SUM 1B] */
    if (raw_len < 4) return I2C_HDX_ERR_NO_DATA;

    uint16_t payload_len = ((uint16_t)raw[1] << 8) | raw[2];
    uint16_t expected_total = 1 + 2 + payload_len + 1;
    if (raw_len < expected_total) return I2C_HDX_ERR_NO_DATA;

    uint8_t expected_sum = i2c_hdx_checksum(&raw[1], 2 + payload_len);
    if (expected_sum != raw[3 + payload_len]) return I2C_HDX_ERR_CHECKSUM;

    memcpy(out_buf, &raw[3], payload_len);
    *out_len = payload_len;
    return I2C_HDX_OK;
}
