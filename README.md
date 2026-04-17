# DTN over Terrestrial Amateur Radio

An AMSAT-UK project exploring Delay-Tolerant Networking (DTN) protocols over 1200 baud VHF amateur packet radio, using standard amateur radio equipment.

## Overview

This project implements a complete protocol stack for reliable data transfer between amateur radio stations over RF links characterised by long propagation delays and constrained bandwidth. The work is a stepping stone toward DTN-based communication on future amateur satellite missions.

The protocol stack, from lowest to highest:

1. **RF physical layer** — 1200 baud AFSK on VHF amateur bands
2. **KISS framing** — host-to-TNC serial protocol for packet delineation
3. **AX.25 UI frames** — standard amateur packet radio addressing (send/receive/echo/ping modes)
4. **APRS position beacons** — periodic station identification and position reporting for regulatory compliance
5. **LTP (Licklider Transmission Protocol)** — reliable data transfer with checkpoint/report acknowledgment
6. **BPv7 (Bundle Protocol version 7)** — DTN bundle encoding with CBOR serialisation, fragmentation, and reassembly

Successfully tested over the air: a 16,854-byte PDF document was transferred bit-perfect between two stations using BPv7 fragmentation over LTP over KISS over 1200 baud VHF.

## Hardware

- Yaesu FT-817 transceiver
- Mobilinkd TNC3 (USB KISS TNC, 1200 baud AFSK)
- Linux computer (Ubuntu laptop or Raspberry Pi)

## Building

```
cd kiss-interface
make
```

Requires only gcc and standard POSIX headers. No external dependencies.

## Running Tests

```
cd kiss-interface
make test
```

144 tests across 11 test suites covering KISS encoding, AX.25 framing, serial port handling, CLI parsing, ping payloads, SDNV encoding, LTP protocol logic, APRS beacon formatting, APRS frame decoding, CBOR encoding, and BPv7 bundle encoding/fragmentation/reassembly. Includes property-based tests with 1000 iterations per property.

## Commands

```
kiss_interface <command> [options]
```

| Command | Description |
|---------|-------------|
| `send` | Send a single AX.25 packet |
| `receive` | Continuously receive and display AX.25 packets |
| `echo` | Receive packets and retransmit with swapped callsigns |
| `ping` | Send ping packets and measure round-trip time |
| `ltp-send` | Send a data block reliably using LTP |
| `ltp-recv` | Receive data blocks reliably using LTP |
| `beacon` | Transmit periodic APRS position beacons |
| `bp-send` | Send a BPv7 bundle over LTP (with fragmentation for large payloads) |
| `bp-recv` | Receive and reassemble BPv7 bundles over LTP |

### Common Options

| Option | Description | Default |
|--------|-------------|---------|
| `--device <path[:baud]>` | Serial device (required) | — |
| `--verbose` | Enable verbose/debug output | off |
| `--txdelay <ms>` | TX-delay in milliseconds | 500 |
| `--txtail <ms>` | TX-tail in milliseconds | 300 |

### LTP/BP Options

| Option | Description | Default |
|--------|-------------|---------|
| `--local <eid>` | Local DTN endpoint (e.g. `dtn://g4dpz-1`) | — |
| `--remote <eid>` | Remote DTN endpoint | — |
| `--mtu <bytes>` | LTP segment MTU | 64 |
| `--owlt <ms>` | One-way light time estimate | 1500 |
| `--retries <n>` | Max retransmission attempts | 7 |
| `--file <path>` | Read payload from file (bp-send) | — |
| `--outdir <dir>` | Write received bundles to directory (bp-recv) | — |
| `--lifetime <seconds>` | Bundle lifetime | 3600 |

### Beacon Options

| Option | Description | Default |
|--------|-------------|---------|
| `--beacon` | Enable APRS beaconing (ltp-send/recv, bp-send/recv) | off |
| `--callsign <call>` | Beacon source callsign | — |
| `--lat <degrees>` | Latitude in decimal degrees | — |
| `--lon <degrees>` | Longitude in decimal degrees | — |
| `--comment <text>` | Beacon comment | repo URL |
| `--beacon-interval <s>` | Beacon interval in seconds | 120 |

### Examples

```bash
# AX.25 packet radio
./kiss_interface send --device /dev/ttyACM0 --src G4DPZ-1 --dst G4DPZ-2 "Hello World"
./kiss_interface receive --device /dev/ttyACM0 --verbose
./kiss_interface echo --device /dev/ttyACM0 --src G4DPZ-2 --dst G4DPZ-1

# Ping with RTT measurement
./kiss_interface ping --device /dev/ttyACM0 --src G4DPZ-1 --dst G4DPZ-2 --count 5

# APRS position beacon
./kiss_interface beacon --device /dev/ttyACM0 --callsign G4DPZ-1 --lat 52.467 --lon -2.022

# LTP reliable transfer
./kiss_interface ltp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 "Hello from LTP"
./kiss_interface ltp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 --verbose

# LTP with APRS beacon identification
./kiss_interface ltp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 \
  --beacon --callsign G4DPZ-1 --lat 52.467 --lon -2.022 "Hello from LTP"
./kiss_interface ltp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 \
  --beacon --callsign G4DPZ-2 --lat 52.467 --lon -2.022

# BPv7 bundle transfer (text)
./kiss_interface bp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 "Hello BPv7"
./kiss_interface bp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2

# BPv7 file transfer with beacon (large files auto-fragment at 800 bytes)
./kiss_interface bp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 \
  --owlt 6000 --beacon --callsign G4DPZ-1 --lat 52.467 --lon -2.022 \
  --file ../docs/OFCOM-DTN-KISS-Compliance.pdf
./kiss_interface bp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 \
  --owlt 6000 --beacon --callsign G4DPZ-2 --lat 52.467 --lon -2.022 \
  --outdir ./received/
```

> **Note:** For large file transfers over 1200 baud, use `--owlt 6000` on both sender and receiver. The default 1500ms is too short for multi-fragment transfers at this data rate.

## Architecture

```
kiss-interface/
├── kiss.c / kiss.h       KISS frame encoding/decoding
├── ax25.c / ax25.h       AX.25 UI frame construction/parsing
├── serial.c / serial.h   Serial port handling (termios)
├── ping.c / ping.h       Ping payload construction/parsing
├── sdnv.c / sdnv.h       Self-Delimiting Numeric Values (LTP header encoding)
├── ltp.c / ltp.h         LTP engine (sessions, timers, checkpoint/report)
├── beacon.c / beacon.h   APRS position beacon (AX.25 UI, KISS pre-built)
├── aprs.c / aprs.h       APRS frame classifier and position decoder
├── cbor.c / cbor.h       CBOR encoding/decoding (for BPv7)
├── bp.c / bp.h           BPv7 bundle encoding, fragmentation, reassembly
├── main.c                CLI parsing, mode dispatch, signal handling
├── Makefile              Build system
├── test_kiss.c           KISS tests (5 tests)
├── test_ax25.c           AX.25 tests (5 tests)
├── test_serial.c         Serial tests (6 tests)
├── test_cli.c            CLI parsing tests (33 tests)
├── test_ping.c           Ping payload tests (7 tests)
├── test_sdnv.c           SDNV tests (11 tests)
├── test_ltp.c            LTP protocol tests (25 tests)
├── test_beacon.c         Beacon tests (23 tests)
├── test_aprs.c           APRS decoder tests (11 tests)
├── test_cbor.c           CBOR tests (7 tests)
└── test_bp.c             BPv7 tests (11 tests)
```

## Standards

- [KISS Protocol](https://files.tapr.org/tech_docs/Packet/kiss.txt) — Phil Karn KA9Q, Mike Chepponis K3MC
- [AX.25 v2.2](https://web.tapr.org/tech_docs/AX25/ax25.doc) — TAPR
- [APRS Protocol Reference](http://www.aprs.org/doc/APRS101.PDF) — APRS Working Group
- [RFC 5326](https://www.rfc-editor.org/rfc/rfc5326) — Licklider Transmission Protocol
- [RFC 9171](https://www.rfc-editor.org/rfc/rfc9171) — Bundle Protocol Version 7
- [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) — Concise Binary Object Representation (CBOR)
- [CCSDS 734.1-B-1](https://public.ccsds.org/Pubs/734x1b1.pdf) — LTP for CCSDS

## Regulatory

This system operates under OFCOM amateur radio licence conditions. APRS beacons provide station identification as required. See [docs/OFCOM-DTN-KISS-Compliance.md](docs/OFCOM-DTN-KISS-Compliance.md) for the full regulatory compliance document.

## CI

[![CI](https://github.com/g4dpz/ion-dtn-terrestrial/actions/workflows/ci.yml/badge.svg)](https://github.com/g4dpz/ion-dtn-terrestrial/actions/workflows/ci.yml)

## Licence

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

Copyright 2026 AMSAT-UK.
