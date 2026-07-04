#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
#include "mock_port.h"

void setUp(void) { mock_port_reset(); }
void tearDown(void) {}

void test_master_send_format(void) {
    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    i2c_hdx_err_t err = i2c_hdx_master_send(payload, 3);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    /* LEN = 0x00 0x03 */
    TEST_ASSERT_EQUAL_HEX8(0x00, mock_write_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, mock_write_buf[1]);
    /* PAYLOAD */
    TEST_ASSERT_EQUAL_HEX8(0xAA, mock_write_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, mock_write_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, mock_write_buf[4]);
    /* SUM = checksum(LEN + PAYLOAD) */
    uint8_t expected_sum = i2c_hdx_checksum(mock_write_buf, 5);
    TEST_ASSERT_EQUAL_HEX8(expected_sum, mock_write_buf[5]);
    /* 总长度 = 2 + 3 + 1 = 6 */
    TEST_ASSERT_EQUAL(6, mock_write_len);
}

void test_master_send_overflow(void) {
    uint8_t payload[I2C_HDX_TX_BUF_SIZE + 1];
    i2c_hdx_err_t err = i2c_hdx_master_send(payload, I2C_HDX_TX_BUF_SIZE + 1);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_OVERFLOW, err);
}

void test_master_poll_empty(void) {
    mock_read_buf[0] = I2C_HDX_STATUS_EMPTY;
    mock_read_len = 1;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

void test_master_poll_valid_response(void) {
    /* 构造: [STATUS=0x01] [LEN=0x00 0x02] [PAYLOAD=0xDE 0xAD] [SUM] */
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_buf[2] = 0x02;
    mock_read_buf[3] = 0xDE;
    mock_read_buf[4] = 0xAD;
    mock_read_buf[5] = i2c_hdx_checksum(&mock_read_buf[1], 4);
    mock_read_len = 6;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(2, out_len);
    TEST_ASSERT_EQUAL_HEX8(0xDE, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, out[1]);
}

void test_master_poll_bad_checksum(void) {
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_buf[2] = 0x02;
    mock_read_buf[3] = 0xDE;
    mock_read_buf[4] = 0xAD;
    mock_read_buf[5] = 0x00; /* 错误的 checksum */
    mock_read_len = 6;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_CHECKSUM, err);
}

void test_master_poll_truncated(void) {
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_buf[2] = 0x10; /* 声称 16 字节 payload */
    mock_read_len = 4;       /* 实际只给了 4 字节 */

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_master_send_format);
    RUN_TEST(test_master_send_overflow);
    RUN_TEST(test_master_poll_empty);
    RUN_TEST(test_master_poll_valid_response);
    RUN_TEST(test_master_poll_bad_checksum);
    RUN_TEST(test_master_poll_truncated);
    return UNITY_END();
}
