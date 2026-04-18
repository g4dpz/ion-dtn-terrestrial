"""Tests for NRZI encoding/decoding.

Feature: gnuradio-aprs-beacon
"""
import sys
import os
from hypothesis import given, settings
from hypothesis import strategies as st

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from nrzi import encode, decode


# ===== Property 6: NRZI encoding round-trip =====

@given(bits=st.lists(st.integers(min_value=0, max_value=1), min_size=0, max_size=500))
@settings(max_examples=200)
def test_nrzi_roundtrip(bits):
    """Property 6: encode then decode recovers original bitstream."""
    encoded = encode(bits)
    recovered = decode(encoded)
    assert recovered == bits


# ===== Unit tests =====

def test_nrzi_encode_all_zeros():
    """All zeros = alternating output (toggle every bit)."""
    result = encode([0, 0, 0, 0])
    # Start space(0), toggle: 1, 0, 1, 0
    assert result == [1, 0, 1, 0]


def test_nrzi_encode_all_ones():
    """All ones = constant output (no transitions)."""
    result = encode([1, 1, 1, 1])
    # Start space(0), no change: 0, 0, 0, 0
    assert result == [0, 0, 0, 0]


def test_nrzi_initial_state_mark():
    """First output is space (0) when first bit is 1 (no transition from space)."""
    result = encode([1])
    assert result == [0]

    # First bit 0 = toggle from space to mark
    result = encode([0])
    assert result == [1]


def test_nrzi_empty():
    """Empty input produces empty output."""
    assert encode([]) == []
    assert decode([]) == []
