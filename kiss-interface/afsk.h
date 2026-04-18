/*
 * afsk.h — Bell 202 AFSK modulator for 1200 baud AX.25
 *
 * Generates continuous-phase audio using a phase accumulator.
 * Tone mapping is inverted for correct demodulation through FM chain:
 *   NRZI state 1 -> 2200 Hz (space tone)
 *   NRZI state 0 -> 1200 Hz (mark tone)
 * See gnuradio/LESSONS-LEARNED.md item 4.
 */

#ifndef AFSK_H
#define AFSK_H

#include <stdint.h>
#include <stddef.h>

#define AFSK_MARK_FREQ    1200   /* Hz — Bell 202 mark */
#define AFSK_SPACE_FREQ   2200   /* Hz — Bell 202 space */
#define AFSK_BAUD_RATE    1200   /* symbols per second */
#define AFSK_SAMPLE_RATE  48000  /* Hz */
#define AFSK_SAMPLES_PER_BIT (AFSK_SAMPLE_RATE / AFSK_BAUD_RATE)  /* 40 */

/* Maximum AFSK audio samples: NRZI_MAX_BITS * 40 samples/bit */
#define AFSK_MAX_SAMPLES  (4096 * 40)  /* 163840 */

/* Generate Bell 202 AFSK audio from NRZI-encoded bitstream.
 * Tone mapping (inverted for FM chain): NRZI 1 -> 2200 Hz, NRZI 0 -> 1200 Hz.
 * Uses continuous-phase accumulator.
 * out must hold at least num_bits * AFSK_SAMPLES_PER_BIT floats.
 * Returns number of samples written, or -1 on error. */
int afsk_modulate(const uint8_t *nrzi_bits, size_t num_bits,
                  float *out, size_t out_size);

#endif
