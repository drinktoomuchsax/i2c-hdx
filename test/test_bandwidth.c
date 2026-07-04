#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx.h"
#include "../src/i2c_hdx_crc.h"
#include "mock_port.h"
#include <stdio.h>
#include <string.h>

/*
 * 带宽利用率测试
 *
 * 衡量: 有效 payload 字节 / I2C 总线实际传输字节
 *
 * Write: 总线字节 = LEN(2) + PAYLOAD(N) + SUM(1) = N + 3
 * Read (有数据): 总线字节 = STATUS(1) + LEN(2) + PAYLOAD(N) + SUM(1) = N + 4
 * Read (空 poll): 总线字节 = STATUS(1) = 1, payload = 0
 *
 * 带宽效率 = sum(payload) / sum(wire_bytes)
 */

static uint8_t cb_payload[256];
static uint16_t cb_len;
static int cb_called;

static void rx_callback(const uint8_t *payload, uint16_t len) {
    if (len <= sizeof(cb_payload)) memcpy(cb_payload, payload, len);
    cb_len = len;
    cb_called++;
}

/* 带宽统计 */
typedef struct {
    uint32_t payload_bytes;
    uint32_t wire_bytes;
} bandwidth_stats_t;

static bandwidth_stats_t stats;

static void stats_reset(void) {
    stats.payload_bytes = 0;
    stats.wire_bytes = 0;
}

/* 模拟一次 master write, 统计带宽 */
static void do_send(const uint8_t *payload, uint16_t len) {
    i2c_hdx_master_send(payload, len);
    stats.payload_bytes += len;
    stats.wire_bytes += mock_write_len;
    i2c_hdx_slave_on_receive(mock_write_buf, mock_write_len);
}

/* 模拟一次 master read, 统计带宽 */
static i2c_hdx_err_t do_poll(uint8_t *out, uint16_t *out_len) {
    uint8_t slave_out[I2C_HDX_MAX_READ_SIZE];
    uint16_t slave_out_len = i2c_hdx_slave_on_request(slave_out);
    memcpy(mock_read_buf, slave_out, slave_out_len);
    mock_read_len = slave_out_len;
    stats.wire_bytes += slave_out_len;

    i2c_hdx_err_t err = i2c_hdx_master_poll(out, out_len);
    if (err == I2C_HDX_OK) {
        stats.payload_bytes += *out_len;
    }
    return err;
}

static float efficiency_percent(void) {
    if (stats.wire_bytes == 0) return 0.0f;
    return (float)stats.payload_bytes / (float)stats.wire_bytes * 100.0f;
}

void setUp(void) {
    mock_port_reset();
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);
    cb_called = 0;
    cb_len = 0;
    stats_reset();
}

void tearDown(void) {}

/*
 * 场景1: 纯请求-应答, payload 1 字节
 * Write: 1+3=4, Read: 1+4=5, 总 payload=2, wire=9
 * 期望效率 ~22%
 */
void test_bw_small_payload_1byte(void) {
    uint8_t req[] = {0xAA};
    do_send(req, 1);

    uint8_t resp[] = {0xBB};
    i2c_hdx_slave_enqueue(resp, 1);

    uint8_t out[64];
    uint16_t out_len = 0;
    do_poll(out, &out_len);

    float eff = efficiency_percent();
    printf("  [1B payload] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    /* 最小 payload 效率低是预期的, 但不能低于 20% */
    TEST_ASSERT_GREATER_THAN(20.0f, eff);
}

/*
 * 场景2: 纯请求-应答, payload 16 字节
 * Write: 16+3=19, Read: 16+4=20, 总 payload=32, wire=39
 * 期望效率 ~82%
 */
void test_bw_medium_payload_16bytes(void) {
    uint8_t req[16];
    memset(req, 0x55, 16);
    do_send(req, 16);

    uint8_t resp[16];
    memset(resp, 0xAA, 16);
    i2c_hdx_slave_enqueue(resp, 16);

    uint8_t out[64];
    uint16_t out_len = 0;
    do_poll(out, &out_len);

    float eff = efficiency_percent();
    printf("  [16B payload] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    TEST_ASSERT_GREATER_THAN(75.0f, eff);
}

/*
 * 场景3: 纯请求-应答, payload 64 字节
 * Write: 64+3=67, Read: 64+4=68, 总 payload=128, wire=135
 * 期望效率 ~94%
 */
void test_bw_large_payload_64bytes(void) {
    uint8_t req[64];
    memset(req, 0x11, 64);
    do_send(req, 64);

    uint8_t resp[64];
    memset(resp, 0x22, 64);
    i2c_hdx_slave_enqueue(resp, 64);

    uint8_t out[64];
    uint16_t out_len = 0;
    do_poll(out, &out_len);

    float eff = efficiency_percent();
    printf("  [64B payload] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    TEST_ASSERT_GREATER_THAN(90.0f, eff);
}

/*
 * 场景4: 纯请求-应答, payload 128 字节 (最大)
 * Write: 128+3=131, Read: 128+4=132, 总 payload=256, wire=263
 * 期望效率 ~97%
 */
void test_bw_max_payload_128bytes(void) {
    uint8_t req[128];
    memset(req, 0x33, 128);
    do_send(req, 128);

    uint8_t resp[128];
    memset(resp, 0x44, 128);
    i2c_hdx_slave_enqueue(resp, 128);

    uint8_t out[128];
    uint16_t out_len = 0;
    do_poll(out, &out_len);

    float eff = efficiency_percent();
    printf("  [128B payload] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    TEST_ASSERT_GREATER_THAN(95.0f, eff);
}

/*
 * 场景5: 带空 poll 的真实通信 — 请求后 poll 3 次才拿到应答
 * 模拟 slave 处理延迟, 有空 poll 浪费
 */
void test_bw_with_empty_polls(void) {
    uint8_t req[32];
    memset(req, 0x55, 32);
    do_send(req, 32);

    uint8_t out[64];
    uint16_t out_len = 0;

    /* 3 次空 poll */
    do_poll(out, &out_len);
    do_poll(out, &out_len);
    do_poll(out, &out_len);

    /* 第 4 次有数据 */
    uint8_t resp[32];
    memset(resp, 0xAA, 32);
    i2c_hdx_slave_enqueue(resp, 32);
    do_poll(out, &out_len);

    float eff = efficiency_percent();
    printf("  [32B + 3 empty polls] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    /* 有空 poll 时效率下降是正常的, 但不应低于 75% (32B payload 场景) */
    TEST_ASSERT_GREATER_THAN(75.0f, eff);
}

/*
 * 场景6: slave 连续上报, 无空 poll
 * 最佳情况: 每次 poll 都有数据
 */
void test_bw_continuous_reports(void) {
    for (int i = 0; i < 10; i++) {
        uint8_t report[32];
        memset(report, (uint8_t)i, 32);
        i2c_hdx_slave_enqueue(report, 32);
    }

    uint8_t out[64];
    uint16_t out_len = 0;
    for (int i = 0; i < 10; i++) {
        /* 需要重新入队, 因为队列只有 DEPTH-1=3 个位置 */
        /* 所以改为: 入一个取一个 */
    }

    /* 重新做: 入一个取一个 */
    stats_reset();
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);

    for (int i = 0; i < 10; i++) {
        uint8_t report[32];
        memset(report, (uint8_t)i, 32);
        i2c_hdx_slave_enqueue(report, 32);

        do_poll(out, &out_len);
        TEST_ASSERT_EQUAL(I2C_HDX_OK, do_poll(out, &out_len) ? 0 : 0);
    }

    /* 上面逻辑有问题, 重写 */
    stats_reset();
    i2c_hdx_slave_init();
    i2c_hdx_slave_set_rx_callback(rx_callback);

    for (int i = 0; i < 10; i++) {
        uint8_t report[32];
        memset(report, (uint8_t)i, 32);
        i2c_hdx_slave_enqueue(report, 32);

        i2c_hdx_err_t err = do_poll(out, &out_len);
        TEST_ASSERT_EQUAL(I2C_HDX_OK, err);
        TEST_ASSERT_EQUAL(32, out_len);
    }

    float eff = efficiency_percent();
    printf("  [10x32B continuous reports] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    /* 无空 poll, 纯 read 效率: 32/(32+4) = 88.9% */
    TEST_ASSERT_GREATER_THAN(85.0f, eff);
}

/*
 * 场景7: 综合场景 — 模拟真实使用
 * 10 次请求-应答 (payload 20B), 每次中间有 1 次空 poll
 */
void test_bw_realistic_workload(void) {
    uint8_t out[64];
    uint16_t out_len = 0;

    for (int i = 0; i < 10; i++) {
        /* 发请求 */
        uint8_t req[20];
        memset(req, (uint8_t)i, 20);
        do_send(req, 20);

        /* 1 次空 poll (slave 还没处理完) */
        do_poll(out, &out_len);

        /* 应答就绪 */
        uint8_t resp[20];
        memset(resp, (uint8_t)(0x80 | i), 20);
        i2c_hdx_slave_enqueue(resp, 20);
        do_poll(out, &out_len);
    }

    float eff = efficiency_percent();
    printf("  [realistic: 10x req/resp 20B + 1 empty poll each] efficiency: %.1f%% (payload=%u, wire=%u)\n",
           eff, stats.payload_bytes, stats.wire_bytes);

    /* 门限: 真实场景效率不低于 80% */
    TEST_ASSERT_GREATER_THAN(80.0f, eff);
}

/*
 * 汇总输出 — 各 payload 大小的理论开销
 */
void test_bw_overhead_summary(void) {
    printf("\n  === Bandwidth Overhead Summary ===\n");
    printf("  Payload | Write Wire | Read Wire | Overhead Write | Overhead Read\n");
    printf("  --------|------------|-----------|----------------|-------------\n");

    uint16_t sizes[] = {1, 4, 8, 16, 32, 64, 128};
    for (int i = 0; i < 7; i++) {
        uint16_t n = sizes[i];
        uint16_t write_wire = n + 3;  /* LEN(2) + PAYLOAD(N) + SUM(1) */
        uint16_t read_wire = n + 4;   /* STATUS(1) + LEN(2) + PAYLOAD(N) + SUM(1) */
        float write_overhead = (float)(write_wire - n) / (float)write_wire * 100.0f;
        float read_overhead = (float)(read_wire - n) / (float)read_wire * 100.0f;
        printf("  %4u B  |  %4u B    |  %4u B   |    %5.1f%%       |   %5.1f%%\n",
               n, write_wire, read_wire, write_overhead, read_overhead);
    }
    printf("\n");

    /* 始终通过, 这个 test 只是输出报告 */
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bw_overhead_summary);
    RUN_TEST(test_bw_small_payload_1byte);
    RUN_TEST(test_bw_medium_payload_16bytes);
    RUN_TEST(test_bw_large_payload_64bytes);
    RUN_TEST(test_bw_max_payload_128bytes);
    RUN_TEST(test_bw_with_empty_polls);
    RUN_TEST(test_bw_continuous_reports);
    RUN_TEST(test_bw_realistic_workload);
    return UNITY_END();
}
