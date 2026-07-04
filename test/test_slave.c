#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
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
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);
    memset(cb_payload, 0, sizeof(cb_payload));
    cb_len = 0;
    cb_called = 0;
}

void tearDown(void) {}

/* ---- on_receive 测试 ---- */

void test_slave_receive_valid(void) {
    /* 构造: [LEN=0x00 0x03] [PAYLOAD=0x01 0x02 0x03] [SUM] */
    uint8_t raw[6];
    raw[0] = 0x00;
    raw[1] = 0x03;
    raw[2] = 0x01;
    raw[3] = 0x02;
    raw[4] = 0x03;
    raw[5] = i2c_hdx_checksum(raw, 5);

    i2c_hdx_slave_on_receive(raw, 6);

    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL(3, cb_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, cb_payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, cb_payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, cb_payload[2]);
}

void test_slave_receive_bad_checksum(void) {
    uint8_t raw[] = {0x00, 0x02, 0xAA, 0xBB, 0x00}; /* SUM 故意错 */
    i2c_hdx_slave_on_receive(raw, 5);
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_slave_receive_len_mismatch(void) {
    /* 声称 payload 5 字节, 实际 raw 只有 4 字节 */
    uint8_t raw[] = {0x00, 0x05, 0x01, 0x02};
    i2c_hdx_slave_on_receive(raw, 4);
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_slave_receive_too_short(void) {
    uint8_t raw[] = {0x00, 0x01};
    i2c_hdx_slave_on_receive(raw, 2);
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_slave_receive_zero_payload(void) {
    /* LEN=0, 只有 LEN(2) + SUM(1) = 3 字节 */
    uint8_t raw[3];
    raw[0] = 0x00;
    raw[1] = 0x00;
    raw[2] = i2c_hdx_checksum(raw, 2);

    i2c_hdx_slave_on_receive(raw, 3);

    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL(0, cb_len);
}

/* ---- on_request + enqueue 测试 ---- */

void test_slave_request_empty_queue(void) {
    uint8_t out[64];
    uint16_t len = i2c_hdx_slave_on_request(out);

    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
}

void test_slave_request_one_frame(void) {
    uint8_t payload[] = {0xDE, 0xAD};
    i2c_hdx_slave_enqueue(payload, 2);

    uint8_t out[64];
    uint16_t len = i2c_hdx_slave_on_request(out);

    /* STATUS(1) + LEN(2) + PAYLOAD(2) + SUM(1) = 6 */
    TEST_ASSERT_EQUAL(6, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDE, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, out[4]);

    uint8_t expected_sum = i2c_hdx_checksum(&out[1], 4);
    TEST_ASSERT_EQUAL_HEX8(expected_sum, out[5]);
}

void test_slave_request_fifo_order(void) {
    uint8_t p1[] = {0x11};
    uint8_t p2[] = {0x22};
    uint8_t p3[] = {0x33};
    i2c_hdx_slave_enqueue(p1, 1);
    i2c_hdx_slave_enqueue(p2, 1);
    i2c_hdx_slave_enqueue(p3, 1);

    uint8_t out[64];

    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(0x11, out[3]);

    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(0x22, out[3]);

    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(0x33, out[3]);
}

void test_slave_queue_full(void) {
    uint8_t payload[] = {0x01};
    /* 队列深度为 I2C_HDX_TX_QUEUE_DEPTH, 环形队列最多存 DEPTH-1 个 */
    for (int i = 0; i < I2C_HDX_TX_QUEUE_DEPTH - 1; i++) {
        TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_slave_enqueue(payload, 1));
    }
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_QUEUE_FULL, i2c_hdx_slave_enqueue(payload, 1));
}

void test_slave_request_empty_after_drain(void) {
    uint8_t payload[] = {0xAB};
    i2c_hdx_slave_enqueue(payload, 1);

    uint8_t out[64];
    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);

    uint16_t len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_slave_receive_valid);
    RUN_TEST(test_slave_receive_bad_checksum);
    RUN_TEST(test_slave_receive_len_mismatch);
    RUN_TEST(test_slave_receive_too_short);
    RUN_TEST(test_slave_receive_zero_payload);
    RUN_TEST(test_slave_request_empty_queue);
    RUN_TEST(test_slave_request_one_frame);
    RUN_TEST(test_slave_request_fifo_order);
    RUN_TEST(test_slave_queue_full);
    RUN_TEST(test_slave_request_empty_after_drain);
    return UNITY_END();
}
