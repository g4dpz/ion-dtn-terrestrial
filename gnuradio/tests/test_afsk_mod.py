"""Tests for Bell 202 AFSK modulator.

Feature: gnuradio-aprs-beacon
"""
import sys
import os
import numpy as np
import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from afsk_mod import modulate, MARK_FREQ, SPACE_FREQ, BAUD_RATE, DEFAULT_SAMPLE_RATE


def _dominant_freq(samples, sample_rate):
    """Estimate dominant frequency via FFT peak."""
    n = len(samples)
    if n == 0:
        return 0
    fft = np.abs(np.fft.rfft(samples))
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)
    # Skip DC component
    peak_idx = np.argmax(fft[1:]) + 1
    return freqs[peak_idx]


# ===== Property 7: AFSK modulation correctness =====

@given(bits=st.lists(st.integers(min_value=0, max_value=1), min_size=10, max_size=100))
@settings(max_examples=200)
def test_afsk_modulation_correctness(bits):
    """Property 7: output length correct, dominant freq within tolerance."""
    sr = DEFAULT_SAMPLE_RATE
    spb = sr // BAUD_RATE  # 40

    audio = modulate(bits, sr)

    # Correct total length
    assert len(audio) == len(bits) * spb

    # Check frequency over runs of same bit value (better FFT resolution)
    # Group consecutive identical bits
    i = 0
    while i < len(bits):
        bit = bits[i]
        run_start = i
        while i < len(bits) and bits[i] == bit:
            i += 1
        run_len = i - run_start
        if run_len >= 3:  # need at least 3 bits for reasonable FFT
            segment = audio[run_start * spb:i * spb]
            expected_freq = MARK_FREQ if bit == 1 else SPACE_FREQ
            measured = _dominant_freq(segment, sr)
            # FFT bin resolution = sr / num_samples
            bin_res = sr / len(segment)
            assert abs(measured - expected_freq) < bin_res + 50, \
                f"run at {run_start}: expected ~{expected_freq}Hz, got {measured:.0f}Hz"

    # Verify float32 dtype
    assert audio.dtype == np.float32


# ===== Unit tests =====

def test_modulate_single_mark_bit():
    """Mark bits should produce ~1200 Hz tone (use 10 bits for FFT resolution)."""
    audio = modulate([1] * 10, DEFAULT_SAMPLE_RATE)
    freq = _dominant_freq(audio, DEFAULT_SAMPLE_RATE)
    assert abs(freq - MARK_FREQ) < MARK_FREQ * 0.05


def test_modulate_single_space_bit():
    """Space bits should produce ~2200 Hz tone (use 10 bits for FFT resolution)."""
    audio = modulate([0] * 10, DEFAULT_SAMPLE_RATE)
    freq = _dominant_freq(audio, DEFAULT_SAMPLE_RATE)
    assert abs(freq - SPACE_FREQ) < SPACE_FREQ * 0.05


def test_modulate_output_length():
    """Output length = num_bits * samples_per_bit."""
    for n in [1, 10, 50, 100]:
        bits = [1] * n
        audio = modulate(bits, DEFAULT_SAMPLE_RATE)
        assert len(audio) == n * (DEFAULT_SAMPLE_RATE // BAUD_RATE)


def test_modulate_rejects_bad_sample_rate():
    """Non-multiple of 1200 raises ValueError."""
    with pytest.raises(ValueError):
        modulate([1], 44100)  # not a multiple of 1200


def test_modulate_empty():
    """Empty input produces empty output."""
    audio = modulate([], DEFAULT_SAMPLE_RATE)
    assert len(audio) == 0


def test_modulate_continuous_phase():
    """Phase should be continuous at bit boundaries (mark→space transition)."""
    # Mark then space: the waveform at the boundary should not jump
    audio = modulate([1, 0], DEFAULT_SAMPLE_RATE)
    spb = DEFAULT_SAMPLE_RATE // BAUD_RATE
    # Check the sample at the boundary
    boundary_diff = abs(audio[spb] - audio[spb - 1])
    # Maximum expected diff for one sample of phase advance at 2200 Hz
    max_diff = abs(np.sin(2 * np.pi * SPACE_FREQ / DEFAULT_SAMPLE_RATE))
    assert boundary_diff < max_diff + 0.1  # small tolerance
