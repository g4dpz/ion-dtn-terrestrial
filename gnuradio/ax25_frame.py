"""AX.25 UI frame construction for APRS beacons.

Implements address encoding, APRS position formatting, CRC-CCITT FCS,
bit stuffing, and flag framing. Pure Python, no external dependencies.
Reference: kiss-interface/ax25.c, kiss-interface/beacon.c
"""

AX25_ADDR_LEN = 7
AX25_CTRL_UI = 0x03
AX25_PID_NOLAYER3 = 0xF0
TOCALL = "APZ001"
CRC_POLY = 0x8408
CRC_INIT = 0xFFFF
PREAMBLE_FLAGS = 80
FLAG = 0x7E


def _parse_callsign(callsign: str) -> tuple:
    """Parse callsign string into (base, ssid).

    Raises ValueError if invalid.
    """
    if not callsign:
        raise ValueError("callsign must not be empty")

    parts = callsign.upper().split("-", 1)
    base = parts[0]
    ssid = 0

    if len(parts) == 2:
        try:
            ssid = int(parts[1])
        except ValueError:
            raise ValueError(f"invalid SSID in callsign '{callsign}'")

    if not base or len(base) > 6:
        raise ValueError(f"base callsign must be 1-6 characters, got '{base}'")
    if not base.isalnum():
        raise ValueError(f"base callsign must be alphanumeric, got '{base}'")
    if ssid < 0 or ssid > 15:
        raise ValueError(f"SSID must be 0-15, got {ssid}")

    return base, ssid


def encode_address(callsign: str, last: bool) -> bytes:
    """Encode callsign-SSID into 7-byte AX.25 address field.

    Args:
        callsign: Callsign string, e.g. "G4DPZ" or "G4DPZ-1".
        last: True if this is the final address field (sets extension bit).

    Returns:
        7-byte address field.
    """
    base, ssid = _parse_callsign(callsign)
    # Pad base to 6 chars, left-shift each by 1
    padded = base.ljust(6)
    addr = bytearray(AX25_ADDR_LEN)
    for i in range(6):
        addr[i] = ord(padded[i]) << 1
    # SSID byte: 0xE0 | (ssid << 1) | extension_bit
    # Bits 5,6,7 are reserved and set to 1 per AX.25 spec
    addr[6] = 0xE0 | (ssid << 1) | (1 if last else 0)
    return bytes(addr)


def format_latitude(lat: float) -> str:
    """Format decimal degrees latitude to APRS DDMM.MMN string.

    Args:
        lat: Latitude in decimal degrees, -90.0 to +90.0.

    Returns:
        8-character string, e.g. "5228.02N".
    """
    if lat < -90.0 or lat > 90.0:
        raise ValueError(f"latitude must be -90 to +90, got {lat}")
    hemi = "N" if lat >= 0 else "S"
    lat = abs(lat)
    deg = int(lat)
    minutes = (lat - deg) * 60.0
    return f"{deg:02d}{minutes:05.2f}{hemi}"


def format_longitude(lon: float) -> str:
    """Format decimal degrees longitude to APRS DDDMM.MMW string.

    Args:
        lon: Longitude in decimal degrees, -180.0 to +180.0.

    Returns:
        9-character string, e.g. "00201.32W".
    """
    if lon < -180.0 or lon > 180.0:
        raise ValueError(f"longitude must be -180 to +180, got {lon}")
    hemi = "E" if lon >= 0 else "W"
    lon = abs(lon)
    deg = int(lon)
    minutes = (lon - deg) * 60.0
    return f"{deg:03d}{minutes:05.2f}{hemi}"


def build_info_field(lat: float, lon: float, comment: str = "") -> bytes:
    """Build APRS position info field.

    Format: !DDMM.MMN/DDDMM.MMW-comment
    """
    lat_str = format_latitude(lat)
    lon_str = format_longitude(lon)
    info = f"!{lat_str}/{lon_str}-{comment}"
    return info.encode("ascii")


def compute_fcs(data: bytes) -> int:
    """Compute CRC-CCITT FCS over data.

    Uses polynomial 0x8408 (reflected), initial value 0xFFFF.
    Returns 16-bit FCS value.
    """
    crc = CRC_INIT
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ CRC_POLY
            else:
                crc >>= 1
    return crc ^ 0xFFFF


def bytes_to_bits(data: bytes) -> list:
    """Convert bytes to list of bits, LSB first per byte."""
    bits = []
    for byte in data:
        for i in range(8):
            bits.append((byte >> i) & 1)
    return bits


def bit_stuff(bits: list) -> list:
    """Apply AX.25 bit stuffing: insert 0 after five consecutive 1s."""
    out = []
    ones = 0
    for b in bits:
        out.append(b)
        if b == 1:
            ones += 1
            if ones == 5:
                out.append(0)
                ones = 0
        else:
            ones = 0
    return out


def bit_destuff(bits: list) -> list:
    """Remove AX.25 bit stuffing: remove 0 after five consecutive 1s."""
    out = []
    ones = 0
    skip_next = False
    for b in bits:
        if skip_next:
            skip_next = False
            ones = 0
            continue
        out.append(b)
        if b == 1:
            ones += 1
            if ones == 5:
                skip_next = True
        else:
            ones = 0
    return out


def build_frame(src_callsign: str, lat: float, lon: float,
                comment: str = "") -> list:
    """Build complete AX.25 UI frame as a bitstream with flags.

    Returns:
        List of bits (0/1 integers) representing the complete frame.
    """
    # Build frame bytes: dst addr + src addr + ctrl + pid + info
    dst_addr = encode_address(TOCALL, last=False)
    src_addr = encode_address(src_callsign, last=True)
    info = build_info_field(lat, lon, comment)
    frame_bytes = dst_addr + src_addr + bytes([AX25_CTRL_UI, AX25_PID_NOLAYER3]) + info

    # Compute FCS and append as bytes (little-endian, LSB first like all fields)
    fcs = compute_fcs(frame_bytes)
    fcs_bytes = bytes([fcs & 0xFF, (fcs >> 8) & 0xFF])

    # Convert entire frame + FCS to bits (LSB first) and apply bit stuffing
    frame_bits = bytes_to_bits(frame_bytes + fcs_bytes)
    stuffed = bit_stuff(frame_bits)

    # Build flag bits (0x7E = 01111110)
    flag_bits = [0, 1, 1, 1, 1, 1, 1, 0]

    # Assemble: preamble flags + stuffed content + closing flags
    # Multiple closing flags needed for reliable receiver frame detection
    result = flag_bits * PREAMBLE_FLAGS + stuffed + flag_bits * 3
    return result
