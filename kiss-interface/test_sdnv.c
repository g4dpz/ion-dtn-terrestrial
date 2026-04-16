/*
 * test_sdnv.c — Property-based and unit tests for SDNV module
 *
 * Uses theft library if available (HAVE_THEFT), otherwise falls back to
 * hand-rolled random testing with rand()/srand().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "sdnv.h"

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

/* Helper: generate a random uint64_t in [0, 2^63-1] */
static uint64_t rand_uint64(void)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u = (u << 8) | (uint8_t)(rand() & 0xFF);
    /* Ensure value is in [0, 2^63-1] */
    return u >> 1;
}

/* ================================================================== */
/* Property 1: SDNV encode/decode round-trip                           */
/* Feature: ltp-over-kiss, Property 1: SDNV encode/decode round-trip   */
/* Validates: Requirements 1.3, 13.1, 13.2, 13.3                      */
/* ================================================================== */

static int test_sdnv_roundtrip(void)
{
    uint8_t buf[SDNV_MAX_BYTES];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint64_t value_in = rand_uint64();

        int enc_len = sdnv_encode(value_in, buf, sizeof(buf));
        if (enc_len < 1) {
            printf("\n    FAIL at iter %d: encode returned %d for value %lu\n",
                   iter, enc_len, (unsigned long)value_in);
            return 0;
        }

        uint64_t value_out = 0;
        int dec_len = sdnv_decode(buf, (size_t)enc_len, &value_out);
        if (dec_len < 1) {
            printf("\n    FAIL at iter %d: decode returned %d for value %lu\n",
                   iter, dec_len, (unsigned long)value_in);
            return 0;
        }

        if (value_out != value_in) {
            printf("\n    FAIL at iter %d: round-trip mismatch: "
                   "in=%lu out=%lu\n",
                   iter, (unsigned long)value_in, (unsigned long)value_out);
            return 0;
        }

        if (dec_len != enc_len) {
            printf("\n    FAIL at iter %d: bytes consumed (%d) != "
                   "bytes written (%d)\n",
                   iter, dec_len, enc_len);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 2: SDNV encoding structural invariant                      */
/* Feature: ltp-over-kiss, Property 2: SDNV encoding structural inv.   */
/* Validates: Requirements 13.1, 13.4                                  */
/* ================================================================== */

static int test_sdnv_structure(void)
{
    uint8_t buf[SDNV_MAX_BYTES];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint64_t value = rand_uint64();

        int enc_len = sdnv_encode(value, buf, sizeof(buf));
        if (enc_len < 1) {
            printf("\n    FAIL at iter %d: encode returned %d\n",
                   iter, enc_len);
            return 0;
        }

        /* Encoding must be at most 10 bytes */
        if (enc_len > SDNV_MAX_BYTES) {
            printf("\n    FAIL at iter %d: encoding is %d bytes "
                   "(max %d)\n", iter, enc_len, SDNV_MAX_BYTES);
            return 0;
        }

        /* All bytes except last must have MSB=1 (continuation bit) */
        for (int j = 0; j < enc_len - 1; j++) {
            if ((buf[j] & 0x80) == 0) {
                printf("\n    FAIL at iter %d: byte %d has MSB=0 "
                       "(expected 1)\n", iter, j);
                return 0;
            }
        }

        /* Last byte must have MSB=0 */
        if ((buf[enc_len - 1] & 0x80) != 0) {
            printf("\n    FAIL at iter %d: last byte has MSB=1 "
                   "(expected 0)\n", iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests for SDNV known values and error cases                    */
/* Requirements: 13.1, 13.5                                            */
/* ================================================================== */

/* Test: encode 0 → [0x00] (1 byte) */
static int test_encode_zero(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    int len = sdnv_encode(0, buf, sizeof(buf));
    if (len != 1) {
        printf("\n    FAIL: encode(0) returned %d bytes, expected 1\n", len);
        return 0;
    }
    if (buf[0] != 0x00) {
        printf("\n    FAIL: encode(0) = [0x%02X], expected [0x00]\n", buf[0]);
        return 0;
    }
    return 1;
}

/* Test: encode 127 → [0x7F] (1 byte) */
static int test_encode_127(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    int len = sdnv_encode(127, buf, sizeof(buf));
    if (len != 1) {
        printf("\n    FAIL: encode(127) returned %d bytes, expected 1\n", len);
        return 0;
    }
    if (buf[0] != 0x7F) {
        printf("\n    FAIL: encode(127) = [0x%02X], expected [0x7F]\n", buf[0]);
        return 0;
    }
    return 1;
}

/* Test: encode 128 → [0x81, 0x00] (2 bytes) */
static int test_encode_128(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    int len = sdnv_encode(128, buf, sizeof(buf));
    if (len != 2) {
        printf("\n    FAIL: encode(128) returned %d bytes, expected 2\n", len);
        return 0;
    }
    if (buf[0] != 0x81 || buf[1] != 0x00) {
        printf("\n    FAIL: encode(128) = [0x%02X, 0x%02X], "
               "expected [0x81, 0x00]\n", buf[0], buf[1]);
        return 0;
    }
    return 1;
}

/* Test: encode 16383 → [0xFF, 0x7F] (2 bytes) */
static int test_encode_16383(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    int len = sdnv_encode(16383, buf, sizeof(buf));
    if (len != 2) {
        printf("\n    FAIL: encode(16383) returned %d bytes, expected 2\n", len);
        return 0;
    }
    if (buf[0] != 0xFF || buf[1] != 0x7F) {
        printf("\n    FAIL: encode(16383) = [0x%02X, 0x%02X], "
               "expected [0xFF, 0x7F]\n", buf[0], buf[1]);
        return 0;
    }
    return 1;
}

/* Test: encode 16384 → [0x81, 0x80, 0x00] (3 bytes) */
static int test_encode_16384(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    int len = sdnv_encode(16384, buf, sizeof(buf));
    if (len != 3) {
        printf("\n    FAIL: encode(16384) returned %d bytes, expected 3\n", len);
        return 0;
    }
    if (buf[0] != 0x81 || buf[1] != 0x80 || buf[2] != 0x00) {
        printf("\n    FAIL: encode(16384) = [0x%02X, 0x%02X, 0x%02X], "
               "expected [0x81, 0x80, 0x00]\n", buf[0], buf[1], buf[2]);
        return 0;
    }
    return 1;
}

/* Test: encode 2^63-1 — max supported value round-trips correctly
 * 2^63-1 has 63 significant bits → 63/7 = 9 SDNV bytes exactly. */
static int test_encode_max(void)
{
    uint8_t buf[SDNV_MAX_BYTES];
    uint64_t max_val = (uint64_t)INT64_MAX;  /* 2^63 - 1 */
    int len = sdnv_encode(max_val, buf, sizeof(buf));
    if (len < 1 || len > SDNV_MAX_BYTES) {
        printf("\n    FAIL: encode(2^63-1) returned %d bytes\n", len);
        return 0;
    }
    /* 63 data bits / 7 bits per byte = 9 bytes */
    if (len != 9) {
        printf("\n    FAIL: encode(2^63-1) returned %d bytes, expected 9\n", len);
        return 0;
    }
    /* Verify round-trip for max value */
    uint64_t decoded = 0;
    int dec_len = sdnv_decode(buf, (size_t)len, &decoded);
    if (dec_len != len) {
        printf("\n    FAIL: decode(2^63-1) consumed %d bytes, expected %d\n",
               dec_len, len);
        return 0;
    }
    if (decoded != max_val) {
        printf("\n    FAIL: round-trip of 2^63-1 failed: got %lu\n",
               (unsigned long)decoded);
        return 0;
    }
    return 1;
}

/* Test: decode truncated buffer (all bytes have MSB=1) returns -1 */
static int test_decode_truncated(void)
{
    /* All bytes have continuation bit set — no terminator */
    uint8_t buf[] = { 0x81, 0x82, 0x83 };
    uint64_t value = 0;
    int rc = sdnv_decode(buf, sizeof(buf), &value);
    if (rc != -1) {
        printf("\n    FAIL: decode of truncated buffer returned %d, "
               "expected -1\n", rc);
        return 0;
    }
    return 1;
}

/* Test: encode with out_size=0 returns -1 */
static int test_encode_zero_buffer(void)
{
    uint8_t buf[1];
    int rc = sdnv_encode(0, buf, 0);
    if (rc != -1) {
        printf("\n    FAIL: encode with out_size=0 returned %d, "
               "expected -1\n", rc);
        return 0;
    }
    return 1;
}

/* Test: decode empty buffer returns -1 */
static int test_decode_empty(void)
{
    uint64_t value = 0;
    int rc = sdnv_decode(NULL, 0, &value);
    if (rc != -1) {
        printf("\n    FAIL: decode of empty buffer returned %d, "
               "expected -1\n", rc);
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

    printf("SDNV module tests\n");
    printf("=================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(sdnv_roundtrip);
    TEST(sdnv_structure);

    printf("\nUnit tests:\n");
    TEST(encode_zero);
    TEST(encode_127);
    TEST(encode_128);
    TEST(encode_16383);
    TEST(encode_16384);
    TEST(encode_max);
    TEST(decode_truncated);
    TEST(encode_zero_buffer);
    TEST(decode_empty);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
