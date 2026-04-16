/*
 * test_ping.c — Property-based and unit tests for ping payload module
 *
 * Uses theft library if available (HAVE_THEFT), otherwise falls back to
 * hand-rolled random testing with rand()/srand().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include "ping.h"

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

/* Helper: generate a random non-negative int64_t timestamp */
static int64_t rand_timestamp(void)
{
    /* Build a non-negative int64_t from random bytes */
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u = (u << 8) | (uint8_t)(rand() & 0xFF);
    /* Ensure non-negative */
    return (int64_t)(u >> 1);
}

/* ================================================================== */
/* Property 1: Ping payload round-trip                                 */
/* Feature: kiss-ping, Property 1: Ping payload round-trip             */
/* Validates: Requirements 8.1                                         */
/* ================================================================== */

static int test_ping_roundtrip(void)
{
    uint8_t buf[PING_PAYLOAD_LEN];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint16_t seq_in = (uint16_t)(rand() & 0xFFFF);
        int64_t  ts_in  = rand_timestamp();

        if (ping_build_payload(seq_in, ts_in, buf, sizeof(buf)) != 0) {
            printf("\n    FAIL at iter %d: build returned -1\n", iter);
            return 0;
        }

        uint16_t seq_out = 0;
        int64_t  ts_out  = 0;
        if (ping_parse_payload(buf, sizeof(buf), &seq_out, &ts_out) != 0) {
            printf("\n    FAIL at iter %d: parse returned -1\n", iter);
            return 0;
        }

        if (seq_out != seq_in) {
            printf("\n    FAIL at iter %d: seq %u != %u\n",
                   iter, seq_out, seq_in);
            return 0;
        }
        if (ts_out != ts_in) {
            printf("\n    FAIL at iter %d: ts %ld != %ld\n",
                   iter, (long)ts_out, (long)ts_in);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 2: Ping payload structural invariant                       */
/* Feature: kiss-ping, Property 2: Ping payload structural invariant   */
/* Validates: Requirements 2.1                                         */
/* ================================================================== */

static int test_ping_structure(void)
{
    uint8_t buf[PING_PAYLOAD_LEN];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint16_t seq = (uint16_t)(rand() & 0xFFFF);
        int64_t  ts  = rand_timestamp();

        if (ping_build_payload(seq, ts, buf, sizeof(buf)) != 0) {
            printf("\n    FAIL at iter %d: build returned -1\n", iter);
            return 0;
        }

        /* Bytes 0-3: ASCII "PING" */
        if (memcmp(buf, "PING", 4) != 0) {
            printf("\n    FAIL at iter %d: magic mismatch\n", iter);
            return 0;
        }

        /* Bytes 4-5: htons(seq) */
        uint16_t expected_net_seq = htons(seq);
        uint16_t actual_net_seq;
        memcpy(&actual_net_seq, buf + 4, 2);
        if (actual_net_seq != expected_net_seq) {
            printf("\n    FAIL at iter %d: seq bytes mismatch\n", iter);
            return 0;
        }

        /* Bytes 6-13: network-order timestamp */
        /* Reconstruct expected big-endian bytes manually */
        uint8_t expected_ts[8];
        uint64_t u = (uint64_t)ts;
        for (int i = 7; i >= 0; i--) {
            expected_ts[i] = (uint8_t)(u & 0xFF);
            u >>= 8;
        }
        if (memcmp(buf + 6, expected_ts, 8) != 0) {
            printf("\n    FAIL at iter %d: timestamp bytes mismatch\n", iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 3: Non-ping payload rejection                              */
/* Feature: kiss-ping, Property 3: Non-ping payload rejection          */
/* Validates: Requirements 8.3                                         */
/* ================================================================== */

static int test_ping_reject_non_ping(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Generate a random buffer of length 14..64 */
        size_t len = 14 + (size_t)(rand() % 51);
        uint8_t buf[64];
        for (size_t i = 0; i < len; i++)
            buf[i] = (uint8_t)(rand() & 0xFF);

        /* Ensure first 4 bytes are NOT "PING" */
        if (memcmp(buf, "PING", 4) == 0) {
            /* Corrupt the first byte */
            buf[0] ^= 0xFF;
        }

        uint16_t seq = 0;
        int64_t  ts  = 0;
        if (ping_parse_payload(buf, len, &seq, &ts) != -1) {
            printf("\n    FAIL at iter %d: parse should return -1 for non-PING\n",
                   iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests for ping payload edge cases                              */
/* Requirements: 2.1, 8.1, 8.3                                        */
/* ================================================================== */

/* Test: ping_build_payload with out_size < 14 returns -1 */
static int test_build_short_buffer(void)
{
    uint8_t buf[13];
    if (ping_build_payload(1, 1000000, buf, sizeof(buf)) != -1) {
        printf("\n    FAIL: build should return -1 for buffer < 14\n");
        return 0;
    }
    return 1;
}

/* Test: ping_parse_payload with buffer < 14 bytes returns -1 */
static int test_parse_short_buffer(void)
{
    uint8_t buf[13] = { 'P', 'I', 'N', 'G', 0, 1, 0, 0, 0, 0, 0, 0, 0 };
    uint16_t seq = 0;
    int64_t  ts  = 0;

    if (ping_parse_payload(buf, sizeof(buf), &seq, &ts) != -1) {
        printf("\n    FAIL: parse should return -1 for buffer < 14\n");
        return 0;
    }
    return 1;
}

/* Test: ping_parse_payload with exactly 14 valid bytes succeeds */
static int test_parse_exact_14_bytes(void)
{
    uint8_t buf[PING_PAYLOAD_LEN];
    uint16_t seq_in = 42;
    int64_t  ts_in  = 1234567890LL;

    if (ping_build_payload(seq_in, ts_in, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: build returned -1\n");
        return 0;
    }

    uint16_t seq_out = 0;
    int64_t  ts_out  = 0;
    if (ping_parse_payload(buf, PING_PAYLOAD_LEN, &seq_out, &ts_out) != 0) {
        printf("\n    FAIL: parse returned -1 for valid 14-byte buffer\n");
        return 0;
    }
    if (seq_out != seq_in || ts_out != ts_in) {
        printf("\n    FAIL: values mismatch: seq=%u/%u ts=%ld/%ld\n",
               seq_out, seq_in, (long)ts_out, (long)ts_in);
        return 0;
    }
    return 1;
}

/* Test: ping_build_payload with seq=1 and known timestamp produces expected bytes */
static int test_build_known_values(void)
{
    uint8_t buf[PING_PAYLOAD_LEN];
    uint16_t seq = 1;
    int64_t  ts  = 0x0001020304050607LL;

    if (ping_build_payload(seq, ts, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: build returned -1\n");
        return 0;
    }

    /* Magic: "PING" */
    if (buf[0] != 'P' || buf[1] != 'I' || buf[2] != 'N' || buf[3] != 'G') {
        printf("\n    FAIL: magic bytes wrong\n");
        return 0;
    }

    /* Seq: htons(1) = 0x00 0x01 */
    if (buf[4] != 0x00 || buf[5] != 0x01) {
        printf("\n    FAIL: seq bytes: %02X %02X, expected 00 01\n",
               buf[4], buf[5]);
        return 0;
    }

    /* Timestamp: big-endian 0x0001020304050607 */
    uint8_t expected_ts[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    if (memcmp(buf + 6, expected_ts, 8) != 0) {
        printf("\n    FAIL: timestamp bytes mismatch\n");
        printf("    Got:      ");
        for (int i = 6; i < 14; i++) printf("%02X ", buf[i]);
        printf("\n    Expected: ");
        for (int i = 0; i < 8; i++) printf("%02X ", expected_ts[i]);
        printf("\n");
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

    printf("Ping payload tests\n");
    printf("===================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(ping_roundtrip);
    TEST(ping_structure);
    TEST(ping_reject_non_ping);

    printf("\nUnit tests:\n");
    TEST(build_short_buffer);
    TEST(parse_short_buffer);
    TEST(parse_exact_14_bytes);
    TEST(build_known_values);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
