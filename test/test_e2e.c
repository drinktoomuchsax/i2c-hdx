#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
#include "mock_port.h"
#include <string.h>

/*
 * 端到端测试：模拟真实通信时序
 *
 * 不单独测某个函数，而是模拟 master 的 poll loop 和 slave 的处理循环，
 * 验证完整的交互流程。
 */

#define MAX_FRAMES 8

static uint8_t rx_payloads[MAX_FRAMES][128];
static uint16_t rx_lens[MAX_FRAMES];
static int rx_count;

static void slave_rx_callback(const uint8_t *payload, uint16_t len) {
    if (rx_count < MAX_FRAMES) {
        memcpy(rx_payloads[rx_count], payload, len);
        rx_lens[rx_count] = len;
        rx_count++;
    }
}

/* 模拟一次 master → slave 的 I2C write 事务 */
static void simulate_i2c_write_transaction(void) {
    i2c_hdx_slave_on_receive(mock_write_buf, mock_write_len);
}

/* 模拟一次 master ← slave 的 I2C read 事务 */
static void simulate_i2c_read_transaction(void) {
    uint8_t slave_out[I2C_HDX_MAX_READ_SIZE];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;
}

void setUp(void) {
    mock_port_reset();
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(slave_rx_callback);
    memset(rx_payloads, 0, sizeof(rx_payloads));
    memset(rx_lens, 0, sizeof(rx_lens));
    rx_count = 0;
}

void tearDown(void) {}

/*
 * 场景1: 标准请求-应答流程
 *
 * Master 发请求 → Slave 处理并准备应答 → Master poll 拿到应答
 */
void test_e2e_request_then_response(void) {
    uint8_t request[] = {0x55, 0x10, 0x01, 0x00, 0x0A};
    uint8_t response[] = {0x55, 0x10, 0x01, 0x00, 0x0A, 0xDE, 0xAD};

    /* Master 发送请求 */
    i2c_hdx_master_send(request, sizeof(request));
    simulate_i2c_write_transaction();

    TEST_ASSERT_EQUAL(1, rx_count);
    TEST_ASSERT_EQUAL_MEMORY(request, rx_payloads[0], sizeof(request));

    /* Slave 还没准备好应答, Master 先 poll 一次 */
    simulate_i2c_read_transaction();
    uint8_t out[128];
    uint16_t out_len = 0;
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(out, &out_len));

    /* Slave 处理完成, 放入应答 */
    i2c_hdx_slave_enqueue(response, sizeof(response));

    /* Master 再次 poll, 拿到应答 */
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(response), out_len);
    TEST_ASSERT_EQUAL_MEMORY(response, out, sizeof(response));
}

/*
 * 场景2: Slave 定时上报 + Master 持续 poll
 *
 * 模拟 slave 每隔几个 poll 周期上报一次数据
 */
void test_e2e_slave_periodic_report(void) {
    uint8_t report1[] = {0x01, 0x11, 0x22};
    uint8_t report2[] = {0x02, 0x33, 0x44, 0x55};
    uint8_t out[128];
    uint16_t out_len = 0;

    /* Poll 几次, 都是空的 */
    for (int i = 0; i < 3; i++) {
        simulate_i2c_read_transaction();
        TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(out, &out_len));
    }

    /* Slave 上报第一帧 */
    i2c_hdx_slave_enqueue(report1, sizeof(report1));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(report1), out_len);
    TEST_ASSERT_EQUAL_MEMORY(report1, out, sizeof(report1));

    /* 又是几轮空 poll */
    for (int i = 0; i < 5; i++) {
        simulate_i2c_read_transaction();
        TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(out, &out_len));
    }

    /* Slave 上报第二帧 */
    i2c_hdx_slave_enqueue(report2, sizeof(report2));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(report2), out_len);
    TEST_ASSERT_EQUAL_MEMORY(report2, out, sizeof(report2));
}

/*
 * 场景3: 请求-应答 + 主动上报混合
 *
 * Slave 同时有待上报的数据和对请求的应答, Master 逐个 poll 取走
 */
void test_e2e_mixed_response_and_report(void) {
    uint8_t report[] = {0xAA, 0xBB};
    uint8_t request[] = {0x55, 0x01, 0x02};
    uint8_t response[] = {0x55, 0x81, 0x02, 0xCC};
    uint8_t out[128];
    uint16_t out_len = 0;

    /* Slave 先有一帧待上报 */
    i2c_hdx_slave_enqueue(report, sizeof(report));

    /* Master 发一个请求 */
    i2c_hdx_master_send(request, sizeof(request));
    simulate_i2c_write_transaction();

    /* Slave 处理请求后放入应答 (队列里现在有: report, response) */
    i2c_hdx_slave_enqueue(response, sizeof(response));

    /* Master poll 第一次: 取到 report (FIFO) */
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(report), out_len);
    TEST_ASSERT_EQUAL_MEMORY(report, out, sizeof(report));

    /* Master poll 第二次: 取到 response */
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(response), out_len);
    TEST_ASSERT_EQUAL_MEMORY(response, out, sizeof(response));

    /* Master poll 第三次: 空了 */
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(out, &out_len));
}

/*
 * 场景4: 连续快速请求
 *
 * Master 连发多个请求, Slave 逐个处理并应答
 */
void test_e2e_burst_requests(void) {
    uint8_t req1[] = {0x01, 0x10};
    uint8_t req2[] = {0x02, 0x20};
    uint8_t req3[] = {0x03, 0x30};
    uint8_t out[128];
    uint16_t out_len = 0;

    /* Master 连发 3 个请求 */
    i2c_hdx_master_send(req1, sizeof(req1));
    simulate_i2c_write_transaction();

    i2c_hdx_master_send(req2, sizeof(req2));
    simulate_i2c_write_transaction();

    i2c_hdx_master_send(req3, sizeof(req3));
    simulate_i2c_write_transaction();

    /* Slave 收到了 3 个请求 */
    TEST_ASSERT_EQUAL(3, rx_count);
    TEST_ASSERT_EQUAL_MEMORY(req1, rx_payloads[0], sizeof(req1));
    TEST_ASSERT_EQUAL_MEMORY(req2, rx_payloads[1], sizeof(req2));
    TEST_ASSERT_EQUAL_MEMORY(req3, rx_payloads[2], sizeof(req3));

    /* Slave 逐个准备应答 */
    uint8_t resp1[] = {0x81, 0x10};
    uint8_t resp2[] = {0x82, 0x20};
    uint8_t resp3[] = {0x83, 0x30};
    i2c_hdx_slave_enqueue(resp1, sizeof(resp1));
    i2c_hdx_slave_enqueue(resp2, sizeof(resp2));
    i2c_hdx_slave_enqueue(resp3, sizeof(resp3));

    /* Master 逐个 poll 取走 */
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL_MEMORY(resp1, out, sizeof(resp1));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL_MEMORY(resp2, out, sizeof(resp2));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL_MEMORY(resp3, out, sizeof(resp3));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_ERR_NO_DATA, i2c_hdx_master_poll(out, &out_len));
}

/*
 * 场景5: 传输层校验错误后恢复
 *
 * 模拟一次坏帧 (I2C 线上干扰), slave 丢弃后继续正常工作
 */
void test_e2e_recover_after_bad_frame(void) {
    uint8_t out[128];
    uint16_t out_len = 0;

    /* 发一个坏帧给 slave (checksum 错误) */
    uint8_t bad_raw[] = {0x00, 0x03, 0xAA, 0xBB, 0xCC, 0x00}; /* sum 故意错 */
    i2c_hdx_slave_on_receive(bad_raw, sizeof(bad_raw));
    TEST_ASSERT_EQUAL(0, rx_count);

    /* Slave 状态正常, 再发一个正确的请求 */
    uint8_t good_request[] = {0x55, 0x01, 0x02};
    i2c_hdx_master_send(good_request, sizeof(good_request));
    simulate_i2c_write_transaction();

    TEST_ASSERT_EQUAL(1, rx_count);
    TEST_ASSERT_EQUAL_MEMORY(good_request, rx_payloads[0], sizeof(good_request));

    /* Slave 正常应答 */
    uint8_t response[] = {0x55, 0x81, 0x02, 0xFF};
    i2c_hdx_slave_enqueue(response, sizeof(response));

    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(sizeof(response), out_len);
    TEST_ASSERT_EQUAL_MEMORY(response, out, sizeof(response));
}

/*
 * 场景6: 大帧传输
 *
 * 接近 buffer 上限的数据能正确透传
 */
void test_e2e_large_frame(void) {
    uint8_t large_payload[I2C_HDX_TX_BUF_SIZE];
    for (int i = 0; i < I2C_HDX_TX_BUF_SIZE; i++) {
        large_payload[i] = (uint8_t)(i & 0xFF);
    }

    /* Master 发送大帧 */
    i2c_hdx_master_send(large_payload, I2C_HDX_TX_BUF_SIZE);
    simulate_i2c_write_transaction();

    TEST_ASSERT_EQUAL(1, rx_count);
    TEST_ASSERT_EQUAL(I2C_HDX_TX_BUF_SIZE, rx_lens[0]);
    TEST_ASSERT_EQUAL_MEMORY(large_payload, rx_payloads[0], I2C_HDX_TX_BUF_SIZE);

    /* Slave 用大帧应答 */
    i2c_hdx_slave_enqueue(large_payload, I2C_HDX_TX_BUF_SIZE);

    uint8_t out[I2C_HDX_TX_BUF_SIZE];
    uint16_t out_len = 0;
    simulate_i2c_read_transaction();
    TEST_ASSERT_EQUAL(I2C_HDX_OK, i2c_hdx_master_poll(out, &out_len));
    TEST_ASSERT_EQUAL(I2C_HDX_TX_BUF_SIZE, out_len);
    TEST_ASSERT_EQUAL_MEMORY(large_payload, out, I2C_HDX_TX_BUF_SIZE);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_e2e_request_then_response);
    RUN_TEST(test_e2e_slave_periodic_report);
    RUN_TEST(test_e2e_mixed_response_and_report);
    RUN_TEST(test_e2e_burst_requests);
    RUN_TEST(test_e2e_recover_after_bad_frame);
    RUN_TEST(test_e2e_large_frame);
    return UNITY_END();
}
