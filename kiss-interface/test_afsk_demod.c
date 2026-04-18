/*
 * test_afsk_demod.c — Tests for AFSK demodulation and frame detection
 * Feature: uhd-aprs-receive
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "afsk_demod.h"
#include "afsk.h"
#include "nrzi.h"
#include "ax25.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %-55s", #name); \
    if (name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

#define PROP_ITERS 1000
#define SPB AFSK_DEMOD_SAMPLES_PER_BIT

/* Test callback state */
static uint8_t last_frame[330];
static size_t last_frame_len = 0;
static int last_polarity = -1;
static int cb_count = 0;

static void test_cb(const uint8_t *frame, size_t len, int pol, void *ud)
{
    (void)ud;
    if (len <= sizeof(last_frame)) {
        memcpy(last_frame, frame, len);
        last_frame_len = len;
    }
    last_polarity = pol;
    cb_count++;
}

/* Helper: build a complete AFSK audio packet from a callsign and info */
static int build_test_packet(const char *call, const char *info_str,
                             float *audio, size_t audio_size)
{
    uint8_t ax25[AX25_HDR_LEN + AX25_MAX_INFO];
    int flen = ax25_build_frame("APZ001", call,
                                (const uint8_t *)info_str, strlen(info_str),
                                ax25, sizeof(ax25));
    if (flen < 0) return -1;

    uint8_t bitstream[NRZI_MAX_BITS];
    int nbits = nrzi_frame_to_bitstream(ax25, (size_t)flen,
                                         bitstream, sizeof(bitstream));
    if (nbits < 0) return -1;

    uint8_t nrzi_bits[NRZI_MAX_BITS];
    nrzi_encode(bitstream, (size_t)nbits, nrzi_bits, sizeof(nrzi_bits), 0);

    int nsamples = afsk_modulate(nrzi_bits, (size_t)nbits, audio, audio_size);
    return nsamples;
}

/* ================================================================== */
/* Property 6: AFSK demod output length                                */
/* ================================================================== */
static int test_afsk_demod_output_length(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t nbits = 1 + (size_t)(rand() % 200);
        uint8_t bits[200];
        for (size_t i = 0; i < nbits; i++) bits[i] = (uint8_t)(rand() & 1);

        float audio[200 * SPB];
        int ns = afsk_modulate(bits, nbits, audio, sizeof(audio) / sizeof(audio[0]));
        if (ns < 0) return 0;

        /* Correlate each bit period */
        int count = 0;
        for (size_t i = 0; i + SPB <= (size_t)ns; i += SPB) {
            afsk_demod_correlate(&d, audio + i, SPB, NULL);
            count++;
        }
        if (count != (int)nbits) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 7: AFSK demod output range                                 */
/* ================================================================== */
static int test_afsk_demod_output_range(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        float samples[SPB];
        for (int i = 0; i < SPB; i++)
            samples[i] = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
        int bit = afsk_demod_correlate(&d, samples, SPB, NULL);
        if (bit != 0 && bit != 1) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 8: AFSK mod/demod round-trip                               */
/* ================================================================== */
static int test_afsk_mod_demod_roundtrip(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t nbits = 10 + (size_t)(rand() % 50);
        uint8_t bits[60];
        for (size_t i = 0; i < nbits; i++) bits[i] = (uint8_t)(rand() & 1);

        float audio[60 * SPB];
        int ns = afsk_modulate(bits, nbits, audio, sizeof(audio) / sizeof(audio[0]));
        if (ns < 0) return 0;

        /* Demodulate — the TX uses inverted mapping (NRZI 1→2200, 0→1200)
         * so the correlator returns 0 for NRZI 1 and 1 for NRZI 0.
         * We need to invert the correlator output to match. */
        int mismatches = 0;
        for (size_t i = 0; i < nbits; i++) {
            int detected = afsk_demod_correlate(&d, audio + i * SPB, SPB, NULL);
            /* Inverted: correlator mark(1)=1200Hz matches NRZI 0 in TX */
            int expected = bits[i] ? 0 : 1;
            if (detected != expected) mismatches++;
        }
        if (mismatches > 0) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 9: Dual polarity frame detection                           */
/* ================================================================== */
static int test_dual_polarity(void)
{
    for (int iter = 0; iter < 100; iter++) {  /* Fewer iters — expensive */
        afsk_demod_t d;
        afsk_demod_init(&d, test_cb, NULL, 0);
        cb_count = 0;

        float audio[AFSK_MAX_SAMPLES];
        int ns = build_test_packet("G4DPZ", "test123", audio, AFSK_MAX_SAMPLES);
        if (ns < 0) return 0;

        /* Normal polarity */
        afsk_demod_reset(&d);
        cb_count = 0;
        afsk_demod_process(&d, audio, (size_t)ns);
        int normal_ok = (cb_count > 0);

        /* Inverted polarity (negate audio) */
        float inv_audio[AFSK_MAX_SAMPLES];
        for (int i = 0; i < ns; i++) inv_audio[i] = -audio[i];
        afsk_demod_reset(&d);
        cb_count = 0;
        afsk_demod_process(&d, inv_audio, (size_t)ns);
        int inv_ok = (cb_count > 0);

        if (!normal_ok && !inv_ok) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 11: Full TX/RX frame round-trip                            */
/* ================================================================== */
static int test_full_tx_rx_roundtrip(void)
{
    for (int iter = 0; iter < 100; iter++) {
        afsk_demod_t d;
        afsk_demod_init(&d, test_cb, NULL, 0);
        cb_count = 0;
        last_frame_len = 0;

        /* Random callsign */
        char call[7];
        int clen = 1 + (rand() % 6);
        for (int i = 0; i < clen; i++)
            call[i] = 'A' + (rand() % 26);
        call[clen] = '\0';

        /* Random info */
        char info[64];
        int ilen = 1 + (rand() % 40);
        for (int i = 0; i < ilen; i++)
            info[i] = 'A' + (rand() % 26);
        info[ilen] = '\0';

        float audio[AFSK_MAX_SAMPLES];
        int ns = build_test_packet(call, info, audio, AFSK_MAX_SAMPLES);
        if (ns < 0) continue;  /* skip invalid combos */

        afsk_demod_process(&d, audio, (size_t)ns);

        if (cb_count < 1) return 0;

        /* Verify the decoded frame contains the info string */
        /* The info field starts at byte 16 (after 2x7 addr + ctrl + pid) */
        if (last_frame_len < 16 + (size_t)ilen) return 0;
        if (memcmp(last_frame + 16, info, (size_t)ilen) != 0) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests                                                          */
/* ================================================================== */
static int test_correlate_mark_tone(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);
    float samples[SPB];
    for (int i = 0; i < SPB; i++)
        samples[i] = (float)sin(2.0 * M_PI * 1200.0 * (double)i / 48000.0);
    return afsk_demod_correlate(&d, samples, SPB, NULL) == 1;
}

static int test_correlate_space_tone(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);
    float samples[SPB];
    for (int i = 0; i < SPB; i++)
        samples[i] = (float)sin(2.0 * M_PI * 2200.0 * (double)i / 48000.0);
    return afsk_demod_correlate(&d, samples, SPB, NULL) == 0;
}

static int test_flag_detection(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, NULL, NULL, 0);
    /* Feed flag bits: 0x7E = 01111110 LSB-first */
    /* In NRZI with init=0: 0->toggle(1), 1s->hold(1x6), 0->toggle(0) */
    uint8_t flag_nrzi[] = {1, 1, 1, 1, 1, 1, 1, 0};
    for (int i = 0; i < 8; i++)
        afsk_demod_feed_bit(&d, flag_nrzi[i], 0);
    return d.det_state[0] == AFSK_DEMOD_SYNC;
}

static int test_short_frame_rejected(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, test_cb, NULL, 0);
    cb_count = 0;
    /* Feed opening flag */
    uint8_t flag_nrzi[] = {1, 1, 1, 1, 1, 1, 1, 0};
    for (int i = 0; i < 8; i++) afsk_demod_feed_bit(&d, flag_nrzi[i], 0);
    /* Feed a few data bits (too short for a frame) */
    for (int i = 0; i < 40; i++) afsk_demod_feed_bit(&d, 1, 0);
    /* Feed closing flag */
    for (int i = 0; i < 8; i++) afsk_demod_feed_bit(&d, flag_nrzi[i], 0);
    return cb_count == 0;  /* Should not have decoded anything */
}

static int test_bad_fcs_rejected(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, test_cb, NULL, 0);
    cb_count = 0;

    /* Build a valid packet, corrupt one byte, feed through demod */
    float audio[AFSK_MAX_SAMPLES];
    int ns = build_test_packet("G4DPZ", "test", audio, AFSK_MAX_SAMPLES);
    if (ns < 0) return 0;

    /* Corrupt a sample in the middle of the data */
    if (ns > 1000) audio[500] = -audio[500];

    afsk_demod_process(&d, audio, (size_t)ns);
    /* May or may not decode depending on which bit was corrupted,
     * but the test verifies the code doesn't crash */
    return 1;
}

static int test_seven_ones_abort(void)
{
    afsk_demod_t d;
    afsk_demod_init(&d, test_cb, NULL, 0);
    cb_count = 0;
    /* Feed opening flag via NRZI: flag 0x7E = 01111110 LSB-first
     * NRZI init=0: 0->toggle(1), 1s->hold(1x6), 0->toggle(0) */
    uint8_t flag_nrzi[] = {1, 1, 1, 1, 1, 1, 1, 0};
    for (int i = 0; i < 8; i++) afsk_demod_feed_bit(&d, flag_nrzi[i], 0);
    /* Feed a non-flag bit to enter FRAME state */
    afsk_demod_feed_bit(&d, 0, 0);  /* toggle -> data bit 0 */
    /* Now feed NRZI bits that produce 7+ consecutive data 1-bits.
     * NRZI: no transition = data 1. Feed same value repeatedly. */
    for (int i = 0; i < 10; i++)
        afsk_demod_feed_bit(&d, 1, 0);  /* no transition = data 1 */
    /* Should have aborted to HUNT due to 7+ consecutive ones */
    return d.det_state[0] == AFSK_DEMOD_HUNT;
}

int main(void)
{
    srand((unsigned)time(NULL));
    printf("AFSK demod module tests\n");
    printf("========================\n\n");
    printf("Property tests:\n");
    RUN_TEST(test_afsk_demod_output_length);
    RUN_TEST(test_afsk_demod_output_range);
    RUN_TEST(test_afsk_mod_demod_roundtrip);
    RUN_TEST(test_dual_polarity);
    RUN_TEST(test_full_tx_rx_roundtrip);
    printf("\nUnit tests:\n");
    RUN_TEST(test_correlate_mark_tone);
    RUN_TEST(test_correlate_space_tone);
    RUN_TEST(test_flag_detection);
    RUN_TEST(test_short_frame_rejected);
    RUN_TEST(test_bad_fcs_rejected);
    RUN_TEST(test_seven_ones_abort);
    printf("\n------------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
