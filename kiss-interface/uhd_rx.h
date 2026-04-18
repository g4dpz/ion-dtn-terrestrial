/*
 * uhd_rx.h — UHD SDR receive, FM demodulation, and decimation
 *
 * Receives IQ samples from B200 mini, FM-demodulates via atan2
 * discriminator, and decimates from SDR rate to audio rate.
 * DSP functions are always compiled; UHD API calls guarded by HAVE_UHD.
 */

#ifndef UHD_RX_H
#define UHD_RX_H

#include <stdint.h>
#include <stddef.h>

#define UHD_RX_DEFAULT_FREQ        144800000.0
#define UHD_RX_DEFAULT_GAIN        50
#define UHD_RX_DEFAULT_SAMPLE_RATE 480000
#define UHD_RX_AUDIO_RATE          48000
#define UHD_RX_DEFAULT_DEVIATION   3000.0

#define UHD_RX_BAND_LOW            144000000.0
#define UHD_RX_BAND_HIGH           146000000.0
#define UHD_RX_GAIN_MIN            0
#define UHD_RX_GAIN_MAX            76

#define UHD_RX_CHUNK_SAMPLES       4800
#define UHD_RX_DECIM_RATIO         10
#define UHD_RX_AUDIO_CHUNK         (UHD_RX_CHUNK_SAMPLES / UHD_RX_DECIM_RATIO)

typedef struct {
    double freq_hz;
    int    gain;
    int    sdr_sample_rate;
    double deviation;
    double sensitivity;     /* 2*pi*deviation/sdr_rate */
    int    decim_ratio;
    int    verbose;
    void  *usrp;
    void  *rx_streamer;
    void  *rx_metadata;
    int    initialized;
    float  prev_i;
    float  prev_q;
    float  lpf_buf[UHD_RX_DECIM_RATIO];
    int    lpf_idx;
    float  lpf_sum;
    int    lpf_count;       /* samples fed since last reset */
} uhd_rx_state_t;

/* Validate RX parameters. Returns 0 if valid, -1 if invalid. */
int uhd_rx_validate(double freq_hz, int gain, int sdr_sample_rate);

/* FM demodulate: IQ pairs -> real-valued audio.
 * atan2(cross, dot) discriminator, normalised by sensitivity.
 * Maintains prev_i/prev_q state across calls.
 * out must hold at least num_samples floats.
 * Returns number of audio samples written. */
int uhd_rx_fm_demod(uhd_rx_state_t *state,
                    const float *iq_i, const float *iq_q,
                    size_t num_samples, float *out);

/* Low-pass filter and decimate by decim_ratio.
 * Moving-average filter of length decim_ratio.
 * Maintains filter state across calls.
 * num_samples must be a multiple of decim_ratio.
 * out must hold at least num_samples/decim_ratio floats.
 * Returns number of decimated samples written. */
int uhd_rx_decimate(uhd_rx_state_t *state,
                    const float *audio, size_t num_samples,
                    float *out, size_t out_size);

/* Initialise RX state for DSP (no UHD). */
void uhd_rx_init_dsp(uhd_rx_state_t *state, double deviation,
                     int sdr_sample_rate);

#ifdef HAVE_UHD

int uhd_rx_init(uhd_rx_state_t *state, double freq_hz, int gain,
                int sdr_sample_rate, double deviation, int verbose);

int uhd_rx_receive(uhd_rx_state_t *state,
                   float *out_i, float *out_q, size_t max_samples);

void uhd_rx_cleanup(uhd_rx_state_t *state);

#endif /* HAVE_UHD */

#endif /* UHD_RX_H */
