/*
 * uhd_tx.h — FM modulation, resampling, and UHD SDR transmission
 *
 * Converts AFSK audio to FM-modulated IQ, resamples to SDR rate,
 * and streams to Ettus B200 mini via UHD C API.
 * Guarded by HAVE_UHD for conditional compilation.
 */

#ifndef UHD_TX_H
#define UHD_TX_H

#include <stdint.h>
#include <stddef.h>

/* Default SDR parameters */
#define UHD_TX_DEFAULT_FREQ        144850000.0  /* 144.850 MHz */
#define UHD_TX_DEFAULT_GAIN        50
#define UHD_TX_DEFAULT_SAMPLE_RATE 480000       /* Hz */
#define UHD_TX_DEFAULT_DEVIATION   3000.0       /* Hz */
#define UHD_TX_AUDIO_RATE          48000        /* Hz */
#define UHD_TX_SILENCE_SECONDS     1            /* seconds of silence padding */

/* Band limits (2-metre amateur band) */
#define UHD_TX_BAND_LOW            144000000.0  /* 144.000 MHz */
#define UHD_TX_BAND_HIGH           146000000.0  /* 146.000 MHz */

/* Gain limits */
#define UHD_TX_GAIN_MIN            0
#define UHD_TX_GAIN_MAX            89

/* Deviation limits */
#define UHD_TX_DEVIATION_MIN       1000.0
#define UHD_TX_DEVIATION_MAX       5000.0

/* Max IQ samples: (silence + max_audio + silence) * resample_ratio */
/* Worst case: (48000 + 163840 + 48000) * 10 = 2,598,400 complex pairs */
#define UHD_TX_MAX_IQ_SAMPLES      2600000

/* SDR transmitter state */
typedef struct {
    double freq_hz;
    int    gain;
    int    sdr_sample_rate;
    double deviation;
    double sensitivity;     /* 2*pi*deviation/audio_rate */
    int    resample_ratio;  /* sdr_sample_rate / audio_rate */
    int    verbose;
    void  *usrp;            /* Opaque UHD handle */
    void  *tx_streamer;     /* Opaque UHD TX streamer */
    void  *tx_metadata;     /* Opaque UHD TX metadata */
    int    initialized;
} uhd_tx_state_t;

/* Validate SDR parameters. Returns 0 if valid, -1 if invalid (prints error). */
int uhd_tx_validate(double freq_hz, int gain, int sdr_sample_rate,
                    double deviation);

/* FM-modulate audio samples to complex IQ.
 * sensitivity = 2*pi*deviation/audio_rate.
 * out_i and out_q must hold at least num_samples floats.
 * Returns number of IQ pairs written. */
int uhd_tx_fm_modulate(const float *audio, size_t num_samples,
                       double sensitivity,
                       float *out_i, float *out_q);

/* Zero-order hold resample complex IQ by integer ratio.
 * out_i and out_q must hold at least num_samples * ratio floats.
 * Returns number of output IQ pairs. */
int uhd_tx_resample_zoh(const float *in_i, const float *in_q,
                        size_t num_samples, int ratio,
                        float *out_i, float *out_q, size_t out_size);

#ifdef HAVE_UHD

/* Initialise UHD transmitter: open device, set frequency/gain/rate.
 * Returns 0 on success, -1 on error. */
int uhd_tx_init(uhd_tx_state_t *state, double freq_hz, int gain,
                int sdr_sample_rate, double deviation, int verbose);

/* Transmit AFSK audio as FM-modulated IQ via UHD.
 * Applies silence padding, FM modulation, and resampling internally.
 * Returns 0 on success, -1 on error. */
int uhd_tx_transmit(uhd_tx_state_t *state,
                    const float *audio, size_t num_samples);

/* Release UHD hardware and clean up. */
void uhd_tx_cleanup(uhd_tx_state_t *state);

#endif /* HAVE_UHD */

#endif /* UHD_TX_H */
