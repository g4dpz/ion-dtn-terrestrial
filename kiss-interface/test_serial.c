/*
 * test_serial.c — Property-based and unit tests for serial module
 *
 * Tests serial_parse_device (pure logic) and serial_configure_tnc
 * (via pipe to capture KISS command bytes). serial_open/serial_close
 * require real hardware and are tested manually.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "serial.h"
#include "kiss.h"

/* ================================================================== */
/* Test infrastructure                                                 */
/* ================================================================== */

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

#define ITERATIONS 1000

/* ================================================================== */
/* Property 7: Device string parse round-trip                          */
/* Feature: kiss-usb-interface, Property 7: Device string parse        */
/* Validates: Requirements 1.5, 1.4                                    */
/* ================================================================== */

static const int valid_bauds[] = { 1200, 9600, 19200, 38400, 57600, 115200 };
#define NUM_VALID_BAUDS (sizeof(valid_bauds) / sizeof(valid_bauds[0]))

/* Generate a random device path with no colon characters */
static void random_device_path(char *buf, size_t buf_size)
{
    /* Use /dev/ttyXXX style paths with random suffix */
    const char *prefixes[] = {
        "/dev/ttyUSB", "/dev/ttyACM", "/dev/ttyS", "/dev/serial/by-id/usb"
    };
    int prefix_idx = rand() % 4;
    int suffix = rand() % 100;
    snprintf(buf, buf_size, "%s%d", prefixes[prefix_idx], suffix);
}

static int test_device_parse_roundtrip(void)
{
    char device_in[256];
    char arg_buf[512];
    char device_out[256];
    int  baud_out;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        random_device_path(device_in, sizeof(device_in));
        int baud_in = valid_bauds[rand() % NUM_VALID_BAUDS];

        /* Test "device:baud" format */
        snprintf(arg_buf, sizeof(arg_buf), "%s:%d", device_in, baud_in);

        int rc = serial_parse_device(arg_buf, device_out, sizeof(device_out), &baud_out);
        if (rc != 0) {
            printf("\n    FAIL at iter %d: parse returned %d for '%s'\n",
                   iter, rc, arg_buf);
            return 0;
        }

        if (strcmp(device_in, device_out) != 0) {
            printf("\n    FAIL at iter %d: device mismatch '%s' vs '%s'\n",
                   iter, device_in, device_out);
            return 0;
        }

        if (baud_in != baud_out) {
            printf("\n    FAIL at iter %d: baud mismatch %d vs %d\n",
                   iter, baud_in, baud_out);
            return 0;
        }

        /* Test device-only format (no colon) — should default to 9600 */
        rc = serial_parse_device(device_in, device_out, sizeof(device_out), &baud_out);
        if (rc != 0) {
            printf("\n    FAIL at iter %d: parse device-only returned %d\n",
                   iter, rc);
            return 0;
        }

        if (strcmp(device_in, device_out) != 0) {
            printf("\n    FAIL at iter %d: device-only mismatch '%s' vs '%s'\n",
                   iter, device_in, device_out);
            return 0;
        }

        if (baud_out != 9600) {
            printf("\n    FAIL at iter %d: device-only baud %d, expected 9600\n",
                   iter, baud_out);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests for serial module                                        */
/* ================================================================== */

/* Test: parse "device:baud" extracts both parts correctly */
static int test_parse_device_with_baud(void)
{
    char device[256];
    int  baud;

    int rc = serial_parse_device("/dev/ttyUSB0:19200", device, sizeof(device), &baud);
    if (rc != 0) {
        printf("\n    FAIL: parse returned %d\n", rc);
        return 0;
    }
    if (strcmp(device, "/dev/ttyUSB0") != 0) {
        printf("\n    FAIL: device = '%s', expected '/dev/ttyUSB0'\n", device);
        return 0;
    }
    if (baud != 19200) {
        printf("\n    FAIL: baud = %d, expected 19200\n", baud);
        return 0;
    }

    /* Test another baud rate */
    rc = serial_parse_device("/dev/ttyACM0:115200", device, sizeof(device), &baud);
    if (rc != 0) {
        printf("\n    FAIL: parse returned %d\n", rc);
        return 0;
    }
    if (strcmp(device, "/dev/ttyACM0") != 0) {
        printf("\n    FAIL: device = '%s', expected '/dev/ttyACM0'\n", device);
        return 0;
    }
    if (baud != 115200) {
        printf("\n    FAIL: baud = %d, expected 115200\n", baud);
        return 0;
    }

    return 1;
}

/* Test: parse "device" alone defaults to 9600 */
static int test_parse_device_default_baud(void)
{
    char device[256];
    int  baud;

    int rc = serial_parse_device("/dev/ttyUSB0", device, sizeof(device), &baud);
    if (rc != 0) {
        printf("\n    FAIL: parse returned %d\n", rc);
        return 0;
    }
    if (strcmp(device, "/dev/ttyUSB0") != 0) {
        printf("\n    FAIL: device = '%s', expected '/dev/ttyUSB0'\n", device);
        return 0;
    }
    if (baud != 9600) {
        printf("\n    FAIL: baud = %d, expected 9600\n", baud);
        return 0;
    }

    return 1;
}

/* Test: invalid baud defaults to 9600 with warning */
static int test_parse_device_invalid_baud(void)
{
    char device[256];
    int  baud;

    /* 4800 is not in the supported set */
    int rc = serial_parse_device("/dev/ttyUSB0:4800", device, sizeof(device), &baud);
    if (rc != 0) {
        printf("\n    FAIL: parse returned %d\n", rc);
        return 0;
    }
    if (strcmp(device, "/dev/ttyUSB0") != 0) {
        printf("\n    FAIL: device = '%s', expected '/dev/ttyUSB0'\n", device);
        return 0;
    }
    if (baud != 9600) {
        printf("\n    FAIL: baud = %d, expected 9600 (invalid baud fallback)\n", baud);
        return 0;
    }

    /* 2400 is also not supported */
    rc = serial_parse_device("/dev/ttyS0:2400", device, sizeof(device), &baud);
    if (rc != 0) {
        printf("\n    FAIL: parse returned %d for 2400\n", rc);
        return 0;
    }
    if (baud != 9600) {
        printf("\n    FAIL: baud = %d for 2400, expected 9600\n", baud);
        return 0;
    }

    return 1;
}

/* Test: serial_configure_tnc writes correct KISS command frames to a pipe */
static int test_configure_tnc_frames(void)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        printf("\n    FAIL: pipe() failed\n");
        return 0;
    }

    /* Configure TNC with 500ms txdelay, 300ms txtail */
    int rc = serial_configure_tnc(pipefd[1], 500, 300);
    if (rc != 0) {
        printf("\n    FAIL: serial_configure_tnc returned %d\n", rc);
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }

    /* Read back the 8 bytes (two 4-byte KISS command frames) */
    uint8_t buf[8];
    ssize_t n = read(pipefd[0], buf, sizeof(buf));
    close(pipefd[0]);
    close(pipefd[1]);

    if (n != 8) {
        printf("\n    FAIL: read %zd bytes, expected 8\n", n);
        return 0;
    }

    /* TX-delay frame: FEND, 0x01, 50 (500/10), FEND */
    if (buf[0] != KISS_FEND || buf[1] != 0x01 || buf[2] != 50 || buf[3] != KISS_FEND) {
        printf("\n    FAIL: txdelay frame: %02X %02X %02X %02X\n",
               buf[0], buf[1], buf[2], buf[3]);
        return 0;
    }

    /* TX-tail frame: FEND, 0x04, 30 (300/10), FEND */
    if (buf[4] != KISS_FEND || buf[5] != 0x04 || buf[6] != 30 || buf[7] != KISS_FEND) {
        printf("\n    FAIL: txtail frame: %02X %02X %02X %02X\n",
               buf[4], buf[5], buf[6], buf[7]);
        return 0;
    }

    return 1;
}

/* Test: NULL/invalid arguments to serial_parse_device */
static int test_parse_device_null_args(void)
{
    char device[256];
    int  baud;

    if (serial_parse_device(NULL, device, sizeof(device), &baud) != -1) {
        printf("\n    FAIL: NULL arg should return -1\n");
        return 0;
    }
    if (serial_parse_device("/dev/ttyUSB0", NULL, 256, &baud) != -1) {
        printf("\n    FAIL: NULL device should return -1\n");
        return 0;
    }
    if (serial_parse_device("/dev/ttyUSB0", device, 0, &baud) != -1) {
        printf("\n    FAIL: zero dev_size should return -1\n");
        return 0;
    }
    if (serial_parse_device("/dev/ttyUSB0", device, sizeof(device), NULL) != -1) {
        printf("\n    FAIL: NULL baud should return -1\n");
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(void)
{
    srand((unsigned)time(NULL));

    printf("Serial module tests\n");
    printf("====================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(device_parse_roundtrip);

    printf("\nUnit tests:\n");
    TEST(parse_device_with_baud);
    TEST(parse_device_default_baud);
    TEST(parse_device_invalid_baud);
    TEST(configure_tnc_frames);
    TEST(parse_device_null_args);

    printf("\n--------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
