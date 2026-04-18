#!/usr/bin/env python3
"""GNU Radio APRS beacon transmitter for Ettus B200 mini.

Usage:
    python3 gnuradio/aprs_beacon.py --callsign G4DPZ-1 --lat 52.467 --lon -2.022

Constructs AX.25 UI frames with APRS position reports, applies NRZI encoding,
generates Bell 202 AFSK audio, FM-modulates, and transmits via B200 mini SDR.
"""

import argparse
import signal
import sys
import time

DEFAULT_COMMENT = "github.com/g4dpz/ion-dtn-terrestrial"
DEFAULT_INTERVAL = 120
DEFAULT_FREQ_MHZ = 144.850
DEFAULT_GAIN = 50
DEFAULT_SAMPLE_RATE = 480000
MIN_INTERVAL = 10
MAX_INTERVAL = 3600
BAND_LOW_MHZ = 144.000
BAND_HIGH_MHZ = 146.000
MAX_COMMENT_LEN = 128


def parse_args(argv=None):
    """Parse and validate command-line arguments."""
    parser = argparse.ArgumentParser(
        description="GNU Radio APRS beacon transmitter for Ettus B200 mini"
    )
    parser.add_argument("--callsign", required=True,
                        help="Source callsign-SSID (e.g. G4DPZ-1)")
    parser.add_argument("--lat", type=float, required=True,
                        help="Latitude in decimal degrees")
    parser.add_argument("--lon", type=float, required=True,
                        help="Longitude in decimal degrees")
    parser.add_argument("--comment", default=DEFAULT_COMMENT,
                        help=f"Beacon comment (default: {DEFAULT_COMMENT})")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL,
                        help=f"Beacon interval in seconds (default: {DEFAULT_INTERVAL})")
    parser.add_argument("--freq", type=float, default=DEFAULT_FREQ_MHZ,
                        help=f"Transmit frequency in MHz (default: {DEFAULT_FREQ_MHZ})")
    parser.add_argument("--gain", type=int, default=DEFAULT_GAIN,
                        help=f"B200 transmit gain 0-89 (default: {DEFAULT_GAIN})")
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE,
                        dest="sample_rate",
                        help=f"SDR sample rate in Hz (default: {DEFAULT_SAMPLE_RATE})")
    parser.add_argument("--deviation", type=float, default=3000.0,
                        help="FM deviation in Hz (default: 3000)")
    parser.add_argument("--wav-out", default=None, dest="wav_out",
                        help="Write AFSK audio to WAV file instead of transmitting (for testing)")

    args = parser.parse_args(argv)

    # Validate
    errors = []
    if args.lat < -90.0 or args.lat > 90.0:
        errors.append(f"--lat must be -90 to +90, got {args.lat}")
    if args.lon < -180.0 or args.lon > 180.0:
        errors.append(f"--lon must be -180 to +180, got {args.lon}")
    if args.freq < BAND_LOW_MHZ or args.freq > BAND_HIGH_MHZ:
        errors.append(f"--freq must be {BAND_LOW_MHZ}-{BAND_HIGH_MHZ} MHz, got {args.freq}")
    if args.gain < 0 or args.gain > 89:
        errors.append(f"--gain must be 0-89, got {args.gain}")
    if args.interval < MIN_INTERVAL or args.interval > MAX_INTERVAL:
        errors.append(f"--interval must be {MIN_INTERVAL}-{MAX_INTERVAL}, got {args.interval}")
    if args.sample_rate < DEFAULT_SAMPLE_RATE:
        errors.append(f"--sample-rate must be >= {DEFAULT_SAMPLE_RATE}, got {args.sample_rate}")

    if errors:
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    # Truncate comment
    args.comment = args.comment[:MAX_COMMENT_LEN]

    return args


def validate_freq(freq_mhz):
    """Check frequency is within 2m band. Returns True if valid."""
    return BAND_LOW_MHZ <= freq_mhz <= BAND_HIGH_MHZ


def validate_gain(gain):
    """Check gain is 0-89. Returns True if valid."""
    return 0 <= gain <= 89


def validate_interval(interval):
    """Check interval is within range. Returns True if valid."""
    return MIN_INTERVAL <= interval <= MAX_INTERVAL


# Global shutdown flag
_shutdown = False


def _signal_handler(sig, frame):
    global _shutdown
    _shutdown = True


def run_beacon(args):
    """Main beacon loop.

    Transmits one beacon immediately, then repeats at the configured interval.
    Handles SIGINT for clean shutdown.

    Returns:
        Exit code (0 for clean shutdown, 1 for error).
    """
    global _shutdown
    _shutdown = False

    from ax25_frame import build_frame
    from nrzi import encode as nrzi_encode
    from afsk_mod import modulate

    # Pre-build the frame (same every time)
    bitstream = build_frame(args.callsign, args.lat, args.lon, args.comment)
    nrzi_bits = nrzi_encode(bitstream)
    audio = modulate(nrzi_bits)

    # WAV output mode — write audio and exit (no hardware needed)
    if args.wav_out:
        import wave
        import numpy as np
        with wave.open(args.wav_out, "w") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)  # 16-bit
            wf.setframerate(48000)
            # Pad with silence to fill the beacon interval (for GRC repeat mode)
            total_samples = args.interval * 48000
            silence_after = total_samples - len(audio)
            if silence_after < 0:
                silence_after = 48000  # at least 1s gap
            padded = np.concatenate([audio, np.zeros(silence_after, dtype=np.float32)])
            samples_int16 = (padded * 32767).astype("int16")
            wf.writeframes(samples_int16.tobytes())
        print(f"Wrote {len(padded)} samples ({len(padded)/48000:.1f}s) to {args.wav_out}")
        print(f"  Beacon audio: {len(audio)/48000:.3f}s, silence: {silence_after/48000:.1f}s")
        print(f"  Total loop period: {len(padded)/48000:.1f}s ({args.interval}s interval)")
        print(f"Test with: atest {args.wav_out}")
        return 0

    from flowgraph import APRSFlowgraph

    # Install signal handler
    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)

    # Create flowgraph
    freq_hz = args.freq * 1e6
    try:
        fg = APRSFlowgraph(
            freq_hz=freq_hz,
            gain=args.gain,
            sdr_sample_rate=args.sample_rate,
            fm_deviation=args.deviation,
        )
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    print(f"APRS beacon: {args.callsign} on {args.freq:.3f} MHz, "
          f"gain={args.gain}, interval={args.interval}s")

    next_tx = time.monotonic()

    while not _shutdown:
        now = time.monotonic()
        if now >= next_tx:
            # Transmit
            ts = time.strftime("%Y-%m-%dT%H:%M:%S")
            print(f"[{ts}] Beacon: {args.callsign}")
            try:
                fg.transmit(audio)
            except Exception as e:
                print(f"error: transmit failed: {e}", file=sys.stderr)

            next_tx = now + args.interval

        # Sleep in short intervals to check shutdown flag
        sleep_time = min(next_tx - time.monotonic(), 1.0)
        if sleep_time > 0:
            time.sleep(sleep_time)

    fg.stop()
    print("\nBeacon stopped.")
    return 0


if __name__ == "__main__":
    args = parse_args()
    sys.exit(run_beacon(args))
