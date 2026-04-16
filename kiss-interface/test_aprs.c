#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "aprs.h"
#include "ax25.h"
#include "beacon.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

#define ITERATIONS 1000

static double rand_double(double lo, double hi)
{
    return lo + ((double)rand() / (double)RAND_MAX) * (hi - lo);
}

/* Property 1: Frame classification */
static int test_frame_classification(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        size_t len = (size_t)(rand() % 64);
        uint8_t buf[64];
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rand() & 0xFF);

        int expected = (len >= 16 && buf[14] == 0x03 && buf[15] == 0xF0) ? 1 : 0;
        int got = aprs_is_ax25_frame(buf, len);
        if (got != expected) {
            printf("\n    FAIL at iter %d: len=%zu got=%d expected=%d\n",
                   iter, len, got, expected);
            return 0;
        }
    }
    return 1;
}

/* Property 2: Position decode round-trip with beacon encoder */
static int test_position_decode_roundtrip(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        double lat = rand_double(-90.0, 90.0);
        double lon = rand_double(-180.0, 180.0);

        char pos_str[BEACON_MAX_POSITION];
        int len = beacon_build_position(lat, lon, "test", pos_str, sizeof(pos_str));
        if (len < 0) {
            printf("\n    FAIL at iter %d: beacon_build_position failed\n", iter);
            return 0;
        }

        aprs_position_t pos;
        int rc = aprs_decode_position((const uint8_t *)pos_str, (size_t)len, &pos);
        if (rc != 0) {
            printf("\n    FAIL at iter %d: aprs_decode_position failed\n", iter);
            return 0;
        }

        double lat_err = fabs(lat - pos.lat) * 60.0;
        double lon_err = fabs(lon - pos.lon) * 60.0;
        if (lat_err > 0.02) {
            printf("\n    FAIL at iter %d: lat error %.4f arcmin\n", iter, lat_err);
            return 0;
        }
        if (lon_err > 0.02) {
            printf("\n    FAIL at iter %d: lon error %.4f arcmin\n", iter, lon_err);
            return 0;
        }
    }
    return 1;
}

/* Property 3: Malformed input resilience */
static int test_malformed_input_resilience(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        size_t len = (size_t)(rand() % 257);
        uint8_t buf[257];
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rand() & 0xFF);

        aprs_position_t pos;
        /* Should not crash, may return 0 or -1 */
        aprs_decode_position(buf, len, &pos);
    }
    return 1;
}

/* Unit: classify valid AX.25 frame */
static int test_classify_ax25(void)
{
    uint8_t frame[20];
    memset(frame, 0x40, sizeof(frame));
    frame[14] = 0x03; frame[15] = 0xF0;
    return aprs_is_ax25_frame(frame, sizeof(frame)) == 1;
}

/* Unit: classify LTP segment */
static int test_classify_ltp(void)
{
    uint8_t seg[20];
    memset(seg, 0, sizeof(seg));
    seg[0] = 0x02; /* version 0, type 2 */
    return aprs_is_ax25_frame(seg, sizeof(seg)) == 0;
}

/* Unit: classify short buffer */
static int test_classify_short(void)
{
    uint8_t buf[10];
    return aprs_is_ax25_frame(buf, sizeof(buf)) == 0;
}

/* Unit: decode known position */
static int test_decode_known_position(void)
{
    const char *info = "!5228.02N/00201.32W-test comment";
    aprs_position_t pos;
    int rc = aprs_decode_position((const uint8_t *)info, strlen(info), &pos);
    if (rc != 0) { printf("\n    FAIL: decode returned %d\n", rc); return 0; }
    if (fabs(pos.lat - 52.467) > 0.001) {
        printf("\n    FAIL: lat=%.4f expected ~52.467\n", pos.lat); return 0;
    }
    if (fabs(pos.lon - (-2.022)) > 0.001) {
        printf("\n    FAIL: lon=%.4f expected ~-2.022\n", pos.lon); return 0;
    }
    if (strcmp(pos.comment, "test comment") != 0) {
        printf("\n    FAIL: comment='%s'\n", pos.comment); return 0;
    }
    return 1;
}

/* Unit: decode '=' type */
static int test_decode_equals_type(void)
{
    const char *info = "=5228.02N/00201.32W-msg";
    aprs_position_t pos;
    return aprs_decode_position((const uint8_t *)info, strlen(info), &pos) == 0
           && pos.has_position;
}

/* Unit: decode '/' type with timestamp */
static int test_decode_slash_type(void)
{
    const char *info = "/092345z5228.02N/00201.32W-ts";
    aprs_position_t pos;
    int rc = aprs_decode_position((const uint8_t *)info, strlen(info), &pos);
    if (rc != 0) { printf("\n    FAIL: decode returned %d\n", rc); return 0; }
    if (!pos.has_position) { printf("\n    FAIL: no position\n"); return 0; }
    if (fabs(pos.lat - 52.467) > 0.001) {
        printf("\n    FAIL: lat=%.4f\n", pos.lat); return 0;
    }
    return 1;
}

/* Unit: unknown type returns -1 */
static int test_decode_unknown_type(void)
{
    const char *info = ">status text";
    aprs_position_t pos;
    return aprs_decode_position((const uint8_t *)info, strlen(info), &pos) == -1;
}

/* Unit: empty info returns -1 */
static int test_decode_empty(void)
{
    aprs_position_t pos;
    return aprs_decode_position(NULL, 0, &pos) == -1;
}

int main(void)
{
    srand((unsigned)time(NULL));

    printf("APRS decoder tests\n");
    printf("===================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(frame_classification);
    TEST(position_decode_roundtrip);
    TEST(malformed_input_resilience);

    printf("\nUnit tests:\n");
    TEST(classify_ax25);
    TEST(classify_ltp);
    TEST(classify_short);
    TEST(decode_known_position);
    TEST(decode_equals_type);
    TEST(decode_slash_type);
    TEST(decode_unknown_type);
    TEST(decode_empty);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
