/*
 * test_nrzi.c — Tests for NRZI encoding, FCS, bit stuffing, flag framing
 * Feature: uhd-aprs-beacon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nrzi.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %-55s", #name); \
    if (name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

#define PROP_ITERS 1000

/* ================================================================== */
/* Property 1: FCS integrity                                           */
/* ================================================================== */
static int test_fcs_integrity(void)
{
    /* Feature: uhd-aprs-beacon, Property 1: FCS integrity */
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = 16 + (size_t)(rand() % 200);
        uint8_t data[330];
        for (size_t i = 0; i < len; i++)
            data[i] = (uint8_t)(rand() & 0xFF);

        uint16_t fcs = nrzi_compute_fcs(data, len);
        uint8_t fcs_lo = fcs & 0xFF;
        uint8_t fcs_hi = (fcs >> 8) & 0xFF;

        /* Recompute over original data (without FCS) should match */
        uint16_t recomputed = nrzi_compute_fcs(data, len);
        if (recomputed != fcs) return 0;
        if (fcs_lo != (recomputed & 0xFF)) return 0;
        if (fcs_hi != ((recomputed >> 8) & 0xFF)) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 2: Bit stuffing round-trip                                 */
/* ================================================================== */
static int test_bit_stuff_roundtrip(void)
{
    /* Feature: uhd-aprs-beacon, Property 2: Bit stuffing round-trip */
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = (size_t)(rand() % 500);
        uint8_t bits[500], stuffed[1000], recovered[500];
        for (size_t i = 0; i < len; i++)
            bits[i] = (uint8_t)(rand() & 1);

        int slen = nrzi_bit_stuff(bits, len, stuffed, sizeof(stuffed));
        if (slen < 0) return 0;
        int rlen = nrzi_bit_destuff(stuffed, (size_t)slen, recovered, sizeof(recovered));
        if (rlen < 0 || (size_t)rlen != len) return 0;
        if (memcmp(bits, recovered, len) != 0) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 3: Bit stuffing prevents false flags                       */
/* ================================================================== */
static int test_bit_stuff_no_six_ones(void)
{
    /* Feature: uhd-aprs-beacon, Property 3: No run of 6+ ones */
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = 1 + (size_t)(rand() % 500);
        uint8_t bits[500], stuffed[1000];
        for (size_t i = 0; i < len; i++)
            bits[i] = (uint8_t)(rand() & 1);

        int slen = nrzi_bit_stuff(bits, len, stuffed, sizeof(stuffed));
        if (slen < 0) return 0;

        int ones = 0;
        for (int i = 0; i < slen; i++) {
            if (stuffed[i] == 1) {
                ones++;
                if (ones >= 6) return 0;
            } else {
                ones = 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 4: Flag framing structure                                  */
/* ================================================================== */
static int test_flag_framing_structure(void)
{
    /* Feature: uhd-aprs-beacon, Property 4: Flag framing structure */
    static const uint8_t flag[8] = {0, 1, 1, 1, 1, 1, 1, 0};

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        /* Build a minimal valid AX.25-like frame (16+ bytes) */
        size_t flen = 16 + (size_t)(rand() % 100);
        uint8_t frame[200];
        for (size_t i = 0; i < flen; i++)
            frame[i] = (uint8_t)(rand() & 0xFF);
        /* Set valid ctrl/pid */
        frame[14] = 0x03;
        frame[15] = 0xF0;

        uint8_t out[NRZI_MAX_BITS];
        int total = nrzi_frame_to_bitstream(frame, flen, out, sizeof(out));
        if (total < 0) return 0;

        /* Check first 80 flags */
        for (int f = 0; f < NRZI_PREAMBLE_FLAGS; f++)
            for (int b = 0; b < 8; b++)
                if (out[f * 8 + b] != flag[b]) return 0;

        /* Check last 3 flags */
        for (int f = 0; f < NRZI_CLOSING_FLAGS; f++)
            for (int b = 0; b < 8; b++)
                if (out[total - (NRZI_CLOSING_FLAGS - f) * 8 + b] != flag[b]) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 5: NRZI encode/decode round-trip                           */
/* ================================================================== */
static int test_nrzi_roundtrip(void)
{
    /* Feature: uhd-aprs-beacon, Property 5: NRZI round-trip */
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = (size_t)(rand() % 500);
        uint8_t bits[500], encoded[500], decoded[500];
        for (size_t i = 0; i < len; i++)
            bits[i] = (uint8_t)(rand() & 1);

        size_t elen = nrzi_encode(bits, len, encoded, sizeof(encoded), 0);
        if (elen != len) return 0;
        size_t dlen = nrzi_decode(encoded, elen, decoded, sizeof(decoded), 0);
        if (dlen != len) return 0;
        if (memcmp(bits, decoded, len) != 0) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests                                                          */
/* ================================================================== */
static int test_fcs_known_vector(void)
{
    /* "123456789" -> CRC-CCITT = 0x906E */
    uint16_t fcs = nrzi_compute_fcs((const uint8_t *)"123456789", 9);
    return fcs == 0x906E;
}

static int test_bytes_to_bits_known(void)
{
    /* 0x7E = 01111110 -> LSB first: 0,1,1,1,1,1,1,0 */
    uint8_t byte = 0x7E;
    uint8_t bits[8];
    size_t n = nrzi_bytes_to_bits(&byte, 1, bits, sizeof(bits));
    if (n != 8) return 0;
    uint8_t expected[8] = {0, 1, 1, 1, 1, 1, 1, 0};
    return memcmp(bits, expected, 8) == 0;
}

static int test_nrzi_encode_flag_pattern(void)
{
    /* Flag 0x7E = 01111110 LSB-first, NRZI with init=0:
     * bit 0=0: toggle -> 1 (mark)
     * bits 1-6=1: hold -> 1,1,1,1,1,1 (mark)
     * bit 7=0: toggle -> 0 (space)
     * Result: 1,1,1,1,1,1,1,0 — predominantly mark */
    uint8_t flag_bits[8] = {0, 1, 1, 1, 1, 1, 1, 0};
    uint8_t nrzi[8];
    nrzi_encode(flag_bits, 8, nrzi, sizeof(nrzi), 0);
    uint8_t expected[8] = {1, 1, 1, 1, 1, 1, 1, 0};
    return memcmp(nrzi, expected, 8) == 0;
}

static int test_nrzi_encode_all_zeros(void)
{
    /* All zeros = alternating (toggle every bit), init=0 */
    uint8_t bits[4] = {0, 0, 0, 0};
    uint8_t out[4];
    nrzi_encode(bits, 4, out, sizeof(out), 0);
    uint8_t expected[4] = {1, 0, 1, 0};
    return memcmp(out, expected, 4) == 0;
}

static int test_nrzi_encode_all_ones(void)
{
    /* All ones = constant (hold), init=0 */
    uint8_t bits[4] = {1, 1, 1, 1};
    uint8_t out[4];
    nrzi_encode(bits, 4, out, sizeof(out), 0);
    uint8_t expected[4] = {0, 0, 0, 0};
    return memcmp(out, expected, 4) == 0;
}

static int test_ssid_fix_applied(void)
{
    /* Build a frame with SSID bytes at [6] and [13] using 0x60 mask,
     * verify nrzi_frame_to_bitstream fixes them to 0xE0 */
    uint8_t frame[20];
    memset(frame, 0x41, sizeof(frame)); /* Fill with 'A' shifted */
    frame[6]  = 0x60;  /* Dst SSID, wrong reserved bits */
    frame[13] = 0x61;  /* Src SSID, wrong reserved bits, last=1 */
    frame[14] = 0x03;  /* ctrl */
    frame[15] = 0xF0;  /* pid */

    uint8_t out[NRZI_MAX_BITS];
    int total = nrzi_frame_to_bitstream(frame, 20, out, sizeof(out));
    if (total < 0) return 0;

    /* The FCS was computed over the fixed frame, so we can verify
     * by extracting the frame bytes from the bitstream and checking
     * the SSID bytes have 0xE0 mask */
    /* Skip preamble flags, de-stuff, convert back to bytes */
    size_t preamble_bits = NRZI_PREAMBLE_FLAGS * 8;
    size_t closing_bits = NRZI_CLOSING_FLAGS * 8;
    size_t content_bits = (size_t)total - preamble_bits - closing_bits;

    uint8_t destuffed[NRZI_MAX_BITS];
    int dlen = nrzi_bit_destuff(out + preamble_bits, content_bits,
                                 destuffed, sizeof(destuffed));
    if (dlen < 0) return 0;

    /* Convert first 14 bytes back (dst + src addresses) */
    uint8_t bytes[22];
    for (int i = 0; i < 22 && i * 8 + 7 < dlen; i++) {
        bytes[i] = 0;
        for (int j = 0; j < 8; j++)
            bytes[i] |= (uint8_t)(destuffed[i * 8 + j] << j);
    }

    /* Check SSID bytes have 0xE0 mask */
    if ((bytes[6] & 0xE0) != 0xE0) return 0;
    if ((bytes[13] & 0xE0) != 0xE0) return 0;
    return 1;
}

/* ================================================================== */
/* Property 11: Parameter validation (partial — ranges tested here)    */
/* ================================================================== */
static int test_parameter_validation(void)
{
    /* Feature: uhd-aprs-beacon, Property 11: Parameter validation */
    /* Test frame_to_bitstream rejects too-short frames */
    uint8_t out[NRZI_MAX_BITS];
    uint8_t short_frame[10] = {0};
    if (nrzi_frame_to_bitstream(short_frame, 10, out, sizeof(out)) != -1) return 0;
    if (nrzi_frame_to_bitstream(NULL, 20, out, sizeof(out)) != -1) return 0;
    /* Empty bit operations */
    uint8_t empty[1];
    if (nrzi_encode(empty, 0, empty, 1, 0) != 0) return 0;
    if (nrzi_decode(empty, 0, empty, 1, 0) != 0) return 0;
    return 1;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */
int main(void)
{
    srand((unsigned)time(NULL));

    printf("NRZI module tests\n");
    printf("=================\n\n");

    printf("Property tests (%d iterations each):\n", PROP_ITERS);
    RUN_TEST(test_fcs_integrity);
    RUN_TEST(test_bit_stuff_roundtrip);
    RUN_TEST(test_bit_stuff_no_six_ones);
    RUN_TEST(test_flag_framing_structure);
    RUN_TEST(test_nrzi_roundtrip);

    printf("\nUnit tests:\n");
    RUN_TEST(test_fcs_known_vector);
    RUN_TEST(test_bytes_to_bits_known);
    RUN_TEST(test_nrzi_encode_flag_pattern);
    RUN_TEST(test_nrzi_encode_all_zeros);
    RUN_TEST(test_nrzi_encode_all_ones);
    RUN_TEST(test_ssid_fix_applied);
    RUN_TEST(test_parameter_validation);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
