/*
 * test_kiss.c — Property-based and unit tests for KISS encoding/decoding
 *
 * Uses theft library if available (HAVE_THEFT), otherwise falls back to
 * hand-rolled random testing with rand()/srand().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

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
/* Property 1: KISS encode/decode round-trip                           */
/* Feature: kiss-usb-interface, Property 1: KISS encode/decode         */
/* Validates: Requirements 2.4, 9.1, 3.1                              */
/* ================================================================== */

static int test_kiss_roundtrip(void)
{
    /* Allocate on heap to avoid stack overflow for large payloads */
    static uint8_t payload[KISS_MAX_PAYLOAD];
    static uint8_t encoded[KISS_MAX_PAYLOAD * 2 + 3];
    static uint8_t decoded[KISS_MAX_PAYLOAD];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Random length: bias toward small sizes but include some large */
        size_t len;
        int r = rand() % 100;
        if (r < 70)
            len = (size_t)(rand() % 256);        /* 0–255 */
        else if (r < 95)
            len = (size_t)(rand() % 4096);        /* 0–4095 */
        else
            len = (size_t)(rand() % KISS_MAX_PAYLOAD); /* 0–65534 */

        /* Fill with random bytes */
        for (size_t i = 0; i < len; i++)
            payload[i] = (uint8_t)(rand() & 0xFF);

        /* Encode */
        int enc_len = kiss_encode(payload, len, encoded, sizeof(encoded));
        if (enc_len < 0) {
            printf("\n    FAIL at iter %d: encode returned %d for len %zu\n",
                   iter, enc_len, len);
            return 0;
        }

        /* Decode */
        kiss_decoder_t dec;
        kiss_decoder_init(&dec);

        size_t out_len = 0;
        int    got_frame = 0;

        for (int j = 0; j < enc_len; j++) {
            int rc = kiss_decoder_feed(&dec, encoded[j],
                                       decoded, sizeof(decoded), &out_len);
            if (rc == 1) {
                got_frame = 1;
                break;
            }
        }

        if (!got_frame) {
            printf("\n    FAIL at iter %d: no frame decoded (len=%zu)\n",
                   iter, len);
            return 0;
        }

        if (out_len != len) {
            printf("\n    FAIL at iter %d: length mismatch %zu vs %zu\n",
                   iter, out_len, len);
            return 0;
        }

        if (len > 0 && memcmp(payload, decoded, len) != 0) {
            printf("\n    FAIL at iter %d: data mismatch (len=%zu)\n",
                   iter, len);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 4: KISS frame structure invariant                          */
/* Feature: kiss-usb-interface, Property 4: KISS frame structure       */
/* Validates: Requirements 2.1, 2.2, 2.3                              */
/* ================================================================== */

static int test_kiss_frame_structure(void)
{
    static uint8_t payload[KISS_MAX_PAYLOAD];
    static uint8_t encoded[KISS_MAX_PAYLOAD * 2 + 3];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Random length */
        size_t len;
        int r = rand() % 100;
        if (r < 70)
            len = (size_t)(rand() % 256);
        else if (r < 95)
            len = (size_t)(rand() % 4096);
        else
            len = (size_t)(rand() % KISS_MAX_PAYLOAD);

        for (size_t i = 0; i < len; i++)
            payload[i] = (uint8_t)(rand() & 0xFF);

        int enc_len = kiss_encode(payload, len, encoded, sizeof(encoded));
        if (enc_len < 3) {
            printf("\n    FAIL at iter %d: encode returned %d\n", iter, enc_len);
            return 0;
        }

        /* Check: starts with FEND + 0x00 */
        if (encoded[0] != KISS_FEND) {
            printf("\n    FAIL at iter %d: first byte is 0x%02X, expected FEND\n",
                   iter, encoded[0]);
            return 0;
        }
        if (encoded[1] != 0x00) {
            printf("\n    FAIL at iter %d: second byte is 0x%02X, expected 0x00\n",
                   iter, encoded[1]);
            return 0;
        }

        /* Check: ends with FEND */
        if (encoded[enc_len - 1] != KISS_FEND) {
            printf("\n    FAIL at iter %d: last byte is 0x%02X, expected FEND\n",
                   iter, encoded[enc_len - 1]);
            return 0;
        }

        /* Check: no unescaped FEND between delimiters (bytes 2..enc_len-2) */
        for (int j = 2; j < enc_len - 1; j++) {
            if (encoded[j] == KISS_FEND) {
                printf("\n    FAIL at iter %d: unescaped FEND at position %d\n",
                       iter, j);
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests for KISS edge cases                                      */
/* ================================================================== */

/* Test: decoder discards non-data command frames (Req 3.4) */
static int test_decoder_discards_non_data_cmd(void)
{
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t decoded[256];
    size_t  out_len = 0;

    /* Build a frame with command byte 0x01 (TX-delay, not data) */
    uint8_t frame[] = { KISS_FEND, 0x01, 0xAA, 0xBB, KISS_FEND };

    int result = 0;
    for (size_t i = 0; i < sizeof(frame); i++) {
        int rc = kiss_decoder_feed(&dec, frame[i], decoded, sizeof(decoded), &out_len);
        if (rc == 1) {
            /* Should NOT produce a data frame */
            printf("\n    FAIL: non-data cmd frame was returned as data\n");
            return 0;
        }
        if (rc == -1) {
            result = -1; /* expected: discarded */
        }
    }

    /* We expect the frame to have been discarded (rc == -1 at some point) */
    if (result != -1) {
        printf("\n    FAIL: non-data cmd frame was not discarded\n");
        return 0;
    }
    return 1;
}

/* Test: decoder discards oversized frames (Req 3.5) */
static int test_decoder_discards_oversized(void)
{
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t decoded[256];
    size_t  out_len = 0;

    /* Feed FEND to start frame */
    kiss_decoder_feed(&dec, KISS_FEND, decoded, sizeof(decoded), &out_len);

    /* Feed command byte */
    kiss_decoder_feed(&dec, 0x00, decoded, sizeof(decoded), &out_len);

    /* Feed KISS_MAX_PAYLOAD + 1 data bytes (exceeds buffer: cmd + payload) */
    int overflow_detected = 0;
    for (size_t i = 0; i < KISS_MAX_PAYLOAD + 1; i++) {
        int rc = kiss_decoder_feed(&dec, 0x42, decoded, sizeof(decoded), &out_len);
        if (rc == -1) {
            overflow_detected = 1;
            break;
        }
    }

    if (!overflow_detected) {
        printf("\n    FAIL: oversized frame was not discarded\n");
        return 0;
    }
    return 1;
}

/* Test: TNC parameter command frames have correct bytes (Req 1.2) */
static int test_tnc_parameter_frames(void)
{
    uint8_t buf[4];

    /* TX-delay: cmd 0x01, value 50 (500ms / 10) */
    int len = kiss_build_cmd(0x01, 50, buf, sizeof(buf));
    if (len != 4) {
        printf("\n    FAIL: kiss_build_cmd returned %d, expected 4\n", len);
        return 0;
    }
    if (buf[0] != KISS_FEND || buf[1] != 0x01 || buf[2] != 50 || buf[3] != KISS_FEND) {
        printf("\n    FAIL: TX-delay frame bytes incorrect: "
               "%02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3]);
        return 0;
    }

    /* TX-tail: cmd 0x04, value 30 (300ms / 10) */
    len = kiss_build_cmd(0x04, 30, buf, sizeof(buf));
    if (len != 4) {
        printf("\n    FAIL: kiss_build_cmd returned %d, expected 4\n", len);
        return 0;
    }
    if (buf[0] != KISS_FEND || buf[1] != 0x04 || buf[2] != 30 || buf[3] != KISS_FEND) {
        printf("\n    FAIL: TX-tail frame bytes incorrect: "
               "%02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3]);
        return 0;
    }

    /* Error case: buffer too small */
    len = kiss_build_cmd(0x01, 50, buf, 3);
    if (len != -1) {
        printf("\n    FAIL: expected -1 for small buffer, got %d\n", len);
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

    printf("KISS module tests\n");
    printf("=================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(kiss_roundtrip);
    TEST(kiss_frame_structure);

    printf("\nUnit tests:\n");
    TEST(decoder_discards_non_data_cmd);
    TEST(decoder_discards_oversized);
    TEST(tnc_parameter_frames);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
