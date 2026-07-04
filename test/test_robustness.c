#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
#include "mock_port.h"
#include <string.h>

static uint8_t cb_payload[256];
static uint16_t cb_len;
static int cb_called;

static void rx_callback(const uint8_t *payload, uint16_t len) {
    if (len <= sizeof(cb_payload)) {
        memcpy(cb_payload, payload, len);
    }
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

/* ============================================================
 * 一、畸形帧 — 模拟 I2C 线上干扰 / 恶意设备
 * ============================================================ */

void test_malformed_len_claims_huge_but_short_raw(void) {
    /* LEN 声称 512 字节 payload, 但 raw 只有 5 字节 */
    uint8_t raw[] = {0x02, 0x00, 0xAA, 0xBB, 0xCC};
    i2c_hdx_slave_on_receive(raw, 5);
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_malformed_raw_len_zero(void) {
    i2c_hdx_slave_on_receive(NULL, 0);
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_malformed_all_zeros(void) {
    /* SDA 卡低 — 全 0x00 */
    uint8_t raw[32];
    memset(raw, 0x00, sizeof(raw));
    i2c_hdx_slave_on_receive(raw, sizeof(raw));
    /* LEN=0x0000, payload_len=0, 期望 raw_len=3, 但 raw_len=32 != 3, 丢弃 */
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_malformed_all_ff(void) {
    /* SDA 卡高 — 全 0xFF */
    uint8_t raw[32];
    memset(raw, 0xFF, sizeof(raw));
    i2c_hdx_slave_on_receive(raw, sizeof(raw));
    /* LEN=0xFFFF → payload_len=65535, 65535+3 != 32, 丢弃 */
    TEST_ASSERT_EQUAL(0, cb_called);
}

void test_malformed_payload_contains_protocol_bytes(void) {
    /* payload 内有 0x55, 0x00, 0x01 等协议特征字节, 不应被误解析 */
    uint8_t trick_payload[] = {0x55, 0x00, 0x01, 0xFF, 0x00};
    uint8_t raw[8];
    raw[0] = 0x00;
    raw[1] = 0x05;
    memcpy(&raw[2], trick_payload, 5);
    raw[7] = i2c_hdx_checksum(raw, 7);

    i2c_hdx_slave_on_receive(raw, 8);

    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL(5, cb_len);
    TEST_ASSERT_EQUAL_MEMORY(trick_payload, cb_payload, 5);
}

void test_malformed_single_byte_raw(void) {
    uint8_t raw[] = {0x00};
    i2c_hdx_slave_on_receive(raw, 1);
    TEST_ASSERT_EQUAL(0, cb_called);
}

/* ============================================================
 * 二、环形队列边界
 * ============================================================ */

void test_queue_wraparound_fill_drain_refill(void) {
    uint8_t out[I2C_HDX_MAX_READ_SIZE];

    /* 填满队列 (DEPTH-1 个) */
    for (int i = 0; i < I2C_HDX_TX_QUEUE_DEPTH - 1; i++) {
        uint8_t payload[] = {(uint8_t)i};
        TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_slave_enqueue(payload, 1));
    }

    /* 取走一个 */
    uint16_t len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]); /* 第一个入队的 */

    /* 再塞一个 (head 回绕) */
    uint8_t new_payload[] = {0xAA};
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_slave_enqueue(new_payload, 1));

    /* 按 FIFO 取完剩余 */
    for (int i = 1; i < I2C_HDX_TX_QUEUE_DEPTH - 1; i++) {
        len = i2c_hdx_slave_on_request(out);
        TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)i, out[3]);
    }

    /* 最后一个是回绕后塞进去的 */
    len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[3]);

    /* 空了 */
    len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
}

void test_queue_multiple_wraparound_cycles(void) {
    uint8_t out[I2C_HDX_MAX_READ_SIZE];

    /* 连续 fill+drain 10 轮, 检测累积错误 */
    for (int round = 0; round < 10; round++) {
        /* 填满 */
        for (int i = 0; i < I2C_HDX_TX_QUEUE_DEPTH - 1; i++) {
            uint8_t payload[] = {(uint8_t)(round * 10 + i)};
            TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_slave_enqueue(payload, 1));
        }
        /* 全部取出, 验证顺序 */
        for (int i = 0; i < I2C_HDX_TX_QUEUE_DEPTH - 1; i++) {
            i2c_hdx_slave_on_request(out);
            TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(round * 10 + i), out[3]);
        }
        /* 确认空 */
        uint16_t len = i2c_hdx_slave_on_request(out);
        TEST_ASSERT_EQUAL(1, len);
        TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
    }
}

void test_queue_read_beyond_depth(void) {
    /* 只入队 2 帧, 但读 5 次 */
    uint8_t p1[] = {0x11};
    uint8_t p2[] = {0x22};
    i2c_hdx_slave_enqueue(p1, 1);
    i2c_hdx_slave_enqueue(p2, 1);

    uint8_t out[I2C_HDX_MAX_READ_SIZE];

    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(0x11, out[3]);

    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(0x22, out[3]);

    /* 后续 3 次全部返回 EMPTY, 不读到脏数据 */
    for (int i = 0; i < 3; i++) {
        uint16_t len = i2c_hdx_slave_on_request(out);
        TEST_ASSERT_EQUAL(1, len);
        TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
    }
}

/* ============================================================
 * 三、Checksum = 0 合法帧
 * ============================================================ */

void test_checksum_zero_is_valid(void) {
    /* 找一个 payload 使得 sum(LEN + PAYLOAD) = 0x00 */
    /* LEN=0x0001, PAYLOAD={0xFF} → sum = 0x00 + 0x01 + 0xFF = 0x00 (溢出) */
    uint8_t raw[] = {0x00, 0x01, 0xFF, 0x00};
    /* 验证 checksum 确实是 0 */
    TEST_ASSERT_EQUAL_HEX8(0x00, i2c_hdx_checksum(raw, 3));

    i2c_hdx_slave_on_receive(raw, 4);

    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL(1, cb_len);
    TEST_ASSERT_EQUAL_HEX8(0xFF, cb_payload[0]);
}

void test_checksum_zero_master_poll(void) {
    /* 构造一个 slave 应答, 其 checksum 恰好为 0 */
    /* STATUS + LEN(0x00 0x01) + PAYLOAD(0xFF) + SUM(0x00) */
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_buf[2] = 0x01;
    mock_read_buf[3] = 0xFF;
    mock_read_buf[4] = i2c_hdx_checksum(&mock_read_buf[1], 3);
    mock_read_len = 5;

    TEST_ASSERT_EQUAL_HEX8(0x00, mock_read_buf[4]);

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(1, out_len);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
}

/* ============================================================
 * 四、Port 层异常 — 设备 NACK / 传输中断
 * ============================================================ */

void test_port_read_returns_zero(void) {
    /* I2C NACK, 设备不在线 */
    mock_read_len = 0;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

void test_port_read_only_status_byte_ready_but_no_data(void) {
    /* 只收到 STATUS=READY 但后面没了 (传输中断) */
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_len = 1;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

void test_port_read_partial_header(void) {
    /* 只收到 STATUS + LEN 的一部分 */
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_len = 2;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

void test_port_read_extra_trailing_bytes(void) {
    /* slave 多发了几个字节 (buffer 残留), 应只解析帧内数据 */
    mock_read_buf[0] = I2C_HDX_STATUS_READY;
    mock_read_buf[1] = 0x00;
    mock_read_buf[2] = 0x02;
    mock_read_buf[3] = 0xAA;
    mock_read_buf[4] = 0xBB;
    mock_read_buf[5] = i2c_hdx_checksum(&mock_read_buf[1], 4);
    /* 多余字节 */
    mock_read_buf[6] = 0xDE;
    mock_read_buf[7] = 0xAD;
    mock_read_len = 8;

    uint8_t out[64];
    uint16_t out_len = 0;
    i2c_hdx_err_t err = i2c_hdx_master_poll(out, &out_len);

    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(2, out_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, out[1]);
}

/* ============================================================
 * 五、状态残留 / 重入
 * ============================================================ */

void test_slave_init_clears_queue(void) {
    uint8_t payload[] = {0xAB};
    i2c_hdx_slave_enqueue(payload, 1);

    /* 重新 init */
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);

    uint8_t out[I2C_HDX_MAX_READ_SIZE];
    uint16_t len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
}

void test_slave_init_idempotent(void) {
    i2c_hdx_slave_init();
    i2c_hdx_slave_init();
    i2c_hdx_slave_init();

    uint8_t out[I2C_HDX_MAX_READ_SIZE];
    uint16_t len = i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_EMPTY, out[0]);
}

void test_new_request_while_response_pending(void) {
    /* Slave 有一个待取的 response */
    uint8_t resp[] = {0xAA};
    i2c_hdx_slave_enqueue(resp, 1);

    /* 此时 master 又发了一个新 request */
    uint8_t raw[4];
    raw[0] = 0x00;
    raw[1] = 0x01;
    raw[2] = 0xBB;
    raw[3] = i2c_hdx_checksum(raw, 3);
    i2c_hdx_slave_on_receive(raw, 4);

    /* 回调正常触发 */
    TEST_ASSERT_EQUAL(1, cb_called);
    TEST_ASSERT_EQUAL_HEX8(0xBB, cb_payload[0]);

    /* 之前的 response 仍在队列, 可以取出 */
    uint8_t out[I2C_HDX_MAX_READ_SIZE];
    i2c_hdx_slave_on_request(out);
    TEST_ASSERT_EQUAL_HEX8(I2C_HDX_STATUS_READY, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[3]);
}

/* ============================================================
 * 六、压力测试 — 长时间运行检测状态泄漏
 * ============================================================ */

static void simulate_write_to_slave(void) {
    i2c_hdx_slave_on_receive(mock_write_buf, mock_write_len);
}

static i2c_hdx_err_t simulate_read_from_slave(uint8_t *out, uint16_t *out_len) {
    uint8_t slave_out[I2C_HDX_MAX_READ_SIZE];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;
    return i2c_hdx_master_poll(out, out_len);
}

void test_stress_100_request_response_cycles(void) {
    uint8_t out[I2C_HDX_TX_BUF_SIZE];
    uint16_t out_len = 0;

    for (int i = 0; i < 100; i++) {
        /* 构造变长 payload */
        uint8_t payload_len = (uint8_t)((i % 64) + 1);
        uint8_t request[64];
        for (int j = 0; j < payload_len; j++) {
            request[j] = (uint8_t)((i + j) & 0xFF);
        }

        /* Master 发送 */
        i2c_hdx_master_send(request, payload_len);
        simulate_write_to_slave();

        /* 验证 slave 收到 */
        TEST_ASSERT_EQUAL(i + 1, cb_called);
        TEST_ASSERT_EQUAL(payload_len, cb_len);
        TEST_ASSERT_EQUAL_MEMORY(request, cb_payload, payload_len);

        /* Slave 回应 (payload 反转) */
        uint8_t response[64];
        for (int j = 0; j < payload_len; j++) {
            response[j] = request[payload_len - 1 - j];
        }
        i2c_hdx_slave_enqueue(response, payload_len);

        /* Master poll 取回 */
        i2c_hdx_err_t err = simulate_read_from_slave(out, &out_len);
        TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
        TEST_ASSERT_EQUAL(payload_len, out_len);
        TEST_ASSERT_EQUAL_MEMORY(response, out, payload_len);
    }

    /* 100 轮后队列必须为空 */
    i2c_hdx_err_t err = simulate_read_from_slave(out, &out_len);
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, err);
}

void test_stress_alternating_report_and_request(void) {
    uint8_t out[I2C_HDX_TX_BUF_SIZE];
    uint16_t out_len = 0;

    for (int i = 0; i < 50; i++) {
        /* 偶数轮: slave 主动上报 */
        if (i % 2 == 0) {
            uint8_t report[] = {0xF0, (uint8_t)i};
            i2c_hdx_slave_enqueue(report, 2);

            i2c_hdx_err_t err = simulate_read_from_slave(out, &out_len);
            TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
            TEST_ASSERT_EQUAL(2, out_len);
            TEST_ASSERT_EQUAL_HEX8(0xF0, out[0]);
            TEST_ASSERT_EQUAL_HEX8((uint8_t)i, out[1]);
        }
        /* 奇数轮: master 请求 → slave 应答 */
        else {
            uint8_t request[] = {0x0F, (uint8_t)i};
            i2c_hdx_master_send(request, 2);
            simulate_write_to_slave();

            uint8_t response[] = {0x8F, (uint8_t)i};
            i2c_hdx_slave_enqueue(response, 2);

            i2c_hdx_err_t err = simulate_read_from_slave(out, &out_len);
            TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
            TEST_ASSERT_EQUAL_HEX8(0x8F, out[0]);
            TEST_ASSERT_EQUAL_HEX8((uint8_t)i, out[1]);
        }
    }
}

/* ============================================================
 * 七、边界值
 * ============================================================ */

void test_boundary_payload_one_byte(void) {
    uint8_t payload[] = {0x42};
    i2c_hdx_master_send(payload, 1);

    /* 验证帧格式: LEN(0x00 0x01) + PAYLOAD(0x42) + SUM */
    TEST_ASSERT_EQUAL(4, mock_write_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, mock_write_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, mock_write_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x42, mock_write_buf[2]);
    uint8_t expected_sum = i2c_hdx_checksum(mock_write_buf, 3);
    TEST_ASSERT_EQUAL_HEX8(expected_sum, mock_write_buf[3]);
}

void test_boundary_payload_max_minus_one(void) {
    uint8_t payload[I2C_HDX_TX_BUF_SIZE - 1];
    memset(payload, 0xAB, sizeof(payload));

    i2c_hdx_err_t err = i2c_hdx_master_send(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(2 + sizeof(payload) + 1, mock_write_len);
}

void test_boundary_payload_exactly_max(void) {
    uint8_t payload[I2C_HDX_TX_BUF_SIZE];
    memset(payload, 0xCD, sizeof(payload));

    i2c_hdx_err_t err = i2c_hdx_master_send(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
    TEST_ASSERT_EQUAL(2 + sizeof(payload) + 1, mock_write_len);

    /* 验证 checksum 位置正确 (在最后一个字节) */
    uint8_t expected_sum = i2c_hdx_checksum(mock_write_buf, 2 + sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(expected_sum, mock_write_buf[2 + sizeof(payload)]);
}

int main(void) {
    UNITY_BEGIN();

    /* 畸形帧 */
    RUN_TEST(test_malformed_len_claims_huge_but_short_raw);
    RUN_TEST(test_malformed_raw_len_zero);
    RUN_TEST(test_malformed_all_zeros);
    RUN_TEST(test_malformed_all_ff);
    RUN_TEST(test_malformed_payload_contains_protocol_bytes);
    RUN_TEST(test_malformed_single_byte_raw);

    /* 环形队列 */
    RUN_TEST(test_queue_wraparound_fill_drain_refill);
    RUN_TEST(test_queue_multiple_wraparound_cycles);
    RUN_TEST(test_queue_read_beyond_depth);

    /* Checksum 边界 */
    RUN_TEST(test_checksum_zero_is_valid);
    RUN_TEST(test_checksum_zero_master_poll);

    /* Port 异常 */
    RUN_TEST(test_port_read_returns_zero);
    RUN_TEST(test_port_read_only_status_byte_ready_but_no_data);
    RUN_TEST(test_port_read_partial_header);
    RUN_TEST(test_port_read_extra_trailing_bytes);

    /* 状态残留 */
    RUN_TEST(test_slave_init_clears_queue);
    RUN_TEST(test_slave_init_idempotent);
    RUN_TEST(test_new_request_while_response_pending);

    /* 压力 */
    RUN_TEST(test_stress_100_request_response_cycles);
    RUN_TEST(test_stress_alternating_report_and_request);

    /* 边界值 */
    RUN_TEST(test_boundary_payload_one_byte);
    RUN_TEST(test_boundary_payload_max_minus_one);
    RUN_TEST(test_boundary_payload_exactly_max);

    return UNITY_END();
}
