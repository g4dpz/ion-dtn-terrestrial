/*
 * test_beacon.c — Property-based and unit tests for APRS beacon module
 *
 * Uses hand-rolled random testing with rand()/srand() for property tests,
 * consistent with the existing test pattern in the codebase.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#include "beacon.h"
#include "ax25.h"
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
/* Helpers                                                             */
/* ================================================================== */

/* Generate a random double in [lo, hi] */
static double rand_double(double lo, double hi)
{
    return lo + ((double)rand() / (double)RAND_MAX) * (hi - lo);
}

/* Generate a random printable ASCII comment string (0 to max_len chars) */
static void rand_comment(char *buf, size_t buf_size, int max_len)
{
    int len = rand() % (max_len + 1);
    if ((size_t)len >= buf_size) len = (int)buf_size - 1;
    for (int i = 0; i < len; i++)
        buf[i] = (char)(32 + (rand() % 95));  /* printable ASCII */
    buf[len] = '\0';
}

/* ================================================================== */
/* Property 3: Coordinate conversion round-trip                        */
/* Feature: aprs-beacon, Property 3: Coordinate conversion round-trip  */
/* Validates: Requirements 1.3, 8.5                                    */
/* ================================================================== */

static int test_coordinate_roundtrip(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        double lat = rand_double(-90.0, 90.0);
        double lon = rand_double(-180.0, 180.0);

        char lat_str[16], lon_str[16];

        if (beacon_format_lat(lat, lat_str, sizeof(lat_str)) != 0) {
            printf("\n    FAIL at iter %d: format_lat failed for %f\n",
                   iter, lat);
            return 0;
        }
        if (beacon_format_lon(lon, lon_str, sizeof(lon_str)) != 0) {
            printf("\n    FAIL at iter %d: format_lon failed for %f\n",
                   iter, lon);
            return 0;
        }

        /* Parse lat back: "DDMM.MMH" — 8 chars */
        int lat_deg;
        double lat_min;
        char lat_hem;
        {
            char deg_buf[3] = { lat_str[0], lat_str[1], '\0' };
            char min_buf[6] = { lat_str[2], lat_str[3], lat_str[4],
                                lat_str[5], lat_str[6], '\0' };
            lat_deg = atoi(deg_buf);
            lat_min = atof(min_buf);
            lat_hem = lat_str[7];
        }
        if (lat_hem != 'N' && lat_hem != 'S') {
            printf("\n    FAIL at iter %d: could not parse lat '%s'\n",
                   iter, lat_str);
            return 0;
        }
        double lat_back = (double)lat_deg + lat_min / 60.0;
        if (lat_hem == 'S') lat_back = -lat_back;

        /* Parse lon back: "DDDMM.MMH" — parse minutes as substring to
         * avoid sscanf interpreting 'E' hemisphere as scientific notation */
        int lon_deg;
        double lon_min;
        char lon_hem;
        {
            /* lon_str is "DDDMM.MMH" — 9 chars */
            char deg_buf[4] = { lon_str[0], lon_str[1], lon_str[2], '\0' };
            char min_buf[6] = { lon_str[3], lon_str[4], lon_str[5],
                                lon_str[6], lon_str[7], '\0' };
            lon_deg = atoi(deg_buf);
            lon_min = atof(min_buf);
            lon_hem = lon_str[8];
        }
        if (lon_hem != 'E' && lon_hem != 'W') {
            printf("\n    FAIL at iter %d: could not parse lon '%s'\n",
                   iter, lon_str);
            return 0;
        }
        double lon_back = (double)lon_deg + lon_min / 60.0;
        if (lon_hem == 'W') lon_back = -lon_back;

        /* Check within 0.02 arcminutes (= 0.02/60 degrees) */
        double lat_err_min = fabs(lat - lat_back) * 60.0;
        double lon_err_min = fabs(lon - lon_back) * 60.0;

        if (lat_err_min > 0.02) {
            printf("\n    FAIL at iter %d: lat round-trip error %.4f arcmin "
                   "(orig=%f, back=%f, str='%s')\n",
                   iter, lat_err_min, lat, lat_back, lat_str);
            return 0;
        }
        if (lon_err_min > 0.02) {
            printf("\n    FAIL at iter %d: lon round-trip error %.4f arcmin "
                   "(orig=%f, back=%f, str='%s')\n",
                   iter, lon_err_min, lon, lon_back, lon_str);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 2: Position string structural invariants                   */
/* Feature: aprs-beacon, Property 2: Position string structure         */
/* Validates: Requirements 1.2, 8.1, 8.2, 8.3, 8.4                    */
/* ================================================================== */

static int test_position_string_structure(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        double lat = rand_double(-90.0, 90.0);
        double lon = rand_double(-180.0, 180.0);

        char comment[64];
        rand_comment(comment, sizeof(comment), 50);

        char pos[BEACON_MAX_POSITION];
        int len = beacon_build_position(lat, lon, comment, pos, sizeof(pos));
        if (len < 0) {
            printf("\n    FAIL at iter %d: build_position returned %d "
                   "(lat=%f, lon=%f)\n", iter, len, lat, lon);
            return 0;
        }

        /* Byte 0: '!' data type identifier */
        if (pos[0] != '!') {
            printf("\n    FAIL at iter %d: byte 0 = '%c', expected '!'\n",
                   iter, pos[0]);
            return 0;
        }

        /* Bytes 1-8: latitude "DDMM.MMH" (8 chars) */
        /* DD in [00,90] */
        int lat_dd = (pos[1] - '0') * 10 + (pos[2] - '0');
        if (lat_dd < 0 || lat_dd > 90) {
            printf("\n    FAIL at iter %d: lat degrees %d out of [0,90]\n",
                   iter, lat_dd);
            return 0;
        }
        /* MM.MM — check digits and dot */
        if (pos[5] != '.') {
            printf("\n    FAIL at iter %d: lat missing '.' at byte 5\n", iter);
            return 0;
        }
        /* Hemisphere at byte 8 */
        char lat_hem = pos[8];
        if (lat >= 0.0 && lat_hem != 'N') {
            printf("\n    FAIL at iter %d: lat_hem='%c' for lat=%f, expected 'N'\n",
                   iter, lat_hem, lat);
            return 0;
        }
        if (lat < 0.0 && lat_hem != 'S') {
            printf("\n    FAIL at iter %d: lat_hem='%c' for lat=%f, expected 'S'\n",
                   iter, lat_hem, lat);
            return 0;
        }

        /* Byte 9: '/' symbol table selector */
        if (pos[9] != '/') {
            printf("\n    FAIL at iter %d: byte 9 = '%c', expected '/'\n",
                   iter, pos[9]);
            return 0;
        }

        /* Bytes 10-18: longitude "DDDMM.MMH" (9 chars) */
        int lon_ddd = (pos[10] - '0') * 100 + (pos[11] - '0') * 10 + (pos[12] - '0');
        if (lon_ddd < 0 || lon_ddd > 180) {
            printf("\n    FAIL at iter %d: lon degrees %d out of [0,180]\n",
                   iter, lon_ddd);
            return 0;
        }
        if (pos[15] != '.') {
            printf("\n    FAIL at iter %d: lon missing '.' at byte 15\n", iter);
            return 0;
        }
        char lon_hem = pos[18];
        if (lon >= 0.0 && lon_hem != 'E') {
            printf("\n    FAIL at iter %d: lon_hem='%c' for lon=%f, expected 'E'\n",
                   iter, lon_hem, lon);
            return 0;
        }
        if (lon < 0.0 && lon_hem != 'W') {
            printf("\n    FAIL at iter %d: lon_hem='%c' for lon=%f, expected 'W'\n",
                   iter, lon_hem, lon);
            return 0;
        }

        /* Byte 19: '-' symbol code (house/QTH) */
        if (pos[19] != '-') {
            printf("\n    FAIL at iter %d: byte 19 = '%c', expected '-'\n",
                   iter, pos[19]);
            return 0;
        }

        /* Remaining bytes should be the comment */
        size_t comment_len = strlen(comment);
        if ((size_t)len != 20 + comment_len) {
            printf("\n    FAIL at iter %d: len=%d, expected %zu "
                   "(20 + comment_len=%zu)\n",
                   iter, len, 20 + comment_len, comment_len);
            return 0;
        }
        if (comment_len > 0 && memcmp(pos + 20, comment, comment_len) != 0) {
            printf("\n    FAIL at iter %d: comment mismatch\n", iter);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests: Coordinate formatting edge cases (Task 1.6)             */
/* ================================================================== */

/* Test: lat 52.467 → "5228.02N" */
static int test_format_lat_52_467(void)
{
    char buf[16];
    if (beacon_format_lat(52.467, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lat returned error\n");
        return 0;
    }
    if (strcmp(buf, "5228.02N") != 0) {
        printf("\n    FAIL: got '%s', expected '5228.02N'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: lon -2.022 → "00201.32W" */
static int test_format_lon_neg2_022(void)
{
    char buf[16];
    if (beacon_format_lon(-2.022, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lon returned error\n");
        return 0;
    }
    if (strcmp(buf, "00201.32W") != 0) {
        printf("\n    FAIL: got '%s', expected '00201.32W'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: lat 0.0 → "0000.00N" (equator) */
static int test_format_lat_zero(void)
{
    char buf[16];
    if (beacon_format_lat(0.0, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lat returned error\n");
        return 0;
    }
    if (strcmp(buf, "0000.00N") != 0) {
        printf("\n    FAIL: got '%s', expected '0000.00N'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: lon 0.0 → "00000.00E" (prime meridian) */
static int test_format_lon_zero(void)
{
    char buf[16];
    if (beacon_format_lon(0.0, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lon returned error\n");
        return 0;
    }
    if (strcmp(buf, "00000.00E") != 0) {
        printf("\n    FAIL: got '%s', expected '00000.00E'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: lat -90.0 → "9000.00S" (south pole) */
static int test_format_lat_south_pole(void)
{
    char buf[16];
    if (beacon_format_lat(-90.0, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lat returned error\n");
        return 0;
    }
    if (strcmp(buf, "9000.00S") != 0) {
        printf("\n    FAIL: got '%s', expected '9000.00S'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: lon 180.0 → "18000.00E" (antimeridian) */
static int test_format_lon_antimeridian(void)
{
    char buf[16];
    if (beacon_format_lon(180.0, buf, sizeof(buf)) != 0) {
        printf("\n    FAIL: format_lon returned error\n");
        return 0;
    }
    if (strcmp(buf, "18000.00E") != 0) {
        printf("\n    FAIL: got '%s', expected '18000.00E'\n", buf);
        return 0;
    }
    return 1;
}

/* Test: empty comment produces valid position-only string */
static int test_build_position_empty_comment(void)
{
    char pos[BEACON_MAX_POSITION];
    int len = beacon_build_position(52.467, -2.022, "", pos, sizeof(pos));
    if (len < 0) {
        printf("\n    FAIL: build_position returned %d\n", len);
        return 0;
    }
    /* Should be "!5228.02N/00201.32W-" (20 chars) */
    if (pos[0] != '!') {
        printf("\n    FAIL: byte 0 = '%c', expected '!'\n", pos[0]);
        return 0;
    }
    if (pos[19] != '-') {
        printf("\n    FAIL: byte 19 = '%c', expected '-'\n", pos[19]);
        return 0;
    }
    if (len != 20) {
        printf("\n    FAIL: len=%d, expected 20\n", len);
        return 0;
    }
    return 1;
}

/* Test: NULL comment produces valid position-only string */
static int test_build_position_null_comment(void)
{
    char pos[BEACON_MAX_POSITION];
    int len = beacon_build_position(52.467, -2.022, NULL, pos, sizeof(pos));
    if (len < 0) {
        printf("\n    FAIL: build_position returned %d\n", len);
        return 0;
    }
    if (len != 20) {
        printf("\n    FAIL: len=%d, expected 20\n", len);
        return 0;
    }
    return 1;
}

/* Test: default comment produces expected string */
static int test_build_position_default_comment(void)
{
    char pos[BEACON_MAX_POSITION];
    int len = beacon_build_position(52.467, -2.022, BEACON_DEFAULT_COMMENT,
                                    pos, sizeof(pos));
    if (len < 0) {
        printf("\n    FAIL: build_position returned %d\n", len);
        return 0;
    }
    /* Expected: "!5228.02N/00201.32W-github.com/g4dpz/ion-dtn-terrestrial" */
    const char *expected = "!5228.02N/00201.32W-" BEACON_DEFAULT_COMMENT;
    if (strcmp(pos, expected) != 0) {
        printf("\n    FAIL: got '%s'\n    expected '%s'\n", pos, expected);
        return 0;
    }
    return 1;
}

/* Test: out-of-range latitude rejected */
static int test_format_lat_out_of_range(void)
{
    char buf[16];
    if (beacon_format_lat(91.0, buf, sizeof(buf)) != -1) {
        printf("\n    FAIL: format_lat accepted lat=91.0\n");
        return 0;
    }
    if (beacon_format_lat(-91.0, buf, sizeof(buf)) != -1) {
        printf("\n    FAIL: format_lat accepted lat=-91.0\n");
        return 0;
    }
    return 1;
}

/* Test: out-of-range longitude rejected */
static int test_format_lon_out_of_range(void)
{
    char buf[16];
    if (beacon_format_lon(181.0, buf, sizeof(buf)) != -1) {
        printf("\n    FAIL: format_lon accepted lon=181.0\n");
        return 0;
    }
    if (beacon_format_lon(-181.0, buf, sizeof(buf)) != -1) {
        printf("\n    FAIL: format_lon accepted lon=-181.0\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 1: Beacon frame construction round-trip                    */
/* Feature: aprs-beacon, Property 1: Beacon frame round-trip           */
/* Validates: Requirements 1.1, 5.1                                    */
/* ================================================================== */

/* Generate a random valid callsign: 1-6 uppercase alphanumeric + optional SSID 0-15 */
static void rand_callsign(char *buf, size_t buf_size)
{
    static const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int base_len = 1 + (rand() % 6);  /* 1 to 6 */
    int i;
    for (i = 0; i < base_len; i++)
        buf[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

    int ssid = rand() % 16;  /* 0 to 15 */
    if (ssid > 0 || (rand() % 2)) {
        int written = snprintf(buf + base_len, buf_size - (size_t)base_len,
                               "-%d", ssid);
        buf[base_len + written] = '\0';
    } else {
        buf[base_len] = '\0';
    }
}

static int test_beacon_frame_roundtrip(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        char callsign[10];
        rand_callsign(callsign, sizeof(callsign));

        double lat = rand_double(-90.0, 90.0);
        double lon = rand_double(-180.0, 180.0);

        char comment[64];
        rand_comment(comment, sizeof(comment), 50);

        beacon_state_t state;
        int rc = beacon_init(&state, callsign, lat, lon, comment, 120);
        if (rc != 0) {
            printf("\n    FAIL at iter %d: beacon_init failed for callsign='%s' "
                   "lat=%f lon=%f\n", iter, callsign, lat, lon);
            return 0;
        }

        /* KISS-decode the stored frame to get the AX.25 frame */
        kiss_decoder_t dec;
        kiss_decoder_init(&dec);
        uint8_t ax25_buf[512];
        size_t ax25_len = 0;
        int got_frame = 0;
        for (int i = 0; i < state.kiss_frame_len; i++) {
            int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                      ax25_buf, sizeof(ax25_buf), &ax25_len);
            if (r == 1) { got_frame = 1; break; }
        }
        if (!got_frame) {
            printf("\n    FAIL at iter %d: KISS decode failed\n", iter);
            return 0;
        }

        /* Strip AX.25 frame */
        char src_call[16];
        char dst_call[16];
        const uint8_t *info = NULL;
        int info_len = ax25_strip_frame(ax25_buf, ax25_len,
                                        src_call, dst_call, &info);
        if (info_len < 0) {
            printf("\n    FAIL at iter %d: ax25_strip_frame failed\n", iter);
            return 0;
        }

        /* Verify destination is "APZ001-0" */
        if (strcmp(dst_call, "APZ001-0") != 0) {
            printf("\n    FAIL at iter %d: dst='%s', expected 'APZ001-0'\n",
                   iter, dst_call);
            return 0;
        }

        /* Verify source matches input callsign.
         * ax25_decode_addr always produces "CALL-SSID" with uppercase.
         * Build expected: uppercase base + SSID (default -0 if none). */
        char expected_src[16];
        {
            const char *d = strchr(callsign, '-');
            size_t blen = d ? (size_t)(d - callsign) : strlen(callsign);
            int ssid = d ? atoi(d + 1) : 0;
            char upper[7];
            for (size_t j = 0; j < blen; j++)
                upper[j] = (char)toupper((unsigned char)callsign[j]);
            upper[blen] = '\0';
            snprintf(expected_src, sizeof(expected_src), "%s-%d", upper, ssid);
        }
        if (strcmp(src_call, expected_src) != 0) {
            printf("\n    FAIL at iter %d: src='%s', expected '%s' "
                   "(input='%s')\n", iter, src_call, expected_src, callsign);
            return 0;
        }

        /* Verify info field starts with '!' */
        if (info_len < 1 || info[0] != '!') {
            printf("\n    FAIL at iter %d: info doesn't start with '!'\n", iter);
            return 0;
        }

        /* Verify info field contains the comment */
        if (strlen(comment) > 0) {
            /* The info field is not NUL-terminated, so use memmem-like search */
            size_t clen = strlen(comment);
            int found = 0;
            for (int j = 0; j <= info_len - (int)clen; j++) {
                if (memcmp(info + j, comment, clen) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                printf("\n    FAIL at iter %d: comment not found in info field\n",
                       iter);
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 4: Invalid input rejection                                 */
/* Feature: aprs-beacon, Property 4: Invalid input rejection           */
/* Validates: Requirements 1.7, 1.8                                    */
/* ================================================================== */

static int test_invalid_input_rejection(void)
{
    beacon_state_t state;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        int test_type = rand() % 5;

        switch (test_type) {
        case 0: {
            /* Empty callsign */
            int rc = beacon_init(&state, "", 52.0, -2.0, "test", 120);
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted empty callsign\n", iter);
                return 0;
            }
            break;
        }
        case 1: {
            /* Callsign >6 chars (base, no SSID) */
            char long_call[16];
            int len = 7 + (rand() % 5);  /* 7 to 11 chars */
            for (int i = 0; i < len; i++)
                long_call[i] = 'A' + (rand() % 26);
            long_call[len] = '\0';
            int rc = beacon_init(&state, long_call, 52.0, -2.0, "test", 120);
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted long callsign '%s'\n",
                       iter, long_call);
                return 0;
            }
            break;
        }
        case 2: {
            /* Non-alphanumeric callsign */
            static const char bad_chars[] = "!@#$%^&*()_+=[]{}|;:',.<>?/~` ";
            char bad_call[8];
            int blen = 1 + (rand() % 6);
            for (int i = 0; i < blen; i++) {
                if (rand() % 3 == 0)
                    bad_call[i] = bad_chars[rand() % (sizeof(bad_chars) - 1)];
                else
                    bad_call[i] = 'A' + (rand() % 26);
            }
            /* Ensure at least one bad char */
            bad_call[rand() % blen] = bad_chars[rand() % (sizeof(bad_chars) - 1)];
            bad_call[blen] = '\0';
            int rc = beacon_init(&state, bad_call, 52.0, -2.0, "test", 120);
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted non-alnum callsign '%s'\n",
                       iter, bad_call);
                return 0;
            }
            break;
        }
        case 3: {
            /* Latitude outside [-90, +90] */
            double bad_lat;
            if (rand() % 2)
                bad_lat = 90.0 + rand_double(0.001, 1000.0);
            else
                bad_lat = -90.0 - rand_double(0.001, 1000.0);
            int rc = beacon_init(&state, "TEST", bad_lat, 0.0, "test", 120);
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted lat=%f\n", iter, bad_lat);
                return 0;
            }
            break;
        }
        case 4: {
            /* Longitude outside [-180, +180] */
            double bad_lon;
            if (rand() % 2)
                bad_lon = 180.0 + rand_double(0.001, 1000.0);
            else
                bad_lon = -180.0 - rand_double(0.001, 1000.0);
            int rc = beacon_init(&state, "TEST", 0.0, bad_lon, "test", 120);
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted lon=%f\n", iter, bad_lon);
                return 0;
            }
            break;
        }
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 5: Beacon interval validation                              */
/* Feature: aprs-beacon, Property 5: Beacon interval validation        */
/* Validates: Requirements 3.4                                         */
/* ================================================================== */

static int test_interval_validation(void)
{
    beacon_state_t state;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Generate a random interval: mix of valid and invalid */
        int interval;
        if (rand() % 2) {
            /* Valid range [10, 3600] */
            interval = 10 + (rand() % 3591);
        } else {
            /* Invalid: either < 10 or > 3600 */
            if (rand() % 2)
                interval = -(rand() % 10000);       /* negative or zero */
            else
                interval = 3601 + (rand() % 10000); /* > 3600 */
        }

        int rc = beacon_init(&state, "TEST", 52.0, -2.0, "test", interval);

        if (interval >= BEACON_MIN_INTERVAL && interval <= BEACON_MAX_INTERVAL) {
            if (rc != 0) {
                printf("\n    FAIL at iter %d: rejected valid interval %d\n",
                       iter, interval);
                return 0;
            }
        } else {
            if (rc != -1) {
                printf("\n    FAIL at iter %d: accepted invalid interval %d\n",
                       iter, interval);
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests: beacon_init edge cases (Task 2.5)                       */
/* ================================================================== */

/* Test: beacon_init with "APZ001" as TOCALL (verify dst in frame) */
static int test_init_tocall(void)
{
    beacon_state_t state;
    int rc = beacon_init(&state, "G4DPZ-1", 52.467, -2.022,
                         BEACON_DEFAULT_COMMENT, 120);
    if (rc != 0) {
        printf("\n    FAIL: beacon_init returned %d\n", rc);
        return 0;
    }

    /* KISS-decode */
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);
    uint8_t ax25_buf[512];
    size_t ax25_len = 0;
    int got_frame = 0;
    for (int i = 0; i < state.kiss_frame_len; i++) {
        int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                  ax25_buf, sizeof(ax25_buf), &ax25_len);
        if (r == 1) { got_frame = 1; break; }
    }
    if (!got_frame) {
        printf("\n    FAIL: KISS decode failed\n");
        return 0;
    }

    char dst_call[AX25_MAX_CALLSIGN];
    if (ax25_strip_frame(ax25_buf, ax25_len, NULL, dst_call, NULL) < 0) {
        printf("\n    FAIL: ax25_strip_frame failed\n");
        return 0;
    }
    if (strcmp(dst_call, "APZ001-0") != 0) {
        printf("\n    FAIL: dst='%s', expected 'APZ001-0'\n", dst_call);
        return 0;
    }
    return 1;
}

/* Test: beacon_init sets control=0x03, PID=0xF0 in frame */
static int test_init_ctrl_pid(void)
{
    beacon_state_t state;
    int rc = beacon_init(&state, "G4DPZ-1", 52.467, -2.022, "test", 120);
    if (rc != 0) {
        printf("\n    FAIL: beacon_init returned %d\n", rc);
        return 0;
    }

    /* KISS-decode */
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);
    uint8_t ax25_buf[512];
    size_t ax25_len = 0;
    int got_frame = 0;
    for (int i = 0; i < state.kiss_frame_len; i++) {
        int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                  ax25_buf, sizeof(ax25_buf), &ax25_len);
        if (r == 1) { got_frame = 1; break; }
    }
    if (!got_frame) {
        printf("\n    FAIL: KISS decode failed\n");
        return 0;
    }

    /* AX.25 header: bytes 14=control, 15=PID */
    if (ax25_len < 16) {
        printf("\n    FAIL: frame too short (%zu bytes)\n", ax25_len);
        return 0;
    }
    if (ax25_buf[14] != 0x03) {
        printf("\n    FAIL: control=0x%02X, expected 0x03\n", ax25_buf[14]);
        return 0;
    }
    if (ax25_buf[15] != 0xF0) {
        printf("\n    FAIL: PID=0x%02X, expected 0xF0\n", ax25_buf[15]);
        return 0;
    }
    return 1;
}

/* Test: beacon_init rejects empty callsign */
static int test_init_reject_empty_callsign(void)
{
    beacon_state_t state;
    if (beacon_init(&state, "", 52.0, -2.0, "test", 120) != -1) {
        printf("\n    FAIL: accepted empty callsign\n");
        return 0;
    }
    return 1;
}

/* Test: beacon_init rejects lat=91.0 */
static int test_init_reject_lat_91(void)
{
    beacon_state_t state;
    if (beacon_init(&state, "TEST", 91.0, -2.0, "test", 120) != -1) {
        printf("\n    FAIL: accepted lat=91.0\n");
        return 0;
    }
    return 1;
}

/* Test: beacon_init rejects interval=5 */
static int test_init_reject_interval_5(void)
{
    beacon_state_t state;
    if (beacon_init(&state, "TEST", 52.0, -2.0, "test", 5) != -1) {
        printf("\n    FAIL: accepted interval=5\n");
        return 0;
    }
    return 1;
}

/* Test: beacon_init accepts interval=120 */
static int test_init_accept_interval_120(void)
{
    beacon_state_t state;
    if (beacon_init(&state, "TEST", 52.0, -2.0, "test", 120) != 0) {
        printf("\n    FAIL: rejected interval=120\n");
        return 0;
    }
    if (state.initialized != 1) {
        printf("\n    FAIL: initialized=%d, expected 1\n", state.initialized);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 6: Beacon timeout calculation correctness                  */
/* Feature: aprs-beacon, Property 6: Beacon timeout calculation        */
/* Validates: Requirements 4.5                                         */
/* ================================================================== */

static int test_timeout_calculation(void)
{
    beacon_state_t state;
    int rc = beacon_init(&state, "G4DPZ-1", 52.467, -2.022,
                         "test", 120);
    if (rc != 0) {
        printf("\n    FAIL: beacon_init failed\n");
        return 0;
    }

    /* Set last_tx to now */
    clock_gettime(CLOCK_MONOTONIC, &state.last_tx);

    /* Timeout should be approximately 120000 ms */
    int timeout = beacon_get_timeout_ms(&state);
    if (timeout < 119900 || timeout > 120100) {
        printf("\n    FAIL: timeout=%d, expected ~120000\n", timeout);
        return 0;
    }

    /* beacon_is_due should return 0 (not due yet) */
    if (beacon_is_due(&state) != 0) {
        printf("\n    FAIL: beacon_is_due should be 0\n");
        return 0;
    }

    /* Set last_tx to 121 seconds ago — should be due */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    state.last_tx.tv_sec = now.tv_sec - 121;
    state.last_tx.tv_nsec = now.tv_nsec;

    timeout = beacon_get_timeout_ms(&state);
    if (timeout != 0) {
        printf("\n    FAIL: timeout=%d after 121s, expected 0\n", timeout);
        return 0;
    }

    if (beacon_is_due(&state) != 1) {
        printf("\n    FAIL: beacon_is_due should be 1 after 121s\n");
        return 0;
    }

    /* Not initialized should return -1 */
    beacon_state_t uninit;
    memset(&uninit, 0, sizeof(uninit));
    if (beacon_get_timeout_ms(&uninit) != -1) {
        printf("\n    FAIL: uninitialized should return -1\n");
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

    printf("Beacon module tests\n");
    printf("====================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(coordinate_roundtrip);
    TEST(position_string_structure);
    TEST(beacon_frame_roundtrip);
    TEST(invalid_input_rejection);
    TEST(interval_validation);

    printf("\nUnit tests:\n");
    TEST(format_lat_52_467);
    TEST(format_lon_neg2_022);
    TEST(format_lat_zero);
    TEST(format_lon_zero);
    TEST(format_lat_south_pole);
    TEST(format_lon_antimeridian);
    TEST(build_position_empty_comment);
    TEST(build_position_null_comment);
    TEST(build_position_default_comment);
    TEST(format_lat_out_of_range);
    TEST(format_lon_out_of_range);
    TEST(init_tocall);
    TEST(init_ctrl_pid);
    TEST(init_reject_empty_callsign);
    TEST(init_reject_lat_91);
    TEST(init_reject_interval_5);
    TEST(init_accept_interval_120);
    TEST(timeout_calculation);

    printf("\n------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
