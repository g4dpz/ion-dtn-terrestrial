/*
 * test_uhd_tx.c — Tests for FM modulation, resampling, and parameter validation
 * Feature: uhd-aprs-beacon
 *
 * Tests FM modulator and resampler logic only — no UHD hardware needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "uhd_tx.h"

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

/* ================================================================== */
/* Property 9: FM modulator unit magnitude                             */
/* ================================================================== */
static int test_fm_unit_magnitude(void)
{
    /* Feature: uhd-aprs-beacon, Property 9 */
    float audio[1000], out_i[1000], out_q[1000];
    double sensitivity = 2.0 * M_PI * 3000.0 / 48000.0;

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = 1 + (size_t)(rand() % 500);
        for (size_t i = 0; i < len; i++)
            audio[i] = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;

        int n = uhd_tx_fm_modulate(audio, len, sensitivity, out_i, out_q);
        if (n != (int)len) return 0;

        for (int i = 0; i < n; i++) {
            float mag_sq = out_i[i] * out_i[i] + out_q[i] * out_q[i];
            if (fabsf(mag_sq - 1.0f) > 1e-4f) return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 10: Resampler zero-order hold correctness                  */
/* ================================================================== */
static int test_resampler_zoh(void)
{
    /* Feature: uhd-aprs-beacon, Property 10 */
    float in_i[500], in_q[500];
    float out_i[5000], out_q[5000];

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t len = 1 + (size_t)(rand() % 200);
        int ratio = 2 + (rand() % 9);  /* 2-10 */
        for (size_t i = 0; i < len; i++) {
            in_i[i] = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
            in_q[i] = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
        }

        int n = uhd_tx_resample_zoh(in_i, in_q, len, ratio,
                                     out_i, out_q, 5000);
        if (n != (int)(len * (size_t)ratio)) return 0;

        /* Verify each group of ratio samples equals the input */
        for (size_t i = 0; i < len; i++) {
            for (int r = 0; r < ratio; r++) {
                size_t idx = i * (size_t)ratio + (size_t)r;
                if (out_i[idx] != in_i[i]) return 0;
                if (out_q[idx] != in_q[i]) return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 11: Parameter validation                                   */
/* ================================================================== */
static int test_parameter_validation(void)
{
    /* Feature: uhd-aprs-beacon, Property 11 */
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        double freq = 140e6 + (double)(rand() % 10000000);
        int gain = -10 + (rand() % 110);
        double dev = 500.0 + (double)(rand() % 6000);
        int sr = 24000 * (1 + (rand() % 30));

        int expected_ok = 1;
        if (freq < UHD_TX_BAND_LOW || freq > UHD_TX_BAND_HIGH) expected_ok = 0;
        if (gain < UHD_TX_GAIN_MIN || gain > UHD_TX_GAIN_MAX) expected_ok = 0;
        if (dev < UHD_TX_DEVIATION_MIN || dev > UHD_TX_DEVIATION_MAX) expected_ok = 0;
        if (sr % UHD_TX_AUDIO_RATE != 0) expected_ok = 0;

        /* Suppress stderr output during validation tests */
        FILE *saved = stderr;
        stderr = fopen("/dev/null", "w");
        int result = uhd_tx_validate(freq, gain, sr, dev);
        fclose(stderr);
        stderr = saved;

        int actual_ok = (result == 0) ? 1 : 0;
        if (actual_ok != expected_ok) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests                                                          */
/* ================================================================== */
static int test_fm_known_dc(void)
{
    /* DC input (0.0) should produce constant phase = rotating at 0 Hz */
    float audio[40], out_i[40], out_q[40];
    memset(audio, 0, sizeof(audio));
    double sensitivity = 2.0 * M_PI * 3000.0 / 48000.0;
    int n = uhd_tx_fm_modulate(audio, 40, sensitivity, out_i, out_q);
    if (n != 40) return 0;
    /* Phase should stay at 0, so I=1, Q=0 for all samples */
    for (int i = 0; i < n; i++) {
        if (fabsf(out_i[i] - 1.0f) > 1e-4f) return 0;
        if (fabsf(out_q[i]) > 1e-4f) return 0;
    }
    return 1;
}

static int test_resample_ratio_10(void)
{
    /* Verify 10:1 resampling (our actual use case) */
    float in_i[3] = {1.0f, -0.5f, 0.3f};
    float in_q[3] = {0.0f, 0.7f, -1.0f};
    float out_i[30], out_q[30];
    int n = uhd_tx_resample_zoh(in_i, in_q, 3, 10, out_i, out_q, 30);
    if (n != 30) return 0;
    /* First 10 should be (1.0, 0.0) */
    for (int i = 0; i < 10; i++) {
        if (out_i[i] != 1.0f || out_q[i] != 0.0f) return 0;
    }
    /* Next 10 should be (-0.5, 0.7) */
    for (int i = 10; i < 20; i++) {
        if (out_i[i] != -0.5f || out_q[i] != 0.7f) return 0;
    }
    return 1;
}

static int test_validate_good_params(void)
{
    FILE *saved = stderr;
    stderr = fopen("/dev/null", "w");
    int r = uhd_tx_validate(144.85e6, 50, 480000, 3000.0);
    fclose(stderr);
    stderr = saved;
    return r == 0;
}

static int test_validate_bad_freq(void)
{
    FILE *saved = stderr;
    stderr = fopen("/dev/null", "w");
    int r = uhd_tx_validate(100e6, 50, 480000, 3000.0);
    fclose(stderr);
    stderr = saved;
    return r == -1;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */
int main(void)
{
    srand((unsigned)time(NULL));

    printf("UHD TX module tests\n");
    printf("====================\n\n");

    printf("Property tests (%d iterations each):\n", PROP_ITERS);
    RUN_TEST(test_fm_unit_magnitude);
    RUN_TEST(test_resampler_zoh);
    RUN_TEST(test_parameter_validation);

    printf("\nUnit tests:\n");
    RUN_TEST(test_fm_known_dc);
    RUN_TEST(test_resample_ratio_10);
    RUN_TEST(test_validate_good_params);
    RUN_TEST(test_validate_bad_freq);

    printf("\n--------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
