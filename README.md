# DTN over Terrestrial Amateur Radio

An AMSAT-UK project exploring Delay-Tolerant Networking (DTN) protocols over 1200 baud VHF amateur packet radio, using standard amateur radio equipment.

## Overview

This project implements a layered protocol stack for reliable data transfer between amateur radio stations over RF links characterised by long propagation delays and constrained bandwidth. The work is a stepping stone toward DTN-based communication on future amateur satellite missions.

The protocol stack, from lowest to highest:

1. **RF physical layer** — 1200 baud AFSK on VHF amateur bands
2. **KISS framing** — host-to-TNC serial protocol for packet delineation
3. **AX.25 UI frames** — standard amateur packet radio addressing (send/receive/echo/ping modes)
4. **LTP (Licklider Transmission Protocol)** — reliable data transfer with checkpoint/report acknowledgment (ltp-send/ltp-recv modes)

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

89 tests across 7 test suites covering KISS encoding, AX.25 framing, serial port handling, CLI parsing, ping payloads, SDNV encoding, and LTP protocol logic. Includes property-based tests with 1000 iterations per property.

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

### Examples

```bash
# AX.25 packet radio
./kiss_interface send --device /dev/ttyACM0 --src G4DPZ-1 --dst G4DPZ-2 "Hello World"
./kiss_interface receive --device /dev/ttyACM0 --verbose
./kiss_interface echo --device /dev/ttyACM0 --src G4DPZ-2 --dst G4DPZ-1

# Ping with RTT measurement
./kiss_interface ping --device /dev/ttyACM0 --src G4DPZ-1 --dst G4DPZ-2 --count 5

# LTP reliable transfer (DTN addressing)
./kiss_interface ltp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 "Hello from LTP"
./kiss_interface ltp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 --verbose
```

## Architecture

```
kiss-interface/
├── kiss.c / kiss.h       KISS frame encoding/decoding
├── ax25.c / ax25.h       AX.25 UI frame construction/parsing
├── serial.c / serial.h   Serial port handling (termios)
├── ping.c / ping.h       Ping payload construction/parsing
├── sdnv.c / sdnv.h       Self-Delimiting Numeric Values (LTP header encoding)
├── ltp.c / ltp.h         LTP engine (sessions, timers, checkpoint/report)
├── main.c                CLI parsing, mode dispatch, signal handling
├── Makefile              Build system
├── test_kiss.c           KISS tests
├── test_ax25.c           AX.25 tests
├── test_serial.c         Serial tests
├── test_cli.c            CLI parsing tests
├── test_ping.c           Ping payload tests
├── test_sdnv.c           SDNV tests
└── test_ltp.c            LTP protocol tests
```

## Standards

- [KISS Protocol](https://files.tapr.org/tech_docs/Packet/kiss.txt) — Phil Karn KA9Q, Mike Chepponis K3MC
- [AX.25 v2.2](https://web.tapr.org/tech_docs/AX25/ax25.doc) — TAPR
- [RFC 5326](https://www.rfc-editor.org/rfc/rfc5326) — Licklider Transmission Protocol
- [RFC 9171](https://www.rfc-editor.org/rfc/rfc9171) — Bundle Protocol Version 7
- [CCSDS 734.1-B-1](https://public.ccsds.org/Pubs/734x1b1.pdf) — LTP for CCSDS

## Regulatory

This system operates under OFCOM amateur radio licence conditions. See [docs/OFCOM-DTN-KISS-Compliance.md](docs/OFCOM-DTN-KISS-Compliance.md) for the full regulatory compliance document.

## CI

[![CI](https://github.com/g4dpz/ion-dtn-terrestrial/actions/workflows/ci.yml/badge.svg)](https://github.com/g4dpz/ion-dtn-terrestrial/actions/workflows/ci.yml)

## Licence

This project is developed by AMSAT-UK for amateur radio experimentation and education.
