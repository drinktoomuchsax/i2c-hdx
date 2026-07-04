#include "mock_port.h"
#include "../src/port/i2c_hdx_port.h"
#include <string.h>

uint8_t mock_write_buf[MOCK_BUF_SIZE];
uint16_t mock_write_len = 0;

uint8_t mock_read_buf[MOCK_BUF_SIZE];
uint16_t mock_read_len = 0;

void mock_port_reset(void) {
    memset(mock_write_buf, 0, sizeof(mock_write_buf));
    mock_write_len = 0;
    memset(mock_read_buf, 0, sizeof(mock_read_buf));
    mock_read_len = 0;
}

void i2c_hdx_port_write(const uint8_t *data, uint16_t len) {
    if (len > MOCK_BUF_SIZE) len = MOCK_BUF_SIZE;
    memcpy(mock_write_buf, data, len);
    mock_write_len = len;
}

uint16_t i2c_hdx_port_read(uint8_t *buf, uint16_t max_len) {
    uint16_t len = mock_read_len < max_len ? mock_read_len : max_len;
    memcpy(buf, mock_read_buf, len);
    return len;
}
