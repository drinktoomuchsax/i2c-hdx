#ifndef I2C_HDX_CONFIG_H
#define I2C_HDX_CONFIG_H

/* Slave 接收 buffer 大小 (字节) */
#ifndef I2C_HDX_RX_BUF_SIZE
#define I2C_HDX_RX_BUF_SIZE 128
#endif

/* Slave 发送 buffer 大小 (字节) */
#ifndef I2C_HDX_TX_BUF_SIZE
#define I2C_HDX_TX_BUF_SIZE 128
#endif

/* Slave 发送队列深度 */
#ifndef I2C_HDX_TX_QUEUE_DEPTH
#define I2C_HDX_TX_QUEUE_DEPTH 4
#endif

/* Master poll 间隔 (毫秒), 仅作参考值 */
#ifndef I2C_HDX_POLL_INTERVAL_MS
#define I2C_HDX_POLL_INTERVAL_MS 10
#endif

/* Master 单次 read 最大长度 */
#ifndef I2C_HDX_MAX_READ_SIZE
#define I2C_HDX_MAX_READ_SIZE (1 + I2C_HDX_TX_BUF_SIZE)
#endif

#endif /* I2C_HDX_CONFIG_H */
