"""NRZI encoder/decoder for AX.25 AFSK transmission.

In NRZI encoding:
- A 0 bit causes a tone transition (toggle state)
- A 1 bit causes no transition (maintain state)

Initial state is mark (logical high / 1).
"""


def encode(bits: list) -> list:
    """NRZI-encode a bitstream.

    Args:
        bits: Input bitstream (0s and 1s).

    Returns:
        NRZI-encoded bitstream where each element represents
        the current tone state (1=mark, 0=space).
    """
    state = 0  # initial state: space (so flag 0x7E produces mark during 1-runs)
    out = []
    for b in bits:
        if b == 0:
            state ^= 1  # toggle
        # b == 1: no change
        out.append(state)
    return out


def decode(nrzi_bits: list) -> list:
    """Decode an NRZI-encoded bitstream back to original bits.

    Args:
        nrzi_bits: NRZI-encoded bitstream.

    Returns:
        Original bitstream.
    """
    prev = 0  # initial state: space (matches encoder)
    out = []
    for b in nrzi_bits:
        if b == prev:
            out.append(1)  # no transition = 1
        else:
            out.append(0)  # transition = 0
        prev = b
    return out
