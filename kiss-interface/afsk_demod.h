/*
 * afsk_demod.h — AFSK demodulation, clock recovery, and frame detection
 *
 * Correlation-based Bell 202 tone detection, zero-crossing clock recovery,
 * flag detection state machine, dual polarity handling.
 * Reuses nrzi_decode, nrzi_bit_destuff, nrzi_compute_fcs from nrzi.c.
 */

#ifndef AFSK_DEMOD_H
#define AFSK_DEMOD_H

#include <stdint.h>
#include <stddef.h>

#define AFSK_DEMOD_SAMPLE_RATE   48000
#define AFSK_DEMOD_BAUD_RATE     1200
#define AFSK_DEMOD_SAMPLES_PER_BIT (AFSK_DEMOD_SAMPLE_RATE / AFSK_DEMOD_BAUD_RATE)
#define AFSK_DEMOD_MARK_FREQ     1200
#define AFSK_DEMOD_SPACE_FREQ    2200

#define AFSK_DEMOD_MAX_FRAME_BITS 4096
#define AFSK_DEMOD_MAX_FRAME_BYTES 330
#define AFSK_DEMOD_FLAG 0x7E

typedef enum {
    AFSK_DEMOD_HUNT,
    AFSK_DEMOD_SYNC,
    AFSK_DEMOD_FRAME
} afsk_demod_fsm_t;

typedef void (*afsk_demod_frame_cb)(const uint8_t *frame, size_t frame_len,
                                    int polarity, void *user_data);

typedef struct {
    /* Pre-computed reference waveforms */
    float mark_sin[AFSK_DEMOD_SAMPLES_PER_BIT];
    float mark_cos[AFSK_DEMOD_SAMPLES_PER_BIT];
    float space_sin[AFSK_DEMOD_SAMPLES_PER_BIT];
    float space_cos[AFSK_DEMOD_SAMPLES_PER_BIT];

    /* Clock recovery */
    float phase;
    float prev_corr_diff;

    /* Audio buffer for partial bit periods across chunks */
    float audio_buf[AFSK_DEMOD_SAMPLES_PER_BIT * 2];
    int   audio_buf_len;

    /* Frame detector state (one per polarity: [0]=normal, [1]=inverted) */
    afsk_demod_fsm_t det_state[2];
    uint8_t frame_bits[2][AFSK_DEMOD_MAX_FRAME_BITS];
    size_t  frame_bit_count[2];
    uint8_t shift_reg[2];
    int     prev_nrzi[2];

    /* Callback */
    afsk_demod_frame_cb callback;
    void *user_data;

    int frames_decoded;
    int verbose;
} afsk_demod_t;

void afsk_demod_init(afsk_demod_t *d, afsk_demod_frame_cb cb,
                     void *user_data, int verbose);

int afsk_demod_process(afsk_demod_t *d,
                       const float *audio, size_t num_samples);

int afsk_demod_correlate(const afsk_demod_t *d,
                         const float *samples, size_t num_samples,
                         float *corr_diff);

int afsk_demod_feed_bit(afsk_demod_t *d, uint8_t nrzi_bit, int polarity);

void afsk_demod_reset(afsk_demod_t *d);

#endif
