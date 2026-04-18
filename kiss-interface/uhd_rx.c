/*
 * uhd_rx.c — UHD SDR receive, FM demodulation, and decimation
 *
 * FM discriminator: atan2(cross, dot) on consecutive IQ samples.
 * Decimation: moving-average LPF of length decim_ratio.
 */

#include "uhd_rx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* uhd_rx_validate                                                     */
/* ------------------------------------------------------------------ */
int uhd_rx_validate(double freq_hz, int gain, int sdr_sample_rate)
{
    int ok = 1;
    if (freq_hz < UHD_RX_BAND_LOW || freq_hz > UHD_RX_BAND_HIGH) {
        fprintf(stderr, "error: frequency %.3f MHz outside 2m band\n",
                freq_hz / 1e6);
        ok = 0;
    }
    if (gain < UHD_RX_GAIN_MIN || gain > UHD_RX_GAIN_MAX) {
        fprintf(stderr, "error: gain %d outside range %d-%d\n",
                gain, UHD_RX_GAIN_MIN, UHD_RX_GAIN_MAX);
        ok = 0;
    }
    if (sdr_sample_rate <= 0 || sdr_sample_rate % UHD_RX_AUDIO_RATE != 0) {
        fprintf(stderr, "error: sample rate %d not positive multiple of %d\n",
                sdr_sample_rate, UHD_RX_AUDIO_RATE);
        ok = 0;
    }
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* uhd_rx_init_dsp — initialise DSP state (no UHD)                     */
/* ------------------------------------------------------------------ */
void uhd_rx_init_dsp(uhd_rx_state_t *state, double deviation,
                     int sdr_sample_rate)
{
    memset(state, 0, sizeof(*state));
    state->deviation = deviation;
    state->sdr_sample_rate = sdr_sample_rate;
    state->sensitivity = 2.0 * M_PI * deviation / (double)sdr_sample_rate;
    state->decim_ratio = sdr_sample_rate / UHD_RX_AUDIO_RATE;
    state->prev_i = 1.0f;
    state->prev_q = 0.0f;
}

/* ------------------------------------------------------------------ */
/* uhd_rx_fm_demod — atan2 FM discriminator                            */
/* ------------------------------------------------------------------ */
int uhd_rx_fm_demod(uhd_rx_state_t *state,
                    const float *iq_i, const float *iq_q,
                    size_t num_samples, float *out)
{
    double sens = state->sensitivity;
    if (sens == 0.0) sens = 1.0;

    for (size_t i = 0; i < num_samples; i++) {
        float ci = iq_i[i], cq = iq_q[i];
        float pi = state->prev_i, pq = state->prev_q;

        /* cross = Im(z[n] * conj(z[n-1])) = I[n-1]*Q[n] - Q[n-1]*I[n] */
        float cross = pi * cq - pq * ci;
        /* dot = Re(z[n] * conj(z[n-1])) = I[n-1]*I[n] + Q[n-1]*Q[n] */
        float dot = pi * ci + pq * cq;

        float dphi = atan2f(cross, dot);
        out[i] = (float)(dphi / sens);

        state->prev_i = ci;
        state->prev_q = cq;
    }
    return (int)num_samples;
}

/* ------------------------------------------------------------------ */
/* uhd_rx_decimate — moving-average LPF + decimation                   */
/* ------------------------------------------------------------------ */
int uhd_rx_decimate(uhd_rx_state_t *state,
                    const float *audio, size_t num_samples,
                    float *out, size_t out_size)
{
    int ratio = state->decim_ratio;
    if (ratio <= 0) ratio = UHD_RX_DECIM_RATIO;
    size_t out_count = num_samples / (size_t)ratio;
    if (out_count > out_size) return -1;

    size_t out_idx = 0;
    for (size_t i = 0; i < num_samples; i++) {
        /* Update moving average ring buffer */
        state->lpf_sum -= state->lpf_buf[state->lpf_idx];
        state->lpf_buf[state->lpf_idx] = audio[i];
        state->lpf_sum += audio[i];
        state->lpf_idx = (state->lpf_idx + 1) % ratio;
        state->lpf_count++;

        /* Output one sample per ratio inputs */
        if (state->lpf_count >= ratio && (i + 1) % (size_t)ratio == 0) {
            out[out_idx++] = state->lpf_sum / (float)ratio;
        }
    }
    return (int)out_idx;
}

/* ================================================================== */
/* UHD C API functions — only compiled when HAVE_UHD is defined        */
/* ================================================================== */
#ifdef HAVE_UHD

#include <uhd.h>

int uhd_rx_init(uhd_rx_state_t *state, double freq_hz, int gain,
                int sdr_sample_rate, double deviation, int verbose)
{
    uhd_rx_init_dsp(state, deviation, sdr_sample_rate);
    state->freq_hz = freq_hz;
    state->gain = gain;
    state->verbose = verbose;

    if (uhd_rx_validate(freq_hz, gain, sdr_sample_rate) != 0)
        return -1;

    uhd_usrp_handle usrp;
    if (uhd_usrp_make(&usrp, "") != UHD_ERROR_NONE) {
        fprintf(stderr, "error: B200 mini not detected\n");
        return -1;
    }
    state->usrp = usrp;

    uhd_usrp_set_rx_rate(usrp, (double)sdr_sample_rate, 0);
    uhd_tune_request_t tune = {
        .target_freq = freq_hz,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t result;
    uhd_usrp_set_rx_freq(usrp, &tune, 0, &result);
    uhd_usrp_set_rx_gain(usrp, (double)gain, 0, "");

    uhd_rx_streamer_handle streamer;
    uhd_rx_streamer_make(&streamer);
    uhd_stream_args_t stream_args = {
        .cpu_format = "fc32",
        .otw_format = "sc16",
        .args = "",
        .channel_list = (size_t[]){0},
        .n_channels = 1,
    };
    uhd_usrp_get_rx_stream(usrp, &stream_args, streamer);
    state->rx_streamer = streamer;

    uhd_rx_metadata_handle md;
    uhd_rx_metadata_make(&md);
    state->rx_metadata = md;

    /* Start streaming */
    uhd_stream_cmd_t cmd = {
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = true,
    };
    uhd_rx_streamer_issue_stream_cmd(streamer, &cmd);

    state->initialized = 1;

    if (verbose) {
        double actual_rate;
        uhd_usrp_get_rx_rate(usrp, 0, &actual_rate);
        printf("UHD RX: freq=%.3f MHz, gain=%d, rate=%.0f Hz\n",
               freq_hz / 1e6, gain, actual_rate);
    }

    return 0;
}

int uhd_rx_receive(uhd_rx_state_t *state,
                   float *out_i, float *out_q, size_t max_samples)
{
    if (!state || !state->initialized) return -1;

    uhd_rx_streamer_handle streamer = (uhd_rx_streamer_handle)state->rx_streamer;
    uhd_rx_metadata_handle md = (uhd_rx_metadata_handle)state->rx_metadata;

    /* Interleaved fc32 buffer */
    static float iq_buf[UHD_RX_CHUNK_SAMPLES * 2];
    size_t to_recv = max_samples;
    if (to_recv > UHD_RX_CHUNK_SAMPLES) to_recv = UHD_RX_CHUNK_SAMPLES;

    void *buffs[] = {iq_buf};
    size_t received = 0;
    uhd_rx_streamer_recv(streamer, buffs, to_recv, &md, 3.0, false, &received);

    /* De-interleave */
    for (size_t i = 0; i < received; i++) {
        out_i[i] = iq_buf[i * 2];
        out_q[i] = iq_buf[i * 2 + 1];
    }

    return (int)received;
}

void uhd_rx_cleanup(uhd_rx_state_t *state)
{
    if (!state) return;
    if (state->rx_streamer) {
        uhd_stream_cmd_t cmd = {
            .stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS,
            .stream_now = true,
        };
        uhd_rx_streamer_issue_stream_cmd(
            (uhd_rx_streamer_handle)state->rx_streamer, &cmd);
        uhd_rx_streamer_free((uhd_rx_streamer_handle *)&state->rx_streamer);
    }
    if (state->rx_metadata)
        uhd_rx_metadata_free((uhd_rx_metadata_handle *)&state->rx_metadata);
    if (state->usrp)
        uhd_usrp_free((uhd_usrp_handle *)&state->usrp);
    state->initialized = 0;
}

#endif /* HAVE_UHD */
