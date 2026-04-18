"""Integration tests for the full APRS beacon pipeline (without hardware).

Tests the complete chain: AX.25 frame -> NRZI -> AFSK audio.
Feature: gnuradio-aprs-beacon
"""
import sys
import os
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from ax25_frame import build_frame
from nrzi import encode as nrzi_encode
from afsk_mod import modulate, DEFAULT_SAMPLE_RATE, BAUD_RATE


def test_full_pipeline_produces_audio():
    """Frame construction -> NRZI -> AFSK produces valid audio samples."""
    bitstream = build_frame("G4DPZ-1", 52.467, -2.022,
                            "github.com/g4dpz/ion-dtn-terrestrial")
    nrzi_bits = nrzi_encode(bitstream)
    audio = modulate(nrzi_bits)

    # Should produce audio
    assert len(audio) > 0
    assert audio.dtype == np.float32

    # Expected length: num_bits * samples_per_bit
    spb = DEFAULT_SAMPLE_RATE // BAUD_RATE
    assert len(audio) == len(nrzi_bits) * spb

    # Audio should be in [-1, 1] range
    assert np.all(audio >= -1.1)  # small tolerance for float
    assert np.all(audio <= 1.1)

    # Should have non-trivial content (not all zeros)
    assert np.max(np.abs(audio)) > 0.5


def test_pipeline_different_callsigns():
    """Different callsigns produce different audio."""
    audio1 = modulate(nrzi_encode(build_frame("G4DPZ-1", 52.467, -2.022)))
    audio2 = modulate(nrzi_encode(build_frame("M0ABC-5", 51.5, -0.1)))

    # Different frames should produce different audio
    assert len(audio1) != len(audio2) or not np.array_equal(audio1, audio2)


def test_pipeline_frame_duration():
    """Beacon frame should be a reasonable duration for 1200 baud."""
    bitstream = build_frame("G4DPZ-1", 52.467, -2.022, "test")
    nrzi_bits = nrzi_encode(bitstream)
    audio = modulate(nrzi_bits)

    duration_ms = len(audio) / DEFAULT_SAMPLE_RATE * 1000

    # A typical APRS beacon frame at 1200 baud:
    # ~25 flags preamble (200 bits) + ~50 bytes frame (~400 bits + stuffing)
    # + 1 closing flag (8 bits) = ~600-700 bits = ~500-600ms
    assert 200 < duration_ms < 2000, f"unexpected duration: {duration_ms:.0f}ms"
