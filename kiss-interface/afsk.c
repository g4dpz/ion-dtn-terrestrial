/*
 * afsk.c — Bell 202 AFSK modulator for 1200 baud AX.25
 *
 * Continuous-phase synthesis using a phase accumulator.
 * Inverted tone mapping for correct demodulation through FM:
 *   NRZI 1 -> 2200 Hz, NRZI 0 -> 1200 Hz
 */

#include "afsk.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int afsk_modulate(const uint8_t *nrzi_bits, size_t num_bits,
                  float *out, size_t out_size)
{
    size_t total_samples = num_bits * AFSK_SAMPLES_PER_BIT;
    if (total_samples > out_size)
        return -1;
    if (num_bits == 0)
        return 0;

    double phase = 0.0;
    size_t idx = 0;

    for (size_t i = 0; i < num_bits; i++) {
        /* Inverted mapping: NRZI 1 -> space (2200), NRZI 0 -> mark (1200) */
        double freq = (nrzi_bits[i] == 1)
                      ? (double)AFSK_SPACE_FREQ
                      : (double)AFSK_MARK_FREQ;
        double delta = 2.0 * M_PI * freq / (double)AFSK_SAMPLE_RATE;

        for (int s = 0; s < AFSK_SAMPLES_PER_BIT; s++) {
            out[idx++] = (float)sin(phase);
            phase += delta;
        }

        /* Wrap phase to prevent precision loss */
        if (phase > 2.0 * M_PI)
            phase -= 2.0 * M_PI * floor(phase / (2.0 * M_PI));
    }

    return (int)total_samples;
}
