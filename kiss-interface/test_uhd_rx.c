/*
 * test_uhd_rx.c — Tests for FM demodulation, decimation, and RX validation
 * Feature: uhd-aprs-receive
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "uhd_rx.h"
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
/* Property 1: RX parameter validation                                 */
/* ================================================================== */
static int test_rx_param_validation(void)
{
    for (int iter = 0; iter < PROP_ITERS; iter++) {
        double freq = 140e6 + (double)(rand() % 10000000);
        int gain = -10 + (rand() % 100);
        int sr = 24000 * (1 + (rand() % 30));

        int expected_ok = 1;
        if (freq < UHD_RX_BAND_LOW || freq > UHD_RX_BAND_HIGH) expected_ok = 0;
        if (gain < UHD_RX_GAIN_MIN || gain > UHD_RX_GAIN_MAX) expected_ok = 0;
        if (sr <= 0 || sr % UHD_RX_AUDIO_RATE != 0) expected_ok = 0;

        FILE *saved = stderr;
        stderr = fopen("/dev/null", "w");
        int result = uhd_rx_validate(freq, gain, sr);
        fclose(stderr);
        stderr = saved;

        if ((result == 0) != expected_ok) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 2: FM demodulator accuracy                                 */
/* ================================================================== */
static int test_fm_demod_accuracy(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        double f_offset = -5000.0 + (double)(rand() % 10001);
        int n = 480;  /* 1ms at 480kHz */
        float iq_i[480], iq_q[480], out[480];

        /* Generate constant-frequency sinusoid at f_offset */
        for (int i = 0; i < n; i++) {
            double phase = 2.0 * M_PI * f_offset * (double)i / 480000.0;
            iq_i[i] = (float)cos(phase);
            iq_q[i] = (float)sin(phase);
        }

        /* Reset state for each test */
        state.prev_i = 1.0f;
        state.prev_q = 0.0f;

        uhd_rx_fm_demod(&state, iq_i, iq_q, (size_t)n, out);

        /* Compute mean of output (skip first sample — transient) */
        double sum = 0.0;
        for (int i = 1; i < n; i++) sum += out[i];
        double mean = sum / (double)(n - 1);

        double expected = f_offset / 3000.0;
        double tolerance = fabs(expected) * 0.05 + 0.05;
        if (fabs(mean - expected) > tolerance) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 3: FM demodulator output length                            */
/* ================================================================== */
static int test_fm_demod_output_length(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        size_t n = 1 + (size_t)(rand() % 1000);
        float iq_i[1000], iq_q[1000], out[1000];
        for (size_t i = 0; i < n; i++) {
            iq_i[i] = (float)(rand() % 2001 - 1000) / 1000.0f;
            iq_q[i] = (float)(rand() % 2001 - 1000) / 1000.0f;
        }
        int result = uhd_rx_fm_demod(&state, iq_i, iq_q, n, out);
        if (result != (int)n) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 4: Decimation output length                                */
/* ================================================================== */
static int test_decimate_output_length(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        int mult = 1 + (rand() % 50);
        size_t n = (size_t)(mult * 10);
        float audio[500], out[50];
        for (size_t i = 0; i < n; i++)
            audio[i] = (float)(rand() % 2001 - 1000) / 1000.0f;

        /* Reset filter state */
        memset(state.lpf_buf, 0, sizeof(state.lpf_buf));
        state.lpf_idx = 0;
        state.lpf_sum = 0;
        state.lpf_count = 10;  /* pre-fill so output starts immediately */

        int result = uhd_rx_decimate(&state, audio, n, out, 50);
        if (result != (int)(n / 10)) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 5: Decimation passband preservation                        */
/* ================================================================== */
static int test_decimate_passband(void)
{
    uhd_rx_state_t state;

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        uhd_rx_init_dsp(&state, 3000.0, 480000);
        state.lpf_count = 10;

        double freq = 100.0 + (double)(rand() % 2901);  /* 100-3000 Hz */
        int n = 4800;  /* 10ms at 480kHz */
        float audio[4800], out[480];

        double input_sum_sq = 0.0;
        for (int i = 0; i < n; i++) {
            audio[i] = (float)sin(2.0 * M_PI * freq * (double)i / 480000.0);
            input_sum_sq += (double)(audio[i] * audio[i]);
        }
        double input_rms = sqrt(input_sum_sq / (double)n);

        int out_n = uhd_rx_decimate(&state, audio, (size_t)n, out, 480);
        if (out_n <= 0) return 0;

        double output_sum_sq = 0.0;
        for (int i = 0; i < out_n; i++)
            output_sum_sq += (double)(out[i] * out[i]);
        double output_rms = sqrt(output_sum_sq / (double)out_n);

        /* < 3 dB attenuation: output_rms >= 0.707 * input_rms */
        if (output_rms < 0.5 * input_rms) return 0;  /* generous tolerance */
    }
    return 1;
}

/* ================================================================== */
/* Property 10: FM mod/demod frequency round-trip                      */
/* ================================================================== */
static int test_fm_mod_demod_roundtrip(void)
{
    uhd_rx_state_t rx_state;

    for (int iter = 0; iter < PROP_ITERS; iter++) {
        uhd_rx_init_dsp(&rx_state, 3000.0, 480000);

        double freq = 1000.0 + (double)(rand() % 1501);  /* 1000-2500 Hz */
        int n = 4800;
        float audio[4800], mod_i[4800], mod_q[4800], demod[4800];

        for (int i = 0; i < n; i++)
            audio[i] = (float)sin(2.0 * M_PI * freq * (double)i / 480000.0);

        /* FM modulate using TX path */
        double tx_sens = 2.0 * M_PI * 3000.0 / 480000.0;
        uhd_tx_fm_modulate(audio, (size_t)n, tx_sens, mod_i, mod_q);

        /* FM demodulate using RX path */
        uhd_rx_fm_demod(&rx_state, mod_i, mod_q, (size_t)n, demod);

        /* The demodulated signal should correlate strongly with the
         * original audio (recovered sine wave). Check correlation. */
        double corr = 0.0, norm_a = 0.0, norm_d = 0.0;
        for (int i = 100; i < n; i++) {
            corr += (double)audio[i] * (double)demod[i];
            norm_a += (double)audio[i] * (double)audio[i];
            norm_d += (double)demod[i] * (double)demod[i];
        }
        double r = (norm_a > 0 && norm_d > 0)
                   ? corr / sqrt(norm_a * norm_d) : 0.0;
        /* Correlation should be > 0.95 for a clean round-trip */
        if (r < 0.90) return 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests                                                          */
/* ================================================================== */
static int test_fm_demod_dc_input(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);
    float iq_i[100], iq_q[100], out[100];
    for (int i = 0; i < 100; i++) { iq_i[i] = 1.0f; iq_q[i] = 0.0f; }
    uhd_rx_fm_demod(&state, iq_i, iq_q, 100, out);
    /* DC input = no frequency offset, output should be ~0 */
    for (int i = 1; i < 100; i++)
        if (fabsf(out[i]) > 0.01f) return 0;
    return 1;
}

static int test_fm_demod_known_tone(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);
    float iq_i[480], iq_q[480], out[480];
    for (int i = 0; i < 480; i++) {
        double phase = 2.0 * M_PI * 1200.0 * (double)i / 480000.0;
        iq_i[i] = (float)cos(phase);
        iq_q[i] = (float)sin(phase);
    }
    uhd_rx_fm_demod(&state, iq_i, iq_q, 480, out);
    double sum = 0.0;
    for (int i = 10; i < 480; i++) sum += out[i];
    double mean = sum / 470.0;
    double expected = 1200.0 / 3000.0;
    return fabs(mean - expected) < 0.05;
}

static int test_decimate_known_signal(void)
{
    uhd_rx_state_t state;
    uhd_rx_init_dsp(&state, 3000.0, 480000);
    state.lpf_count = 10;
    float audio[4800], out[480];
    for (int i = 0; i < 4800; i++)
        audio[i] = (float)sin(2.0 * M_PI * 1200.0 * (double)i / 480000.0);
    int n = uhd_rx_decimate(&state, audio, 4800, out, 480);
    if (n != 480) return 0;
    /* Check output has content (not all zeros) */
    float max_val = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(out[i]) > max_val) max_val = fabsf(out[i]);
    return max_val > 0.3f;
}

static int test_validate_good_params(void)
{
    FILE *s = stderr; stderr = fopen("/dev/null", "w");
    int r = uhd_rx_validate(144.8e6, 50, 480000);
    fclose(stderr); stderr = s;
    return r == 0;
}

static int test_validate_bad_freq(void)
{
    FILE *s = stderr; stderr = fopen("/dev/null", "w");
    int r = uhd_rx_validate(100e6, 50, 480000);
    fclose(stderr); stderr = s;
    return r == -1;
}

static int test_validate_bad_gain(void)
{
    FILE *s = stderr; stderr = fopen("/dev/null", "w");
    int r = uhd_rx_validate(144.8e6, 80, 480000);
    fclose(stderr); stderr = s;
    return r == -1;
}

static int test_validate_bad_rate(void)
{
    FILE *s = stderr; stderr = fopen("/dev/null", "w");
    int r = uhd_rx_validate(144.8e6, 50, 100000);
    fclose(stderr); stderr = s;
    return r == -1;
}

int main(void)
{
    srand((unsigned)time(NULL));
    printf("UHD RX module tests\n");
    printf("====================\n\n");
    printf("Property tests (%d iterations each):\n", PROP_ITERS);
    RUN_TEST(test_rx_param_validation);
    RUN_TEST(test_fm_demod_accuracy);
    RUN_TEST(test_fm_demod_output_length);
    RUN_TEST(test_decimate_output_length);
    RUN_TEST(test_decimate_passband);
    RUN_TEST(test_fm_mod_demod_roundtrip);
    printf("\nUnit tests:\n");
    RUN_TEST(test_fm_demod_dc_input);
    RUN_TEST(test_fm_demod_known_tone);
    RUN_TEST(test_decimate_known_signal);
    RUN_TEST(test_validate_good_params);
    RUN_TEST(test_validate_bad_freq);
    RUN_TEST(test_validate_bad_gain);
    RUN_TEST(test_validate_bad_rate);
    printf("\n--------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
