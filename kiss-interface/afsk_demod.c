/*
 * afsk_demod.c — AFSK demodulation, clock recovery, and frame detection
 */

#include "afsk_demod.h"
#include "nrzi.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPB AFSK_DEMOD_SAMPLES_PER_BIT

/* ------------------------------------------------------------------ */
/* afsk_demod_init                                                     */
/* ------------------------------------------------------------------ */
void afsk_demod_init(afsk_demod_t *d, afsk_demod_frame_cb cb,
                     void *user_data, int verbose)
{
    memset(d, 0, sizeof(*d));
    d->callback = cb;
    d->user_data = user_data;
    d->verbose = verbose;

    /* Pre-compute reference waveforms */
    for (int n = 0; n < SPB; n++) {
        double t = (double)n / (double)AFSK_DEMOD_SAMPLE_RATE;
        d->mark_sin[n]  = (float)sin(2.0 * M_PI * AFSK_DEMOD_MARK_FREQ * t);
        d->mark_cos[n]  = (float)cos(2.0 * M_PI * AFSK_DEMOD_MARK_FREQ * t);
        d->space_sin[n] = (float)sin(2.0 * M_PI * AFSK_DEMOD_SPACE_FREQ * t);
        d->space_cos[n] = (float)cos(2.0 * M_PI * AFSK_DEMOD_SPACE_FREQ * t);
    }

    for (int p = 0; p < 2; p++) {
        d->det_state[p] = AFSK_DEMOD_HUNT;
        d->prev_nrzi[p] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* afsk_demod_correlate — detect mark or space in one bit period       */
/* ------------------------------------------------------------------ */
int afsk_demod_correlate(const afsk_demod_t *d,
                         const float *samples, size_t num_samples,
                         float *corr_diff)
{
    size_t len = (num_samples < (size_t)SPB) ? num_samples : (size_t)SPB;

    float ms = 0, mc = 0, ss = 0, sc = 0;
    for (size_t i = 0; i < len; i++) {
        float s = samples[i];
        ms += s * d->mark_sin[i];
        mc += s * d->mark_cos[i];
        ss += s * d->space_sin[i];
        sc += s * d->space_cos[i];
    }

    float mark_mag  = sqrtf(ms * ms + mc * mc);
    float space_mag = sqrtf(ss * ss + sc * sc);

    if (corr_diff)
        *corr_diff = mark_mag - space_mag;

    return (mark_mag >= space_mag) ? 1 : 0;  /* 1=mark, 0=space */
}

/* ------------------------------------------------------------------ */
/* afsk_demod_feed_bit — process one NRZI bit through frame detector   */
/* ------------------------------------------------------------------ */
int afsk_demod_feed_bit(afsk_demod_t *d, uint8_t nrzi_bit, int polarity)
{
    int p = polarity ? 1 : 0;

    /* NRZI decode: transition=0, no transition=1 */
    uint8_t data_bit = (nrzi_bit == d->prev_nrzi[p]) ? 1 : 0;
    d->prev_nrzi[p] = nrzi_bit;

    /* Shift register for flag detection */
    d->shift_reg[p] = (d->shift_reg[p] >> 1) | (data_bit << 7);

    switch (d->det_state[p]) {
    case AFSK_DEMOD_HUNT:
        if (d->shift_reg[p] == AFSK_DEMOD_FLAG) {
            d->det_state[p] = AFSK_DEMOD_SYNC;
            d->frame_bit_count[p] = 0;
        }
        break;

    case AFSK_DEMOD_SYNC:
        if (d->shift_reg[p] == AFSK_DEMOD_FLAG) {
            /* Another flag — still in preamble */
            d->frame_bit_count[p] = 0;
        } else {
            /* Non-flag data — transition to FRAME */
            d->det_state[p] = AFSK_DEMOD_FRAME;
            d->frame_bit_count[p] = 0;
            /* Accumulate just this bit (the shift register contains
             * partial flag bits that we don't want) */
            d->frame_bits[p][d->frame_bit_count[p]++] = data_bit;
        }
        break;

    case AFSK_DEMOD_FRAME:
        if (d->shift_reg[p] == AFSK_DEMOD_FLAG) {
            /* Closing flag — process accumulated frame */
            /* Remove the last 7 bits (they're part of the flag) */
            if (d->frame_bit_count[p] >= 7)
                d->frame_bit_count[p] -= 7;

            /* De-stuff bits */
            uint8_t destuffed[AFSK_DEMOD_MAX_FRAME_BITS];
            int dlen = nrzi_bit_destuff(d->frame_bits[p],
                                         d->frame_bit_count[p],
                                         destuffed,
                                         AFSK_DEMOD_MAX_FRAME_BITS);
            if (dlen < 0 || dlen < 16 * 8) {
                /* Too short or destuff error */
                d->det_state[p] = AFSK_DEMOD_SYNC;
                d->frame_bit_count[p] = 0;
                break;
            }

            /* Convert bits to bytes (LSB first) */
            int num_bytes = dlen / 8;
            uint8_t frame[AFSK_DEMOD_MAX_FRAME_BYTES];
            for (int i = 0; i < num_bytes && i < AFSK_DEMOD_MAX_FRAME_BYTES; i++) {
                frame[i] = 0;
                for (int j = 0; j < 8; j++)
                    frame[i] |= (uint8_t)(destuffed[i * 8 + j] << j);
            }

            /* Verify FCS */
            if (num_bytes >= 2) {
                uint16_t rx_fcs = (uint16_t)frame[num_bytes - 2] |
                                  ((uint16_t)frame[num_bytes - 1] << 8);
                uint16_t calc_fcs = nrzi_compute_fcs(frame, (size_t)(num_bytes - 2));
                if (rx_fcs == calc_fcs && num_bytes - 2 >= 16) {
                    /* Valid frame! */
                    if (d->callback)
                        d->callback(frame, (size_t)(num_bytes - 2), p, d->user_data);
                    d->frames_decoded++;
                }
            }

            d->det_state[p] = AFSK_DEMOD_SYNC;
            d->frame_bit_count[p] = 0;
        } else {
            /* Accumulate frame bit */
            if (d->frame_bit_count[p] < AFSK_DEMOD_MAX_FRAME_BITS) {
                d->frame_bits[p][d->frame_bit_count[p]++] = data_bit;
            } else {
                /* Overflow — abort */
                d->det_state[p] = AFSK_DEMOD_HUNT;
                d->frame_bit_count[p] = 0;
            }

            /* Check for 7+ consecutive ones (invalid) */
            int ones = 0;
            for (int i = 0; i < 8; i++) {
                if ((d->shift_reg[p] >> i) & 1) ones++;
                else break;
            }
            if (ones >= 7) {
                d->det_state[p] = AFSK_DEMOD_HUNT;
                d->frame_bit_count[p] = 0;
            }
        }
        break;
    }

    return (d->det_state[p] == AFSK_DEMOD_HUNT) ? 0 : 0;
}

/* ------------------------------------------------------------------ */
/* afsk_demod_process — process audio chunk, detect frames             */
/* ------------------------------------------------------------------ */
int afsk_demod_process(afsk_demod_t *d,
                       const float *audio, size_t num_samples)
{
    int frames_found = 0;
    int prev_decoded = d->frames_decoded;
    size_t pos = 0;

    while (pos + SPB <= num_samples) {
        float corr_diff;
        int nrzi_bit = afsk_demod_correlate(d, audio + pos, SPB, &corr_diff);

        /* Feed to both polarity detectors */
        afsk_demod_feed_bit(d, (uint8_t)nrzi_bit, 0);       /* normal */
        afsk_demod_feed_bit(d, (uint8_t)(1 - nrzi_bit), 1); /* inverted */

        pos += SPB;
    }

    frames_found = d->frames_decoded - prev_decoded;
    return frames_found;
}

/* ------------------------------------------------------------------ */
/* afsk_demod_reset                                                    */
/* ------------------------------------------------------------------ */
void afsk_demod_reset(afsk_demod_t *d)
{
    for (int p = 0; p < 2; p++) {
        d->det_state[p] = AFSK_DEMOD_HUNT;
        d->frame_bit_count[p] = 0;
        d->shift_reg[p] = 0;
        d->prev_nrzi[p] = 0;
    }
    d->phase = 0;
    d->prev_corr_diff = 0;
    d->audio_buf_len = 0;
}
