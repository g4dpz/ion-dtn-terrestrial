"""Tests for APRS beacon CLI and parameter validation.

Feature: gnuradio-aprs-beacon
"""
import sys
import os
import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from aprs_beacon import (
    parse_args, validate_freq, validate_gain, validate_interval,
    BAND_LOW_MHZ, BAND_HIGH_MHZ, MIN_INTERVAL, MAX_INTERVAL,
    DEFAULT_COMMENT, DEFAULT_INTERVAL, DEFAULT_FREQ_MHZ, DEFAULT_GAIN,
    DEFAULT_SAMPLE_RATE,
)


# ===== Property 8: Parameter range validation =====

@given(interval=st.integers(min_value=-100, max_value=5000))
@settings(max_examples=200)
def test_interval_range_validation(interval):
    """Property 8: interval accepted iff in [10, 3600]."""
    expected = MIN_INTERVAL <= interval <= MAX_INTERVAL
    assert validate_interval(interval) == expected


@given(freq=st.floats(min_value=100.0, max_value=200.0, allow_nan=False))
@settings(max_examples=200)
def test_freq_range_validation(freq):
    """Property 8: frequency accepted iff in [144.000, 146.000]."""
    expected = BAND_LOW_MHZ <= freq <= BAND_HIGH_MHZ
    assert validate_freq(freq) == expected


@given(gain=st.integers(min_value=-10, max_value=100))
@settings(max_examples=200)
def test_gain_range_validation(gain):
    """Property 8: gain accepted iff in [0, 89]."""
    expected = 0 <= gain <= 89
    assert validate_gain(gain) == expected


# ===== Unit tests =====

def test_parse_args_required_only():
    args = parse_args(["--callsign", "G4DPZ-1", "--lat", "52.467", "--lon", "-2.022"])
    assert args.callsign == "G4DPZ-1"
    assert args.lat == 52.467
    assert args.lon == -2.022


def test_parse_args_all_options():
    args = parse_args([
        "--callsign", "G4DPZ-1",
        "--lat", "52.467", "--lon", "-2.022",
        "--comment", "test", "--interval", "60",
        "--freq", "144.850", "--gain", "30",
        "--sample-rate", "960000",
    ])
    assert args.comment == "test"
    assert args.interval == 60
    assert args.freq == 144.850
    assert args.gain == 30
    assert args.sample_rate == 960000


def test_parse_args_missing_callsign():
    with pytest.raises(SystemExit) as exc:
        parse_args(["--lat", "52.467", "--lon", "-2.022"])
    assert exc.value.code == 2  # argparse exits with 2 for missing required


def test_parse_args_help():
    with pytest.raises(SystemExit) as exc:
        parse_args(["--help"])
    assert exc.value.code == 0


def test_parse_args_default_values():
    args = parse_args(["--callsign", "G4DPZ-1", "--lat", "52.467", "--lon", "-2.022"])
    assert args.comment == DEFAULT_COMMENT
    assert args.interval == DEFAULT_INTERVAL
    assert args.freq == DEFAULT_FREQ_MHZ
    assert args.gain == DEFAULT_GAIN
    assert args.sample_rate == DEFAULT_SAMPLE_RATE


def test_freq_band_validation():
    """Out-of-band frequency should cause exit."""
    with pytest.raises(SystemExit):
        parse_args(["--callsign", "G4DPZ-1", "--lat", "52.467", "--lon", "-2.022",
                     "--freq", "100.0"])
    with pytest.raises(SystemExit):
        parse_args(["--callsign", "G4DPZ-1", "--lat", "52.467", "--lon", "-2.022",
                     "--freq", "200.0"])
