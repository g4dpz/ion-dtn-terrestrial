/*
 * test_afsk.c — Tests for Bell 202 AFSK modulator
 * Feature: uhd-aprs-beacon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "afsk.h"
#include "nrzi.h"

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

/* Helper: estimate dominant frequency of a float audio segment via FFT-like
 * zero-crossing analysis. For segments >= 3 bit periods this is reliable. */
static double estimate_freq(const float *samples, size_t len, int sample_rate)
{
    if (len < 4) return 0.0;
    /* Count zero crossings */
    int crossings = 0;
    for (size_t i = 1; i < len; i++)
        if (samples[i - 1] * samples[i] < 0.0f)
            crossings++;
    double duration = (double)len / (double)sample_rate;
    return (double)crossings / (2.0 * duration);
}

/* ================================================================== */
/* Property 6: AFSK output length invariant                            */
/* ================================================================== */
static int test_afsk_output_length(void)
{
    /* Feature: uhd-aprs-beacon, Property 6 */
    float audio[AFSK_MAX_SAMPLES];
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t nbits = 1 + (size_t)(rand() % 200);
        uint8_t bits[200];
        for (size_t i = 0; i < nbits; i++)
            bits[i] = (uint8_t)(rand() & 1);

        int n = afsk_modulate(bits, nbits, audio, sizeof(audio) / sizeof(audio[0]));
        if (n != (int)(nbits * AFSK_SAMPLES_PER_BIT)) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 7: AFSK tone frequency correctness                         */
/* ================================================================== */
static int test_afsk_tone_frequency(void)
{
    /* Feature: uhd-aprs-beacon, Property 7 */
    float audio[AFSK_MAX_SAMPLES];
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        /* Generate a run of 5-20 identical bits for good frequency resolution */
        int bit_val = rand() & 1;
        size_t run_len = 5 + (size_t)(rand() % 16);
        uint8_t bits[20];
        for (size_t i = 0; i < run_len; i++)
            bits[i] = (uint8_t)bit_val;

        int n = afsk_modulate(bits, run_len, audio, sizeof(audio) / sizeof(audio[0]));
        if (n < 0) return 0;

        double measured = estimate_freq(audio, (size_t)n, AFSK_SAMPLE_RATE);
        /* Inverted mapping: NRZI 1 -> 2200, NRZI 0 -> 1200 */
        double expected = (bit_val == 1) ? AFSK_SPACE_FREQ : AFSK_MARK_FREQ;
        /* FFT bin width at this segment length */
        double bin_width = (double)AFSK_SAMPLE_RATE / (double)n;
        double tolerance = 50.0 + bin_width;
        if (fabs(measured - expected) > tolerance) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 8: AFSK sample range                                       */
/* ================================================================== */
static int test_afsk_sample_range(void)
{
    /* Feature: uhd-aprs-beacon, Property 8 */
    float audio[AFSK_MAX_SAMPLES];
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t nbits = 1 + (size_t)(rand() % 200);
        uint8_t bits[200];
        for (size_t i = 0; i < nbits; i++)
            bits[i] = (uint8_t)(rand() & 1);

        int n = afsk_modulate(bits, nbits, audio, sizeof(audio) / sizeof(audio[0]));
        if (n < 0) return 0;
        for (int i = 0; i < n; i++)
            if (audio[i] < -1.01f || audio[i] > 1.01f) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests                                                          */
/* ================================================================== */
static int test_afsk_single_mark_bit(void)
{
    /* NRZI 0 -> 1200 Hz (mark, inverted mapping) */
    float audio[AFSK_SAMPLES_PER_BIT * 10];
    /* Use 10 bits for better frequency resolution */
    uint8_t bits[10];
    memset(bits, 0, sizeof(bits));
    int n = afsk_modulate(bits, 10, audio, sizeof(audio) / sizeof(audio[0]));
    if (n != 10 * AFSK_SAMPLES_PER_BIT) return 0;
    double freq = estimate_freq(audio, (size_t)n, AFSK_SAMPLE_RATE);
    return fabs(freq - AFSK_MARK_FREQ) < AFSK_MARK_FREQ * 0.1;
}

static int test_afsk_single_space_bit(void)
{
    /* NRZI 1 -> 2200 Hz (space, inverted mapping) */
    uint8_t bits[10];
    memset(bits, 1, sizeof(bits));
    float audio[AFSK_SAMPLES_PER_BIT * 10];
    int n = afsk_modulate(bits, 10, audio, sizeof(audio) / sizeof(audio[0]));
    if (n != 10 * AFSK_SAMPLES_PER_BIT) return 0;
    double freq = estimate_freq(audio, (size_t)n, AFSK_SAMPLE_RATE);
    return fabs(freq - AFSK_SPACE_FREQ) < AFSK_SPACE_FREQ * 0.1;
}

static int test_afsk_continuous_phase(void)
{
    /* Verify no discontinuity at mark->space transition */
    uint8_t bits[2] = {0, 1}; /* mark then space */
    float audio[AFSK_SAMPLES_PER_BIT * 2];
    int n = afsk_modulate(bits, 2, audio, sizeof(audio) / sizeof(audio[0]));
    if (n != 2 * AFSK_SAMPLES_PER_BIT) return 0;

    /* Check boundary: difference between last sample of bit 0 and first of bit 1
     * should be small (continuous phase) */
    int boundary = AFSK_SAMPLES_PER_BIT;
    float diff = fabsf(audio[boundary] - audio[boundary - 1]);
    /* Max expected diff for one sample of phase advance at 2200 Hz */
    float max_diff = (float)fabs(sin(2.0 * M_PI * AFSK_SPACE_FREQ / AFSK_SAMPLE_RATE));
    return diff < max_diff + 0.15f;
}

static int test_afsk_empty(void)
{
    float audio[1];
    int n = afsk_modulate(NULL, 0, audio, 1);
    return n == 0;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */
int main(void)
{
    srand((unsigned)time(NULL));

    printf("AFSK module tests\n");
    printf("=================\n\n");

    printf("Property tests (%d iterations each):\n", PROP_ITERS);
    RUN_TEST(test_afsk_output_length);
    RUN_TEST(test_afsk_tone_frequency);
    RUN_TEST(test_afsk_sample_range);

    printf("\nUnit tests:\n");
    RUN_TEST(test_afsk_single_mark_bit);
    RUN_TEST(test_afsk_single_space_bit);
    RUN_TEST(test_afsk_continuous_phase);
    RUN_TEST(test_afsk_empty);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
