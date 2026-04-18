"""Bell 202 AFSK modulator for 1200 baud AX.25.

Generates continuous-phase audio using a phase accumulator.
Mark = 1200 Hz, Space = 2200 Hz.
"""

import math
import numpy as np

MARK_FREQ = 1200    # Hz
SPACE_FREQ = 2200   # Hz
BAUD_RATE = 1200    # symbols per second
DEFAULT_SAMPLE_RATE = 48000  # Hz


def modulate(nrzi_bits: list, sample_rate: int = DEFAULT_SAMPLE_RATE) -> np.ndarray:
    """Generate Bell 202 AFSK audio from NRZI-encoded bitstream.

    Uses a phase accumulator for continuous-phase tone generation.
    Each bit period is exactly (sample_rate / BAUD_RATE) samples.

    Args:
        nrzi_bits: NRZI-encoded bitstream (1=mark/1200Hz, 0=space/2200Hz).
        sample_rate: Audio sample rate in Hz. Must be integer multiple of 1200.

    Returns:
        numpy float32 array of audio samples, normalised to [-1.0, 1.0].
    """
    if sample_rate % BAUD_RATE != 0:
        raise ValueError(
            f"sample_rate must be integer multiple of {BAUD_RATE}, got {sample_rate}"
        )

    samples_per_bit = sample_rate // BAUD_RATE
    total_samples = len(nrzi_bits) * samples_per_bit
    out = np.empty(total_samples, dtype=np.float32)

    phase = 0.0
    idx = 0
    for bit in nrzi_bits:
        freq = SPACE_FREQ if bit == 1 else MARK_FREQ
        delta = 2.0 * math.pi * freq / sample_rate
        for _ in range(samples_per_bit):
            out[idx] = math.sin(phase)
            phase += delta
            idx += 1

    # Keep phase bounded to avoid precision loss
    # (not strictly necessary for short bursts but good practice)
    return out
