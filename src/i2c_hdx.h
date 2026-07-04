#ifndef I2C_HDX_H
#define I2C_HDX_H

#include <stdint.h>
#include "i2c_hdx_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 传输层状态字节 */
#define I2C_HDX_STATUS_EMPTY  0x00
#define I2C_HDX_STATUS_READY  0x01
#define I2C_HDX_STATUS_ERROR  0xFF

/* 返回码 */
typedef enum {
    I2C_HDX_OK = 0,
    I2C_HDX_ERR_CHECKSUM,
    I2C_HDX_ERR_OVERFLOW,
    I2C_HDX_ERR_QUEUE_FULL,
    I2C_HDX_ERR_NO_DATA,
} i2c_hdx_err_t;

/*
 * 传输帧格式 (传输层包装，不解析 payload 内容):
 *
 * Master Write:
 * [LEN 2B (big-endian)] [PAYLOAD N bytes] [SUM 1B]
 *
 * Master Read:
 * [STATUS 1B] [LEN 2B (big-endian)] [PAYLOAD N bytes] [SUM 1B]
 *   STATUS=0x00 时后续字节无意义
 *
 * SUM 校验范围: LEN + PAYLOAD
 */

/* ============ Master API ============ */

/* 发送一帧数据给 slave (透传, 不解析 payload) */
i2c_hdx_err_t i2c_hdx_master_send(const uint8_t *payload, uint16_t len);

/* 轮询 slave, 有数据时拷贝到 out_buf, out_len 返回 payload 长度 */
i2c_hdx_err_t i2c_hdx_master_poll(uint8_t *out_buf, uint16_t *out_len);

/* ============ Slave API ============ */

/* 初始化 slave 端 */
void i2c_hdx_slave_init(void);

/* 向发送队列压入一帧待上报数据 (透传, 不解析 payload) */
i2c_hdx_err_t i2c_hdx_slave_enqueue(const uint8_t *payload, uint16_t len);

/*
 * 以下两个函数由平台 I2C ISR 回调调用:
 *
 * on_receive: master 写入完成时调用, data 为收到的原始字节 (含 LEN+PAYLOAD+SUM)
 * on_request: master 读取时调用, 填充 out_buf, 返回总字节数
 */
typedef void (*i2c_hdx_rx_callback_t)(const uint8_t *payload, uint16_t len);

/* 注册收到请求帧后的回调 (校验通过后, 剥离传输层, 只回调纯 payload) */
void i2c_hdx_slave_set_rx_callback(i2c_hdx_rx_callback_t cb);

/* I2C slave ISR 调用: master 写入完成 */
void i2c_hdx_slave_on_receive(const uint8_t *raw, uint16_t raw_len);

/* I2C slave ISR 调用: master 请求读取, 返回应填充的字节数 */
uint16_t i2c_hdx_slave_on_request(uint8_t *out_buf);

#ifdef __cplusplus
}
#endif

#endif /* I2C_HDX_H */
