#include "../third_party/unity/unity.h"
#include "../src/i2c_hdx_crc.h"

void setUp(void) {}
void tearDown(void) {}

void test_checksum_empty(void) {
    uint8_t sum = i2c_hdx_checksum(NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0x00, sum);
}

void test_checksum_single_byte(void) {
    uint8_t data[] = {0x55};
    TEST_ASSERT_EQUAL_HEX8(0x55, i2c_hdx_checksum(data, 1));
}

void test_checksum_multi_byte(void) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_HEX8(0x06, i2c_hdx_checksum(data, 3));
}

void test_checksum_overflow_wrap(void) {
    uint8_t data[] = {0xFF, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0x00, i2c_hdx_checksum(data, 2));
}

void test_checksum_all_ff(void) {
    uint8_t data[] = {0xFF, 0xFF, 0xFF};
    /* 0xFF + 0xFF + 0xFF = 0x2FD, 低8位 = 0xFD */
    TEST_ASSERT_EQUAL_HEX8(0xFD, i2c_hdx_checksum(data, 3));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_empty);
    RUN_TEST(test_checksum_single_byte);
    RUN_TEST(test_checksum_multi_byte);
    RUN_TEST(test_checksum_overflow_wrap);
    RUN_TEST(test_checksum_all_ff);
    return UNITY_END();
}
