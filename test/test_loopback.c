#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
#include "mock_port.h"
#include <string.h>

static uint8_t cb_payload[128];
static uint16_t cb_len;
static int cb_called;

static void rx_callback(const uint8_t *payload, uint16_t len) {
    memcpy(cb_payload, payload, len);
    cb_len = len;
    cb_called++;
}

void setUp(void) {
    mock_port_reset();
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);
    memset(cb_payload, 0, sizeof(cb_payload));
    cb_len = 0;
    cb_called = 0;
}

void tearDown(void) {}

void test_loopback_request_response(void) {
    /* Master 发送请求 */
    uint8_t request[] = {0x55, 0x01, 0x02, 0x03, 0x04};
    i2c_hdx_master_send(request, 5);

    /* 把 master 写出的字节喂给 slave */
    i2c_hdx_slave_on_receive(mock_write_buf, mock_write_len);

    /* slave 收到了正确的 payload */
    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL(5, cb_len);
    TEST_ASSERT_EQUAL_MEMORY(request, cb_payload, 5);

    /* slave 准备应答 */
    uint8_t response[] = {0xAA, 0xBB};
    i2c_hdx_slave_enqueue(response, 2);

    /* slave 生成读取数据 */
    uint8_t slave_out[64];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);

    /* 注入到 master 的 read mock */
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;

    /* master poll 取到应答 */
    uint8_t master_out[64];
    uint16_t master_out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(master_out, &master_out_len);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(2, master_out_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, master_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, master_out[1]);
}

void test_loopback_slave_report(void) {
    /* slave 主动上报, 无需 master 先写 */
    uint8_t report[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    i2c_hdx_slave_enqueue(report, 5);

    uint8_t slave_out[64];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);

    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;

    uint8_t master_out[64];
    uint16_t master_out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(master_out, &master_out_len);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(5, master_out_len);
    TEST_ASSERT_EQUAL_MEMORY(report, master_out, 5);
}

void test_loopback_multiple_reports(void) {
    uint8_t r1[] = {0x11, 0x22};
    uint8_t r2[] = {0x33, 0x44, 0x55};
    i2c_hdx_slave_enqueue(r1, 2);
    i2c_hdx_slave_enqueue(r2, 3);

    /* 第一帧 */
    uint8_t slave_out[64];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;

    uint8_t master_out[64];
    uint16_t master_out_len = 0;
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(master_out, &master_out_len));
    TEST_ASSERT_EQUAL(2, master_out_len);
    TEST_ASSERT_EQUAL_MEMORY(r1, master_out, 2);

    /* 第二帧 */
    slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;

    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(master_out, &master_out_len));
    TEST_ASSERT_EQUAL(3, master_out_len);
    TEST_ASSERT_EQUAL_MEMORY(r2, master_out, 3);

    /* 队列空了 */
    slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;

    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(master_out, &master_out_len));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_loopback_request_response);
    RUN_TEST(test_loopback_slave_report);
    RUN_TEST(test_loopback_multiple_reports);
    return UNITY_END();
}
