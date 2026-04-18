"""Tests for AX.25 frame construction.

Property-based tests (Hypothesis) and unit tests for ax25_frame.py.
Feature: gnuradio-aprs-beacon
"""
import re
import sys
import os
import pytest
from hypothesis import given, settings, assume
from hypothesis import strategies as st

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from ax25_frame import (
    encode_address, format_latitude, format_longitude, build_info_field,
    compute_fcs, bytes_to_bits, bit_stuff, bit_destuff, build_frame,
    TOCALL, AX25_CTRL_UI, AX25_PID_NOLAYER3, FLAG, PREAMBLE_FLAGS,
)

# --- Hypothesis strategies ---

valid_base = st.text(
    alphabet=st.sampled_from("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"),
    min_size=1, max_size=6,
)
valid_ssid = st.integers(min_value=0, max_value=15)
valid_callsign = st.builds(
    lambda b, s: f"{b}-{s}" if s > 0 else b, valid_base, valid_ssid
)
valid_lat = st.floats(min_value=-90.0, max_value=90.0, allow_nan=False, allow_infinity=False)
valid_lon = st.floats(min_value=-180.0, max_value=180.0, allow_nan=False, allow_infinity=False)
valid_comment = st.text(
    alphabet=st.characters(min_codepoint=32, max_codepoint=126),
    min_size=0, max_size=64,
)

# --- Helper: extract frame fields from bitstream ---

def extract_frame_fields(bitstream):
    """Strip flags, de-stuff, extract AX.25 fields from bitstream."""
    flag_bits = [0, 1, 1, 1, 1, 1, 1, 0]

    # Find first non-flag byte boundary
    start = 0
    while start + 8 <= len(bitstream):
        chunk = bitstream[start:start + 8]
        if chunk != flag_bits:
            break
        start += 8

    # Find closing flag from end
    end = len(bitstream)
    while end - 8 >= start:
        chunk = bitstream[end - 8:end]
        if chunk != flag_bits:
            break
        end -= 8

    content_bits = bitstream[start:end]
    destuffed = bit_destuff(content_bits)

    # Extract fields — FCS is last 16 bits, LSB first like all other fields
    # Convert all destuffed bits back to bytes (LSB first)
    frame_bytes = bytearray()
    for i in range(0, len(destuffed) - 7, 8):
        byte = 0
        for j in range(8):
            byte |= destuffed[i + j] << j
        frame_bytes.append(byte)

    # Extract fields
    dst_addr = bytes(frame_bytes[0:7])
    src_addr = bytes(frame_bytes[7:14])
    ctrl = frame_bytes[14]
    pid = frame_bytes[15]
    fcs_bytes = bytes(frame_bytes[-2:])
    info = bytes(frame_bytes[16:-2])

    # Decode callsign from address
    def decode_call(addr_bytes):
        call = ""
        for i in range(6):
            call += chr(addr_bytes[i] >> 1)
        call = call.rstrip()
        ssid = (addr_bytes[6] >> 1) & 0x0F
        if ssid > 0:
            call += f"-{ssid}"
        return call

    return {
        "dst": decode_call(dst_addr),
        "src": decode_call(src_addr),
        "ctrl": ctrl,
        "pid": pid,
        "info": info,
        "fcs": fcs_bytes,
        "frame_content": bytes(frame_bytes[:-2]),
    }


# ===== Property 1: Frame construction round-trip =====

@given(callsign=valid_callsign, lat=valid_lat, lon=valid_lon, comment=valid_comment)
@settings(max_examples=200)
def test_frame_construction_roundtrip(callsign, lat, lon, comment):
    """Property 1: build_frame then extract fields recovers original inputs."""
    bitstream = build_frame(callsign, lat, lon, comment)
    fields = extract_frame_fields(bitstream)

    assert fields["dst"] == TOCALL
    assert fields["src"] == callsign.upper()
    assert fields["ctrl"] == AX25_CTRL_UI
    assert fields["pid"] == AX25_PID_NOLAYER3
    assert fields["info"].startswith(b"!")


# ===== Property 2: Position string structural invariants =====

@given(lat=valid_lat, lon=valid_lon, comment=valid_comment)
@settings(max_examples=200)
def test_position_string_structure(lat, lon, comment):
    """Property 2: build_info_field output matches APRS position format."""
    info = build_info_field(lat, lon, comment)
    s = info.decode("ascii")

    assert s[0] == "!"
    lat_field = s[1:9]
    assert s[9] == "/"
    lon_field = s[10:19]
    assert s[19] == "-"
    assert s[20:] == comment

    # Validate lat format DDMM.MMH
    assert re.match(r"^\d{4}\.\d{2}[NS]$", lat_field), f"bad lat: {lat_field}"
    dd = int(lat_field[:2])
    mm = float(lat_field[2:7])
    assert 0 <= dd <= 90
    assert 0.0 <= mm <= 60.0

    # Validate lon format DDDMM.MMH
    assert re.match(r"^\d{5}\.\d{2}[EW]$", lon_field), f"bad lon: {lon_field}"
    ddd = int(lon_field[:3])
    mmm = float(lon_field[3:8])
    assert 0 <= ddd <= 180
    assert 0.0 <= mmm <= 60.0

    # Hemisphere
    assert lat_field[-1] == ("N" if lat >= 0 else "S")
    assert lon_field[-1] == ("E" if lon >= 0 else "W")


# ===== Property 3: FCS integrity =====

@given(callsign=valid_callsign, lat=valid_lat, lon=valid_lon, comment=valid_comment)
@settings(max_examples=200)
def test_fcs_integrity(callsign, lat, lon, comment):
    """Property 3: FCS in frame validates correctly when recomputed."""
    bitstream = build_frame(callsign, lat, lon, comment)
    fields = extract_frame_fields(bitstream)

    recomputed = compute_fcs(fields["frame_content"])
    fcs_in_frame = fields["fcs"][0] | (fields["fcs"][1] << 8)
    assert recomputed == fcs_in_frame


# ===== Property 4: Bit stuffing round-trip =====

@given(bits=st.lists(st.integers(min_value=0, max_value=1), min_size=0, max_size=500))
@settings(max_examples=200)
def test_bit_stuffing_roundtrip(bits):
    """Property 4: stuff then destuff recovers original bits."""
    stuffed = bit_stuff(bits)
    recovered = bit_destuff(stuffed)
    assert recovered == bits

    # No run of 6+ consecutive 1s in stuffed output
    ones = 0
    for b in stuffed:
        if b == 1:
            ones += 1
            assert ones <= 5, f"run of {ones + 1} ones in stuffed output"
        else:
            ones = 0


# ===== Property 5: Invalid input rejection =====

@given(st.text(min_size=7, max_size=10))
@settings(max_examples=200)
def test_invalid_callsign_too_long(callsign):
    """Property 5: callsigns with base > 6 chars raise ValueError."""
    # Only test if base part is > 6
    base = callsign.split("-")[0]
    assume(len(base) > 6)
    with pytest.raises(ValueError):
        encode_address(callsign, last=True)


def test_invalid_callsign_empty():
    with pytest.raises(ValueError):
        encode_address("", last=True)


def test_invalid_callsign_non_alnum():
    with pytest.raises(ValueError):
        encode_address("G4!PZ", last=True)


def test_invalid_ssid_out_of_range():
    with pytest.raises(ValueError):
        encode_address("G4DPZ-16", last=True)


@given(lat=st.floats().filter(lambda x: x < -90.0 or x > 90.0))
@settings(max_examples=200)
def test_invalid_latitude(lat):
    """Property 5: out-of-range latitude raises ValueError."""
    assume(not (lat != lat))  # skip NaN
    with pytest.raises(ValueError):
        format_latitude(lat)


@given(lon=st.floats().filter(lambda x: x < -180.0 or x > 180.0))
@settings(max_examples=200)
def test_invalid_longitude(lon):
    """Property 5: out-of-range longitude raises ValueError."""
    assume(not (lon != lon))  # skip NaN
    with pytest.raises(ValueError):
        format_longitude(lon)


# ===== Unit tests =====

def test_encode_address_g4dpz():
    addr = encode_address("G4DPZ", last=False)
    assert len(addr) == 7
    assert addr[0] == ord("G") << 1
    assert addr[4] == ord("Z") << 1
    assert addr[5] == ord(" ") << 1
    assert addr[6] & 1 == 0  # not last

def test_encode_address_with_ssid():
    addr = encode_address("G4DPZ-1", last=True)
    assert len(addr) == 7
    ssid = (addr[6] >> 1) & 0x0F
    assert ssid == 1
    assert addr[6] & 1 == 1  # last
    assert addr[6] & 0xE0 == 0xE0  # reserved bits set

def test_format_lat_52_467():
    assert format_latitude(52.467) == "5228.02N"

def test_format_lon_neg2_022():
    assert format_longitude(-2.022) == "00201.32W"

def test_format_lat_zero():
    assert format_latitude(0.0) == "0000.00N"

def test_format_lon_zero():
    assert format_longitude(0.0) == "00000.00E"

def test_format_lat_south_pole():
    assert format_latitude(-90.0) == "9000.00S"

def test_format_lon_antimeridian():
    assert format_longitude(180.0) == "18000.00E"

def test_build_info_field_default_comment():
    info = build_info_field(52.467, -2.022, "github.com/g4dpz/ion-dtn-terrestrial")
    s = info.decode("ascii")
    assert s.startswith("!5228.02N/00201.32W-")
    assert s.endswith("github.com/g4dpz/ion-dtn-terrestrial")

def test_build_info_field_empty_comment():
    info = build_info_field(52.467, -2.022, "")
    s = info.decode("ascii")
    assert s == "!5228.02N/00201.32W-"

def test_compute_fcs_known_vector():
    # Known test: FCS of empty data
    fcs = compute_fcs(b"")
    assert fcs == 0xFFFF ^ 0xFFFF  # identity: 0x0000
    # Known test: FCS of "123456789"
    fcs = compute_fcs(b"123456789")
    assert fcs == 0x906E  # standard CRC-CCITT test vector (reflected)
