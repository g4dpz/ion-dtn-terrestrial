/*
 * test_ax25.c — Property-based and unit tests for AX.25 framing
 *
 * Uses hand-rolled random testing with rand()/srand() for property tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "ax25.h"

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

/* Buffer size for callsign strings in tests — slightly larger than
 * AX25_MAX_CALLSIGN to silence truncation warnings from snprintf.
 * Actual callsigns generated are at most "ABCDEF-15" (9 chars + NUL). */
#define TEST_CALL_BUF 16
static const char CALLSIGN_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define NUM_CALLSIGN_CHARS (sizeof(CALLSIGN_CHARS) - 1)

/* Generate a random callsign of 1-6 uppercase alphanumeric chars with SSID */
static void random_callsign(char *buf, size_t buf_size, int *ssid)
{
    int len = 1 + (rand() % 6);  /* 1–6 chars */
    for (int i = 0; i < len; i++)
        buf[i] = CALLSIGN_CHARS[rand() % NUM_CALLSIGN_CHARS];
    buf[len] = '\0';

    *ssid = rand() % 16;  /* 0–15 */

    /* Append -SSID using a temp buffer to avoid overlap */
    char tmp[TEST_CALL_BUF];
    snprintf(tmp, sizeof(tmp), "%s-%d", buf, *ssid);
    snprintf(buf, buf_size, "%s", tmp);
}

/* Generate a random callsign and return the base call and ssid separately */
static void random_callsign_parts(char *call_out, int *ssid_out,
                                  char *full_out, size_t full_size)
{
    int len = 1 + (rand() % 6);
    for (int i = 0; i < len; i++)
        call_out[i] = CALLSIGN_CHARS[rand() % NUM_CALLSIGN_CHARS];
    call_out[len] = '\0';

    *ssid_out = rand() % 16;

    (void)snprintf(full_out, full_size, "%s-%d", call_out, *ssid_out);
}

/* ================================================================== */
/* Property 2: AX.25 build/strip round-trip                           */
/* Feature: kiss-usb-interface, Property 2: AX.25 build/strip         */
/* Validates: Requirements 9.2, 4.1, 4.5                              */
/* ================================================================== */

static int test_ax25_roundtrip(void)
{
    static uint8_t frame[AX25_HDR_LEN + 256];
    static uint8_t payload[256];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Random callsigns */
        char src[TEST_CALL_BUF], dst[TEST_CALL_BUF];
        int  src_ssid, dst_ssid;
        random_callsign(src, sizeof(src), &src_ssid);
        random_callsign(dst, sizeof(dst), &dst_ssid);

        /* Random payload (0–255 bytes) */
        size_t plen = (size_t)(rand() % 256);
        for (size_t i = 0; i < plen; i++)
            payload[i] = (uint8_t)(rand() & 0xFF);

        /* Build frame */
        int frame_len = ax25_build_frame(dst, src, payload, plen,
                                         frame, sizeof(frame));
        if (frame_len < 0) {
            printf("\n    FAIL at iter %d: build_frame returned %d "
                   "(src=%s dst=%s plen=%zu)\n", iter, frame_len, src, dst, plen);
            return 0;
        }

        /* Strip frame */
        char got_src[TEST_CALL_BUF], got_dst[TEST_CALL_BUF];
        const uint8_t *got_info = NULL;
        int info_len = ax25_strip_frame(frame, (size_t)frame_len,
                                        got_src, got_dst, &got_info);
        if (info_len < 0) {
            printf("\n    FAIL at iter %d: strip_frame returned %d\n",
                   iter, info_len);
            return 0;
        }

        /* Check callsigns match */
        if (strcmp(src, got_src) != 0) {
            printf("\n    FAIL at iter %d: src mismatch '%s' vs '%s'\n",
                   iter, src, got_src);
            return 0;
        }
        if (strcmp(dst, got_dst) != 0) {
            printf("\n    FAIL at iter %d: dst mismatch '%s' vs '%s'\n",
                   iter, dst, got_dst);
            return 0;
        }

        /* Check payload matches */
        if ((size_t)info_len != plen) {
            printf("\n    FAIL at iter %d: info_len %d vs plen %zu\n",
                   iter, info_len, plen);
            return 0;
        }
        if (plen > 0 && memcmp(payload, got_info, plen) != 0) {
            printf("\n    FAIL at iter %d: payload data mismatch\n", iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 3: Callsign encode/decode round-trip                       */
/* Feature: kiss-usb-interface, Property 3: Callsign round-trip        */
/* Validates: Requirements 9.3, 4.3                                    */
/* ================================================================== */

static int test_callsign_roundtrip(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        char base_call[7];
        int  ssid;
        char full_call[TEST_CALL_BUF];

        random_callsign_parts(base_call, &ssid, full_call, sizeof(full_call));

        /* Encode */
        uint8_t addr[AX25_ADDR_LEN];
        int last = rand() % 2;
        if (ax25_encode_addr(full_call, addr, last) != 0) {
            printf("\n    FAIL at iter %d: encode_addr failed for '%s'\n",
                   iter, full_call);
            return 0;
        }

        /* Decode */
        char decoded[TEST_CALL_BUF];
        if (ax25_decode_addr(addr, decoded, sizeof(decoded)) != 0) {
            printf("\n    FAIL at iter %d: decode_addr failed\n", iter);
            return 0;
        }

        /* Compare: decoded should match full_call */
        if (strcmp(full_call, decoded) != 0) {
            printf("\n    FAIL at iter %d: '%s' != '%s'\n",
                   iter, full_call, decoded);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 5: AX.25 frame structural invariants                       */
/* Feature: kiss-usb-interface, Property 5: AX.25 frame structure      */
/* Validates: Requirements 4.2, 4.4                                    */
/* ================================================================== */

static int test_ax25_frame_structure(void)
{
    static uint8_t frame[AX25_HDR_LEN + 256];
    static uint8_t payload[256];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        char src[TEST_CALL_BUF], dst[TEST_CALL_BUF];
        int  src_ssid, dst_ssid;
        random_callsign(src, sizeof(src), &src_ssid);
        random_callsign(dst, sizeof(dst), &dst_ssid);

        size_t plen = (size_t)(rand() % 256);
        for (size_t i = 0; i < plen; i++)
            payload[i] = (uint8_t)(rand() & 0xFF);

        int frame_len = ax25_build_frame(dst, src, payload, plen,
                                         frame, sizeof(frame));
        if (frame_len < 0) {
            printf("\n    FAIL at iter %d: build_frame returned %d\n",
                   iter, frame_len);
            return 0;
        }

        /* Byte 14 = 0x03 (UI control) */
        if (frame[14] != 0x03) {
            printf("\n    FAIL at iter %d: byte 14 = 0x%02X, expected 0x03\n",
                   iter, frame[14]);
            return 0;
        }

        /* Byte 15 = 0xF0 (no layer 3 PID) */
        if (frame[15] != 0xF0) {
            printf("\n    FAIL at iter %d: byte 15 = 0x%02X, expected 0xF0\n",
                   iter, frame[15]);
            return 0;
        }

        /* Destination extension bit (bit 0 of byte 6) = 0 */
        if ((frame[6] & 0x01) != 0) {
            printf("\n    FAIL at iter %d: dst extension bit is 1, expected 0\n",
                   iter);
            return 0;
        }

        /* Source extension bit (bit 0 of byte 13) = 1 */
        if ((frame[13] & 0x01) != 1) {
            printf("\n    FAIL at iter %d: src extension bit is 0, expected 1\n",
                   iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 6: Echo mode preserves payload and swaps callsigns         */
/* Feature: kiss-usb-interface, Property 6: Echo callsign swap         */
/* Validates: Requirements 7.1                                         */
/* ================================================================== */

static int test_echo_swap(void)
{
    static uint8_t frame[AX25_HDR_LEN + 256];
    static uint8_t payload[256];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Generate random src (S) and dst (D) callsigns */
        char src_S[TEST_CALL_BUF], dst_D[TEST_CALL_BUF];
        int  src_ssid, dst_ssid;
        random_callsign(src_S, sizeof(src_S), &src_ssid);
        random_callsign(dst_D, sizeof(dst_D), &dst_ssid);

        /* Generate random payload P (0–255 bytes) */
        size_t plen = (size_t)(rand() % 256);
        for (size_t i = 0; i < plen; i++)
            payload[i] = (uint8_t)(rand() & 0xFF);

        /* Step 1: Build original AX.25 frame with dst=D, src=S */
        int frame_len = ax25_build_frame(dst_D, src_S, payload, plen,
                                         frame, sizeof(frame));
        if (frame_len < 0) {
            printf("\n    FAIL at iter %d: build_frame returned %d "
                   "(src=%s dst=%s plen=%zu)\n", iter, frame_len, src_S, dst_D, plen);
            return 0;
        }

        /* Step 2: Strip to get src_out, dst_out, info */
        char strip_src[TEST_CALL_BUF], strip_dst[TEST_CALL_BUF];
        const uint8_t *strip_info = NULL;
        int info_len = ax25_strip_frame(frame, (size_t)frame_len,
                                        strip_src, strip_dst, &strip_info);
        if (info_len < 0) {
            printf("\n    FAIL at iter %d: strip_frame returned %d\n",
                   iter, info_len);
            return 0;
        }

        /* Step 3: Swap — rebuild with dst=src_out, src=dst_out (echo swap) */
        uint8_t echo_frame[AX25_HDR_LEN + 256];
        int echo_len = ax25_build_frame(strip_src, strip_dst,
                                        strip_info, (size_t)info_len,
                                        echo_frame, sizeof(echo_frame));
        if (echo_len < 0) {
            printf("\n    FAIL at iter %d: echo build_frame returned %d\n",
                   iter, echo_len);
            return 0;
        }

        /* Step 4: Strip the echo frame */
        char echo_src[TEST_CALL_BUF], echo_dst[TEST_CALL_BUF];
        const uint8_t *echo_info = NULL;
        int echo_info_len = ax25_strip_frame(echo_frame, (size_t)echo_len,
                                             echo_src, echo_dst, &echo_info);
        if (echo_info_len < 0) {
            printf("\n    FAIL at iter %d: echo strip_frame returned %d\n",
                   iter, echo_info_len);
            return 0;
        }

        /* Step 5: Verify new src == D (original dst) */
        if (strcmp(echo_src, dst_D) != 0) {
            printf("\n    FAIL at iter %d: echo src '%s' != original dst '%s'\n",
                   iter, echo_src, dst_D);
            return 0;
        }

        /* Verify new dst == S (original src) */
        if (strcmp(echo_dst, src_S) != 0) {
            printf("\n    FAIL at iter %d: echo dst '%s' != original src '%s'\n",
                   iter, echo_dst, src_S);
            return 0;
        }

        /* Verify payload unchanged */
        if ((size_t)echo_info_len != plen) {
            printf("\n    FAIL at iter %d: echo info_len %d != plen %zu\n",
                   iter, echo_info_len, plen);
            return 0;
        }
        if (plen > 0 && memcmp(payload, echo_info, plen) != 0) {
            printf("\n    FAIL at iter %d: echo payload data mismatch\n", iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit test: AX.25 rejects frames < 16 bytes (Req 4.6)               */
/* ================================================================== */

static int test_ax25_rejects_short_frames(void)
{
    char src[TEST_CALL_BUF], dst[TEST_CALL_BUF];
    const uint8_t *info = NULL;

    /* Test frames of length 0 through 15 — all should be rejected */
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));

    for (size_t len = 0; len < 16; len++) {
        int rc = ax25_strip_frame(buf, len, src, dst, &info);
        if (rc != -1) {
            printf("\n    FAIL: frame of %zu bytes was accepted (rc=%d)\n",
                   len, rc);
            return 0;
        }
    }

    /* A 16-byte frame with correct control/PID should be accepted (0-byte info) */
    uint8_t valid_frame[AX25_HDR_LEN];
    memset(valid_frame, 0x40, sizeof(valid_frame)); /* space-padded addresses */
    valid_frame[6]  = 0x60;  /* dst SSID byte: reserved bits, ext=0 */
    valid_frame[13] = 0x61;  /* src SSID byte: reserved bits, ext=1 */
    valid_frame[14] = AX25_CTRL_UI;
    valid_frame[15] = AX25_PID_NOLAYER3;

    int rc = ax25_strip_frame(valid_frame, AX25_HDR_LEN, src, dst, &info);
    if (rc < 0) {
        printf("\n    FAIL: valid 16-byte frame was rejected (rc=%d)\n", rc);
        return 0;
    }
    if (rc != 0) {
        printf("\n    FAIL: expected info_len=0 for 16-byte frame, got %d\n", rc);
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

    printf("AX.25 module tests\n");
    printf("==================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(ax25_roundtrip);
    TEST(callsign_roundtrip);
    TEST(ax25_frame_structure);
    TEST(echo_swap);

    printf("\nUnit tests:\n");
    TEST(ax25_rejects_short_frames);

    printf("\n------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
