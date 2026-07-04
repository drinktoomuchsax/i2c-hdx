#include "i2c_hdx.h"
#include "i2c_hdx_crc.h"
#include <string.h>

static uint8_t tx_queue[I2C_HDX_TX_QUEUE_DEPTH][I2C_HDX_TX_BUF_SIZE];
static uint16_t tx_lens[I2C_HDX_TX_QUEUE_DEPTH];
static volatile uint8_t tx_head = 0;
static volatile uint8_t tx_tail = 0;

static i2c_hdx_rx_callback_t rx_callback = NULL;

void i2c_hdx_slave_init(void) {
    tx_head = 0;
    tx_tail = 0;
    rx_callback = NULL;
}

void i2c_hdx_slave_set_rx_callback(i2c_hdx_rx_callback_t cb) {
    rx_callback = cb;
}

i2c_hdx_err_t i2c_hdx_slave_enqueue(const uint8_t *payload, uint16_t len) {
    if (len > I2C_HDX_TX_BUF_SIZE) {
        return I2C_HDX_ERR_OVERFLOW;
    }
    uint8_t next_head = (tx_head + 1) % I2C_HDX_TX_QUEUE_DEPTH;
    if (next_head == tx_tail) {
        return I2C_HDX_ERR_QUEUE_FULL;
    }
    memcpy(tx_queue[tx_head], payload, len);
    tx_lens[tx_head] = len;
    tx_head = next_head;
    return I2C_HDX_OK;
}

void i2c_hdx_slave_on_receive(const uint8_t *raw, uint16_t raw_len) {
    /* 最少 3 字节: LEN(2) + SUM(1) */
    if (raw_len < 3) return;

    uint16_t payload_len = ((uint16_t)raw[0] << 8) | raw[1];
    if (payload_len + 3 != raw_len) return;

    uint8_t expected_sum = i2c_hdx_checksum(raw, raw_len - 1);
    if (expected_sum != raw[raw_len - 1]) return;

    if (rx_callback) {
        rx_callback(&raw[2], payload_len);
    }
}

uint16_t i2c_hdx_slave_on_request(uint8_t *out_buf) {
    if (tx_head == tx_tail) {
        out_buf[0] = I2C_HDX_STATUS_EMPTY;
        return 1;
    }

    uint16_t len = tx_lens[tx_tail];
    out_buf[0] = I2C_HDX_STATUS_READY;
    out_buf[1] = (uint8_t)(len >> 8);
    out_buf[2] = (uint8_t)(len & 0xFF);
    memcpy(&out_buf[3], tx_queue[tx_tail], len);

    uint8_t sum = i2c_hdx_checksum(&out_buf[1], 2 + len);
    out_buf[3 + len] = sum;

    tx_tail = (tx_tail + 1) % I2C_HDX_TX_QUEUE_DEPTH;

    return 1 + 2 + len + 1; /* status + len + payload + sum */
}
