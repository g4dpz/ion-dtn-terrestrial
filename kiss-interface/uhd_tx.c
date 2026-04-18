/*
 * uhd_tx.c — FM modulation, resampling, and UHD SDR transmission
 *
 * Signal chain: audio -> FM modulate -> ZOH resample -> UHD stream
 * Direct FM modulation (not NBFM with pre-emphasis).
 * See gnuradio/LESSONS-LEARNED.md items 5, 6.
 */

#include "uhd_tx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* uhd_tx_validate — check parameter ranges                            */
/* ------------------------------------------------------------------ */
int uhd_tx_validate(double freq_hz, int gain, int sdr_sample_rate,
                    double deviation)
{
    int ok = 1;
    if (freq_hz < UHD_TX_BAND_LOW || freq_hz > UHD_TX_BAND_HIGH) {
        fprintf(stderr, "error: frequency %.3f MHz outside 2m band "
                "(%.3f-%.3f MHz)\n",
                freq_hz / 1e6, UHD_TX_BAND_LOW / 1e6, UHD_TX_BAND_HIGH / 1e6);
        ok = 0;
    }
    if (gain < UHD_TX_GAIN_MIN || gain > UHD_TX_GAIN_MAX) {
        fprintf(stderr, "error: gain %d outside range %d-%d\n",
                gain, UHD_TX_GAIN_MIN, UHD_TX_GAIN_MAX);
        ok = 0;
    }
    if (deviation < UHD_TX_DEVIATION_MIN || deviation > UHD_TX_DEVIATION_MAX) {
        fprintf(stderr, "error: deviation %.0f Hz outside range %.0f-%.0f\n",
                deviation, UHD_TX_DEVIATION_MIN, UHD_TX_DEVIATION_MAX);
        ok = 0;
    }
    if (sdr_sample_rate % UHD_TX_AUDIO_RATE != 0) {
        fprintf(stderr, "error: sample rate %d not integer multiple of %d\n",
                sdr_sample_rate, UHD_TX_AUDIO_RATE);
        ok = 0;
    }
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* uhd_tx_fm_modulate — direct FM: audio -> complex IQ                 */
/* sensitivity = 2*pi*deviation/audio_rate                             */
/* ------------------------------------------------------------------ */
int uhd_tx_fm_modulate(const float *audio, size_t num_samples,
                       double sensitivity,
                       float *out_i, float *out_q)
{
    double phase = 0.0;
    for (size_t i = 0; i < num_samples; i++) {
        out_i[i] = (float)cos(phase);
        out_q[i] = (float)sin(phase);
        phase += sensitivity * (double)audio[i];
        /* Wrap phase */
        if (phase > M_PI)
            phase -= 2.0 * M_PI;
        else if (phase < -M_PI)
            phase += 2.0 * M_PI;
    }
    return (int)num_samples;
}

/* ------------------------------------------------------------------ */
/* uhd_tx_resample_zoh — zero-order hold integer interpolation         */
/* ------------------------------------------------------------------ */
int uhd_tx_resample_zoh(const float *in_i, const float *in_q,
                        size_t num_samples, int ratio,
                        float *out_i, float *out_q, size_t out_size)
{
    size_t total = num_samples * (size_t)ratio;
    if (total > out_size) return -1;

    size_t idx = 0;
    for (size_t i = 0; i < num_samples; i++) {
        for (int r = 0; r < ratio; r++) {
            out_i[idx] = in_i[i];
            out_q[idx] = in_q[i];
            idx++;
        }
    }
    return (int)idx;
}

/* ================================================================== */
/* UHD C API functions — only compiled when HAVE_UHD is defined        */
/* ================================================================== */
#ifdef HAVE_UHD

#include <uhd.h>

int uhd_tx_init(uhd_tx_state_t *state, double freq_hz, int gain,
                int sdr_sample_rate, double deviation, int verbose)
{
    memset(state, 0, sizeof(*state));
    state->freq_hz = freq_hz;
    state->gain = gain;
    state->sdr_sample_rate = sdr_sample_rate;
    state->deviation = deviation;
    state->sensitivity = 2.0 * M_PI * deviation / (double)UHD_TX_AUDIO_RATE;
    state->resample_ratio = sdr_sample_rate / UHD_TX_AUDIO_RATE;
    state->verbose = verbose;

    if (uhd_tx_validate(freq_hz, gain, sdr_sample_rate, deviation) != 0)
        return -1;

    /* Create USRP handle */
    uhd_usrp_handle usrp;
    if (uhd_usrp_make(&usrp, "") != UHD_ERROR_NONE) {
        fprintf(stderr, "error: B200 mini not detected\n");
        return -1;
    }
    state->usrp = usrp;

    /* Configure */
    uhd_usrp_set_tx_rate(usrp, (double)sdr_sample_rate, 0);
    uhd_tune_request_t tune = {
        .target_freq = freq_hz,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t result;
    uhd_usrp_set_tx_freq(usrp, &tune, 0, &result);
    uhd_usrp_set_tx_gain(usrp, (double)gain, 0, "");

    /* Create TX streamer */
    uhd_tx_streamer_handle streamer;
    uhd_tx_streamer_make(&streamer);
    uhd_stream_args_t stream_args = {
        .cpu_format = "fc32",
        .otw_format = "sc16",
        .args = "",
        .channel_list = (size_t[]){0},
        .n_channels = 1,
    };
    uhd_usrp_get_tx_stream(usrp, &stream_args, streamer);
    state->tx_streamer = streamer;

    /* Create TX metadata */
    uhd_tx_metadata_handle md;
    uhd_tx_metadata_make(&md, false, 0, 0, false, false);
    state->tx_metadata = md;

    state->initialized = 1;

    if (verbose) {
        double actual_rate;
        uhd_usrp_get_tx_rate(usrp, 0, &actual_rate);
        printf("UHD TX: freq=%.3f MHz, gain=%d, rate=%.0f Hz\n",
               freq_hz / 1e6, gain, actual_rate);
    }

    return 0;
}

int uhd_tx_transmit(uhd_tx_state_t *state,
                    const float *audio, size_t num_samples)
{
    if (!state || !state->initialized) return -1;

    /* Add silence padding */
    size_t silence = UHD_TX_SILENCE_SECONDS * UHD_TX_AUDIO_RATE;
    size_t padded_len = silence + num_samples + silence;

    /* FM modulate at audio rate */
    static float pad_audio[300000];  /* silence + audio + silence */
    if (padded_len > sizeof(pad_audio) / sizeof(pad_audio[0])) return -1;

    memset(pad_audio, 0, silence * sizeof(float));
    memcpy(pad_audio + silence, audio, num_samples * sizeof(float));
    memset(pad_audio + silence + num_samples, 0, silence * sizeof(float));

    static float iq_i[300000], iq_q[300000];
    uhd_tx_fm_modulate(pad_audio, padded_len, state->sensitivity, iq_i, iq_q);

    /* Resample to SDR rate */
    static float rs_i[UHD_TX_MAX_IQ_SAMPLES], rs_q[UHD_TX_MAX_IQ_SAMPLES];
    int rs_n = uhd_tx_resample_zoh(iq_i, iq_q, padded_len,
                                    state->resample_ratio,
                                    rs_i, rs_q, UHD_TX_MAX_IQ_SAMPLES);
    if (rs_n < 0) return -1;

    /* Interleave I/Q for UHD (fc32 = float complex = I,Q,I,Q,...) */
    static float iq_interleaved[UHD_TX_MAX_IQ_SAMPLES * 2];
    for (int i = 0; i < rs_n; i++) {
        iq_interleaved[i * 2]     = rs_i[i];
        iq_interleaved[i * 2 + 1] = rs_q[i];
    }

    /* Stream to UHD */
    uhd_tx_streamer_handle streamer = (uhd_tx_streamer_handle)state->tx_streamer;
    uhd_tx_metadata_handle md = (uhd_tx_metadata_handle)state->tx_metadata;
    size_t remaining = (size_t)rs_n;
    size_t offset = 0;
    size_t samples_sent = 0;

    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > 10000) chunk = 10000;
        const void *chunk_buffs[] = {iq_interleaved + offset * 2};
        size_t sent = 0;
        uhd_tx_streamer_send(streamer, chunk_buffs, chunk, &md, 3.0, &sent);
        if (sent == 0) break;
        offset += sent;
        remaining -= sent;
        samples_sent += sent;
    }

    if (state->verbose)
        printf("  UHD TX: %zu IQ samples streamed\n", samples_sent);

    return 0;
}

void uhd_tx_cleanup(uhd_tx_state_t *state)
{
    if (!state) return;
    if (state->tx_metadata)
        uhd_tx_metadata_free((uhd_tx_metadata_handle *)&state->tx_metadata);
    if (state->tx_streamer)
        uhd_tx_streamer_free((uhd_tx_streamer_handle *)&state->tx_streamer);
    if (state->usrp)
        uhd_usrp_free((uhd_usrp_handle *)&state->usrp);
    state->initialized = 0;
}

#endif /* HAVE_UHD */
