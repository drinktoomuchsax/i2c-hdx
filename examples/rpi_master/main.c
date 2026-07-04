#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/i2c_hdx.h"
#include "i2c_hdx_port_linux.h"

#define I2C_DEVICE "/dev/i2c-1"
#define SLAVE_ADDR 0x20
#define POLL_INTERVAL_US (10 * 1000)  /* 10ms */
#define MAX_POLL_RETRIES 100

static void print_hex(const char *label, const uint8_t *data, uint16_t len) {
    printf("%s (%u bytes): ", label, len);
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

static i2c_hdx_err_t send_and_wait_response(
    const uint8_t *request, uint16_t req_len,
    uint8_t *response, uint16_t *resp_len)
{
    i2c_hdx_err_t err = i2c_hdx_master_send(request, req_len);
    if (err != I2C_HDX_OK) {
        printf("send failed: %d\n", err);
        return err;
    }

    print_hex("TX", request, req_len);

    for (int retry = 0; retry < MAX_POLL_RETRIES; retry++) {
        usleep(POLL_INTERVAL_US);

        err = i2c_hdx_master_poll(response, resp_len);
        if (err == I2C_HDX_OK) {
            print_hex("RX", response, *resp_len);
            return I2C_HDX_OK;
        }
        if (err != I2C_HDX_ERR_NO_DATA) {
            printf("poll error: %d\n", err);
            return err;
        }
    }

    printf("timeout: no response after %d polls\n", MAX_POLL_RETRIES);
    return I2C_HDX_ERR_NO_DATA;
}

static void demo_request_response(void) {
    printf("\n=== Request-Response Demo ===\n");

    uint8_t request[] = {0x55, 0x10, 0x01, 0x00, 0x0A};
    uint8_t response[128];
    uint16_t resp_len = 0;

    i2c_hdx_err_t err = send_and_wait_response(
        request, sizeof(request), response, &resp_len);

    if (err == I2C_HDX_OK) {
        printf("success: got %u byte response\n", resp_len);
    }
}

static void demo_poll_reports(void) {
    printf("\n=== Poll Slave Reports Demo ===\n");
    printf("polling for slave reports (Ctrl+C to stop)...\n");

    uint8_t response[128];
    uint16_t resp_len = 0;
    int count = 0;

    for (int i = 0; i < 100; i++) {
        usleep(POLL_INTERVAL_US);

        i2c_hdx_err_t err = i2c_hdx_master_poll(response, &resp_len);
        if (err == I2C_HDX_OK) {
            count++;
            printf("[%d] ", count);
            print_hex("REPORT", response, resp_len);
        }
    }

    printf("received %d reports in %d polls\n", count, 100);
}

int main(int argc, char *argv[]) {
    const char *device = I2C_DEVICE;
    uint8_t addr = SLAVE_ADDR;

    if (argc > 1) device = argv[1];
    if (argc > 2) addr = (uint8_t)strtol(argv[2], NULL, 0);

    printf("i2c-hdx master demo\n");
    printf("device: %s, slave addr: 0x%02X\n", device, addr);

    if (i2c_hdx_port_linux_init(device, addr) != 0) {
        return 1;
    }

    demo_request_response();
    demo_poll_reports();

    i2c_hdx_port_linux_deinit();
    printf("\ndone.\n");
    return 0;
}
