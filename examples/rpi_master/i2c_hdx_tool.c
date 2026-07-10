/*
 * i2c_hdx_tool — interactive bring-up / diagnostic tool for an i2c-hdx link.
 *
 * The demo in main.c shows the happy path. This tool is for the OTHER case:
 * the link doesn't work yet and you need to see why. It has two layers.
 *
 *   High level — uses the real master API (i2c_hdx_master_send/poll), so it
 *   doubles as a worked example of driving the library:
 *       send <hex>     write one request frame
 *       poll           poll once for a reported frame
 *       xfer <hex>     the core half-duplex round trip: send, then poll
 *       monitor        keep polling, print every frame the slave reports
 *       repl           interactive: type hex to send, see responses
 *
 *   Low level — talks straight to the port (i2c_hdx_port_read) and decodes
 *   the raw transport bytes the master API hides behind an err code. This is
 *   what tells "slave absent / NAK" apart from "STATUS=EMPTY" apart from "bad
 *   checksum" apart from "length mismatch":
 *       probe          one raw read, fully decoded (STATUS/LEN/SUM/payload)
 *
 * Transport frame (see src/i2c_hdx.h):
 *   Master write: [LEN 2B big-endian] [PAYLOAD N] [SUM 1B]
 *   Master read:  [STATUS 1B] [LEN 2B big-endian] [PAYLOAD N] [SUM 1B]
 *                 STATUS 0x00 EMPTY, 0x01 READY, 0xFF ERROR
 *   SUM = low 8 bits of the byte-wise sum over (LEN + PAYLOAD).
 *
 * Build: see Makefile target `i2c_hdx_master_tool`.
 */
#define _DEFAULT_SOURCE   /* usleep() under -std=c99 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "../../src/i2c_hdx.h"
#include "../../src/i2c_hdx_crc.h"      /* i2c_hdx_checksum (raw decode) */
#include "../../src/port/i2c_hdx_port.h" /* i2c_hdx_port_read (raw probe) */
#include "i2c_hdx_port_linux.h"

#define DEFAULT_DEVICE   "/dev/i2c-1"
#define DEFAULT_ADDR     0x20
#define POLL_GAP_US      (10 * 1000)   /* 10ms */
#define XFER_MAX_POLLS   50            /* 50 * 10ms = 500ms response budget */

/* Bounded by the transport buffers in i2c_hdx_config.h. */
#define MAX_PAYLOAD      I2C_HDX_TX_BUF_SIZE
#define MAX_RAW_READ     I2C_HDX_MAX_READ_SIZE

/* ------------------------------------------------------------ helpers */

static const char *status_name(uint8_t s) {
    switch (s) {
    case I2C_HDX_STATUS_EMPTY: return "EMPTY (slave has nothing queued)";
    case I2C_HDX_STATUS_READY: return "READY (frame follows)";
    case I2C_HDX_STATUS_ERROR: return "ERROR (slave signalled 0xFF)";
    default:                   return "unexpected status byte";
    }
}

static void print_hex(const char *label, const uint8_t *data, int len) {
    printf("  %s (%d bytes):", label, len);
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) printf("\n    ");
        printf("%02X ", data[i]);
    }
    printf("\n");
}

/* Parse a hex payload from argv tokens. Accepts "DEADBEEF", "DE AD BE EF",
 * "0xDE 0xAD", "de:ad". Returns byte count, or -1 on error. */
static int parse_hex(int argc, char **argv, uint8_t *out, int max) {
    char digits[2 * MAX_PAYLOAD + 1];
    size_t n = 0;
    for (int i = 0; i < argc; i++) {
        const char *s = argv[i];
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
        for (; *s; s++) {
            if (isxdigit((unsigned char)*s)) {
                if (n + 1 >= sizeof(digits)) { fprintf(stderr, "  payload too long\n"); return -1; }
                digits[n++] = *s;
            } else if (*s == ' ' || *s == ',' || *s == ':' || *s == '_') {
                continue;
            } else {
                fprintf(stderr, "  bad hex char '%c'\n", *s);
                return -1;
            }
        }
    }
    if (n % 2 != 0) { fprintf(stderr, "  odd number of hex digits (%zu)\n", n); return -1; }
    int bytes = (int)(n / 2);
    if (bytes > max) { fprintf(stderr, "  payload %d exceeds max %d\n", bytes, max); return -1; }
    for (int i = 0; i < bytes; i++) {
        char b[3] = { digits[2 * i], digits[2 * i + 1], 0 };
        out[i] = (uint8_t)strtol(b, NULL, 16);
    }
    return bytes;
}

/* ---------------------------------------------------- low-level: probe */

/*
 * One raw read straight off the port, decoded byte by byte. Returns:
 *    1  valid READY frame (payload/out_len filled)
 *    0  EMPTY / nothing to read
 *   -1  hard problem (STATUS=ERROR, checksum mismatch, truncated/short)
 * `quiet` suppresses the verbose dump (used by the polling loops).
 */
static int raw_read_decode(uint8_t *payload, int *out_len, int quiet) {
    uint8_t raw[MAX_RAW_READ];
    uint16_t n = i2c_hdx_port_read(raw, sizeof(raw));

    if (n == 0) {
        /* Port read returned nothing — either a NAK (slave has nothing / not
         * present) or a bus-level failure. The port logs the errno. */
        if (!quiet) printf("  RX: no bytes (slave NAK / not present?)\n");
        return 0;
    }
    if (!quiet) print_hex("RX raw", raw, (int)n);

    uint8_t status = raw[0];
    if (!quiet) printf("  STATUS = 0x%02X — %s\n", status, status_name(status));

    if (status == I2C_HDX_STATUS_EMPTY) return 0;
    if (status == I2C_HDX_STATUS_ERROR) return -1;
    if (status != I2C_HDX_STATUS_READY) return 0;

    if (n < 4) {
        if (!quiet) printf("  -> READY but only %u bytes (truncated)\n", n);
        return -1;
    }
    uint16_t plen = ((uint16_t)raw[1] << 8) | raw[2];
    int expected = 1 + 2 + plen + 1;
    if (plen > MAX_PAYLOAD || n < expected) {
        if (!quiet) printf("  -> length mismatch: LEN=%u, expected %d bytes, got %u\n",
                           plen, expected, n);
        return -1;
    }
    uint8_t got_sum  = raw[3 + plen];
    uint8_t calc_sum = i2c_hdx_checksum(&raw[1], 2 + plen); /* over LEN+PAYLOAD */
    if (!quiet) printf("  LEN = %u, SUM got=0x%02X calc=0x%02X -> %s\n",
                       plen, got_sum, calc_sum, got_sum == calc_sum ? "OK" : "MISMATCH");
    if (got_sum != calc_sum) return -1;

    if (payload && out_len) { memcpy(payload, &raw[3], plen); *out_len = plen; }
    if (!quiet) print_hex("RX payload", &raw[3], plen);
    return 1;
}

static int cmd_probe(void) {
    printf("[probe] one raw read, fully decoded\n");
    int rc = raw_read_decode(NULL, NULL, /*quiet=*/0);
    printf("[probe] RESULT: %s\n",
           rc > 0 ? "READY frame present" :
           rc == 0 ? "link answers, nothing queued (EMPTY / NAK)" :
                     "slave reported ERROR or malformed frame");
    return rc < 0 ? 1 : 0;
}

/* --------------------------------------------- high-level: master API */

static int cmd_send(int argc, char **argv) {
    uint8_t payload[MAX_PAYLOAD];
    int len = parse_hex(argc, argv, payload, sizeof(payload));
    if (len < 0) return 1;

    printf("[send] %d payload byte(s) via i2c_hdx_master_send()\n", len);
    print_hex("payload", payload, len);
    i2c_hdx_err_t err = i2c_hdx_master_send(payload, (uint16_t)len);
    if (err != I2C_HDX_OK) {
        printf("[send] FAILED: err=%d\n", err);
        return 1;
    }
    printf("[send] ok\n");
    return 0;
}

static int cmd_poll(void) {
    uint8_t buf[MAX_PAYLOAD];
    uint16_t len = 0;
    printf("[poll] one i2c_hdx_master_poll()\n");
    i2c_hdx_err_t err = i2c_hdx_master_poll(buf, &len);
    switch (err) {
    case I2C_HDX_OK:           print_hex("payload", buf, len); printf("[poll] READY\n"); return 0;
    case I2C_HDX_ERR_NO_DATA:  printf("[poll] no data (EMPTY / NAK)\n"); return 0;
    case I2C_HDX_ERR_CHECKSUM: printf("[poll] checksum/slave error — run `probe` for raw bytes\n"); return 1;
    default:                   printf("[poll] err=%d\n", err); return 1;
    }
}

static int cmd_xfer(int argc, char **argv) {
    uint8_t payload[MAX_PAYLOAD];
    int len = parse_hex(argc, argv, payload, sizeof(payload));
    if (len < 0) return 1;

    printf("[xfer] send %d byte(s), then poll up to %dms\n",
           len, XFER_MAX_POLLS * (POLL_GAP_US / 1000));
    print_hex("TX payload", payload, len);

    i2c_hdx_err_t err = i2c_hdx_master_send(payload, (uint16_t)len);
    if (err != I2C_HDX_OK) { printf("[xfer] send FAILED: err=%d\n", err); return 1; }

    uint8_t rx[MAX_PAYLOAD];
    uint16_t rlen = 0;
    for (int i = 0; i < XFER_MAX_POLLS; i++) {
        usleep(POLL_GAP_US);
        err = i2c_hdx_master_poll(rx, &rlen);
        if (err == I2C_HDX_OK) {
            printf("[xfer] response after %d poll(s):\n", i + 1);
            print_hex("payload", rx, rlen);
            printf("[xfer] RESULT: round trip OK\n");
            return 0;
        }
        if (err != I2C_HDX_ERR_NO_DATA) {
            printf("[xfer] poll error err=%d — run `probe` for raw bytes\n", err);
            return 1;
        }
    }
    printf("[xfer] RESULT: no response within budget (slave never went READY)\n");
    return 1;
}

static int cmd_monitor(void) {
    printf("[monitor] polling every %dms, Ctrl-C to stop\n", POLL_GAP_US / 1000);
    unsigned long frames = 0;
    for (;;) {
        uint8_t rx[MAX_PAYLOAD];
        uint16_t rlen = 0;
        i2c_hdx_err_t err = i2c_hdx_master_poll(rx, &rlen);
        if (err == I2C_HDX_OK) {
            frames++;
            printf("[monitor] frame #%lu:", frames);
            for (uint16_t i = 0; i < rlen; i++) printf(" %02X", rx[i]);
            printf("\n");
        }
        usleep(POLL_GAP_US);
    }
    return 0; /* not reached (loop exits only on Ctrl-C) */
}

static int cmd_repl(void) {
    printf("[repl] type hex to send (e.g. 'DE AD BE EF'); 'p' poll once; 'q' quit\n");
    char line[512];
    while (printf("hdx> "), fflush(stdout), fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (l == 0) continue;
        if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) break;
        if (strcmp(line, "p") == 0 || strcmp(line, "poll") == 0) { cmd_poll(); continue; }

        uint8_t payload[MAX_PAYLOAD];
        char *one[1] = { line };
        int len = parse_hex(1, one, payload, sizeof(payload));
        if (len < 0) continue;
        if (i2c_hdx_master_send(payload, (uint16_t)len) != I2C_HDX_OK) {
            printf("  send failed\n");
            continue;
        }
        /* surface an immediate response, if any */
        for (int i = 0; i < 20; i++) {
            usleep(POLL_GAP_US);
            uint8_t rx[MAX_PAYLOAD];
            uint16_t rlen = 0;
            i2c_hdx_err_t err = i2c_hdx_master_poll(rx, &rlen);
            if (err == I2C_HDX_OK) {
                printf("  <- response:");
                for (uint16_t j = 0; j < rlen; j++) printf(" %02X", rx[j]);
                printf("\n");
                break;
            }
            if (err != I2C_HDX_ERR_NO_DATA) { printf("  <- err=%d (run 'p'/'probe')\n", err); break; }
        }
    }
    return 0;
}

/* --------------------------------------------------------------- main */

static void usage(const char *prog) {
    printf(
"i2c_hdx_tool — i2c-hdx link bring-up / diagnostic tool\n"
"\n"
"Usage: %s [-d DEV] [-a ADDR] <command> [payload-hex...]\n"
"\n"
"Options:\n"
"  -d DEV     i2c device (default %s)\n"
"  -a ADDR    7-bit slave address (default 0x%02X)\n"
"  -h         this help\n"
"\n"
"Commands:\n"
"  probe            one raw read, decoded (STATUS/LEN/SUM) — is the link there?\n"
"  send <hex...>    i2c_hdx_master_send() one request frame\n"
"  poll             i2c_hdx_master_poll() once\n"
"  xfer <hex...>    send a request, then poll for the response (round trip)\n"
"  monitor          keep polling, print every frame the slave reports\n"
"  repl             interactive: type hex to send, see responses\n"
"\n"
"Payload hex accepts: 'DEADBEEF', 'DE AD BE EF', '0xDE 0xAD', 'de:ad'\n"
"\n"
"Tip: `probe` reads the raw bytes the master API hides — use it first when a\n"
"link won't come up, then `xfer` once the slave answers.\n",
        prog, DEFAULT_DEVICE, DEFAULT_ADDR);
}

int main(int argc, char **argv) {
    const char *device = DEFAULT_DEVICE;
    uint8_t addr = DEFAULT_ADDR;

    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) { device = argv[++i]; }
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) { addr = (uint8_t)strtol(argv[++i], NULL, 0); }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-') { fprintf(stderr, "unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
        else break;
    }
    if (i >= argc) { usage(argv[0]); return 1; }

    const char *cmd = argv[i++];
    int rest_argc = argc - i;
    char **rest_argv = argv + i;

    printf("i2c-hdx tool — device %s, slave 0x%02X\n", device, addr);
    if (i2c_hdx_port_linux_init(device, addr) != 0) {
        fprintf(stderr, "failed to open %s @ 0x%02X\n", device, addr);
        return 1;
    }

    int rc;
    if      (strcmp(cmd, "probe") == 0)   rc = cmd_probe();
    else if (strcmp(cmd, "send") == 0)    rc = cmd_send(rest_argc, rest_argv);
    else if (strcmp(cmd, "poll") == 0)    rc = cmd_poll();
    else if (strcmp(cmd, "xfer") == 0)    rc = cmd_xfer(rest_argc, rest_argv);
    else if (strcmp(cmd, "monitor") == 0) rc = cmd_monitor();
    else if (strcmp(cmd, "repl") == 0)    rc = cmd_repl();
    else { fprintf(stderr, "unknown command: %s\n", cmd); usage(argv[0]); rc = 1; }

    i2c_hdx_port_linux_deinit();
    return rc;
}
