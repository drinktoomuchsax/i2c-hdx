#ifndef MOCK_PORT_H
#define MOCK_PORT_H

#include <stdint.h>

#define MOCK_BUF_SIZE 256

/* Master write 捕获 */
extern uint8_t mock_write_buf[MOCK_BUF_SIZE];
extern uint16_t mock_write_len;

/* Master read 注入 */
extern uint8_t mock_read_buf[MOCK_BUF_SIZE];
extern uint16_t mock_read_len;

void mock_port_reset(void);

#endif /* MOCK_PORT_H */
