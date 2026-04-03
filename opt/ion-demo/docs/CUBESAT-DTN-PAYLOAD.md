# CubeSat DTN Payload Design

## Overview

This document describes the design of a delay-tolerant networking (DTN) payload for a CubeSat platform. The payload implements BPv7 (RFC 9171) and LTP (RFC 5326) on an embedded microcontroller, communicating with a ground station via UHF amateur radio during scheduled orbital passes.

The design builds directly on the terrestrial ION-DTN demonstration system, reusing the proven AX.25/KISS/LTP protocol stack.

## Mission Concept

A 1U or 2U CubeSat in LEO (400-600km) collects sensor data continuously and downlinks it to a ground station during pass windows. The DTN payload handles:

- Queuing sensor data as bundles during non-contact periods
- Transmitting bundles during ground station passes (5-15 min per pass)
- Receiving command bundles from the ground station
- Reliable delivery via LTP acknowledgment and retransmission
- Surviving resets and power cycles with persistent bundle storage

## Architecture

```
┌─────────────────────────────────────────────┐
│              CubeSat Bus                     │
│  ┌─────────┐  ┌──────────┐  ┌────────────┐ │
│  │ Sensors │  │ Power    │  │ ADCS       │ │
│  │ (I2C/   │  │ Mgmt     │  │ (attitude) │ │
│  │  SPI)   │  │ (EPS)    │  │            │ │
│  └────┬────┘  └────┬─────┘  └────────────┘ │
│       │             │                        │
│  ┌────┴─────────────┴────────────────────┐  │
│  │         DTN Payload (STM32H7)         │  │
│  │                                       │  │
│  │  ┌─────────┐  ┌──────┐  ┌─────────┐  │  │
│  │  │ BPv7    │  │ LTP  │  │ Contact │  │  │
│  │  │ Engine  │  │Engine│  │ Plan    │  │  │
│  │  └────┬────┘  └──┬───┘  └────┬────┘  │  │
│  │       │          │            │       │  │
│  │  ┌────┴──────────┴────────────┴────┐  │  │
│  │  │     AX.25 / KISS CLA           │  │  │
│  │  └────────────┬────────────────────┘  │  │
│  │               │ UART                  │  │
│  │  ┌────────────┴────────────────────┐  │  │
│  │  │     Flash Storage (bundles)     │  │  │
│  │  └─────────────────────────────────┘  │  │
│  └───────────────┬───────────────────────┘  │
│                  │ UART                      │
│  ┌───────────────┴───────────────────────┐  │
│  │         UHF Radio Module              │  │
│  │   (9600 baud FSK or 1200 AFSK)       │  │
│  └───────────────┬───────────────────────┘  │
│                  │ RF                        │
│              ┌───┴───┐                       │
│              │Antenna│                       │
│              └───────┘                       │
└─────────────────────────────────────────────┘
```

## Hardware Selection

### Processor: STM32H743 or STM32H753

- ARM Cortex-M7, 480 MHz
- 1 MB SRAM (sufficient for bundle processing)
- 2 MB internal flash (firmware + small bundle store)
- External QSPI flash for larger bundle storage (up to 128 MB)
- Multiple UART peripherals (radio, debug, sensor bus)
- Hardware CRC for LTP/BP integrity checks
- RTC with battery backup for contact scheduling
- Operating temperature: -40 to +85C (industrial grade)
- Radiation: not inherently rad-hard, but SEL-tolerant variants exist
- Power: ~200mW active, ~10uW standby

### Radio: UHF Transceiver Module

Options (COTS CubeSat radios):
- Endurosat UHF Type II: 9600 baud, KISS interface, 1W TX
- ISIS TXS/TRXVU: 9600 baud, I2C/UART, 0.5-2W TX
- GomSpace NanoCom AX100: 9600 baud, CSP/KISS, 0.5W TX
- Custom: Si4463/CC1200 transceiver + PA, KISS over UART

All accept KISS-framed data over UART — the CLA code from the ground demo works directly.

### Storage: External Flash

- W25Q128 or IS25LP128: 128 Mbit QSPI flash
- Wear leveling via simple circular buffer
- Bundle metadata in SRAM, payload in flash
- Survives power cycles (persistent storage requirement)

## Software Architecture

### RTOS: FreeRTOS

Tasks:
- DTN Task: BPv7 engine, bundle creation/forwarding/delivery
- LTP Task: segment management, timers, retransmission
- CLA Task: KISS/AX.25 encode/decode, UART I/O
- Sensor Task: periodic data collection, bundle creation
- Contact Task: orbital predictor, radio power control
- Housekeeping: watchdog, telemetry, health monitoring

### Memory Budget (1 MB SRAM)

```
FreeRTOS kernel + stacks:     64 KB
BPv7 engine + bundle table:  128 KB
LTP engine + session table:   64 KB
KISS/AX.25 buffers:           32 KB
Sensor data buffers:           64 KB
Working memory:               128 KB
Reserve:                      520 KB
```

### Flash Budget (2 MB internal + 16 MB external)

```
Firmware:                    256 KB (internal flash)
Contact plan:                 16 KB (internal flash)
Configuration:                 4 KB (internal flash)
Bundle storage:               16 MB (external QSPI flash)
```

At 16 MB storage and ~100 bytes per telemetry bundle, the satellite can store ~160,000 bundles between passes.

## Protocol Stack

### Layer 1: Physical

- UHF amateur band (435 MHz typical)
- 9600 baud GMSK/FSK (or 1200 baud AFSK for simpler radios)
- Half-duplex operation
- TX power: 0.5-2W depending on link budget

### Layer 2: AX.25 / KISS

- Identical to ground demo implementation
- AX.25 UI frames with amateur callsign identification
- KISS framing over UART to radio module
- Source from: `ax25.c`, `kiss.c` (already written and tested)

### Layer 3: LTP (RFC 5326)

Embedded implementation requirements:
- Session management (max 8-16 concurrent sessions)
- Segment encoding/decoding (SDNV or fixed-width)
- Retransmission timer (based on OWLT from contact plan)
- Report generation and processing
- Checkpoint/report acknowledgment
- Cancellation handling

Key differences from ION's LTP:
- No shared memory / SDR — direct memory management
- Timer-based retransmission using FreeRTOS software timers
- Session table in SRAM (small, bounded)
- Segments read from/written to flash storage

### Layer 4: BPv7 (RFC 9171)

Embedded implementation requirements:
- CBOR encoding/decoding (minimal subset)
- Bundle creation with primary block + payload block
- Bundle storage (flash-backed queue)
- Bundle forwarding based on contact plan
- Bundle delivery to local application
- Bundle lifetime management and expiry
- CRC-16 for integrity (hardware-accelerated on STM32)

Simplifications for CubeSat:
- Single destination (ground station) — no complex routing
- No fragmentation at BP layer (LTP handles it)
- No custody transfer (removed in BPv7)
- No security blocks initially (can add later)
- Static contact plan (uploaded from ground)

## Contact Plan and Orbital Mechanics

### Pass Prediction

For LEO at 400km altitude:
- Orbital period: ~92 minutes
- Pass duration: 5-15 minutes (depending on elevation)
- Passes per day: 4-6 visible from a single ground station
- Maximum range: ~2500 km at 5 degree elevation

### Contact Plan Structure

```c
typedef struct {
    uint32_t pass_start;    /* Unix timestamp */
    uint16_t duration_sec;  /* Pass duration */
    uint8_t  max_elevation; /* Degrees, for link budget */
    uint8_t  direction;     /* Ascending/descending */
} contact_entry_t;

/* Uploaded from ground, typically 24-48 hours of passes */
#define MAX_CONTACTS 64
contact_entry_t contact_plan[MAX_CONTACTS];
```

### Pass Execution Sequence

```
1. RTC alarm fires at pass_start - 30 seconds
2. Power on radio, warm up
3. Begin listening for ground station beacon
4. On beacon received: start LTP session, transmit queued bundles
5. Receive command bundles from ground station
6. At pass_end: complete current LTP session, power off radio
7. Return to data collection mode
```

## Link Budget (435 MHz, 400km LEO)

```
TX power:           +30 dBm (1W)
TX antenna gain:    +2 dBi (monopole)
Path loss (400km):  -145 dB (free space at 435 MHz)
RX antenna gain:    +12 dBi (ground station Yagi)
RX sensitivity:     -120 dBm (9600 baud)
────────────────────────────────
Link margin:        +19 dB (comfortable)
```

At 9600 baud with this margin, the CubeSat can reliably downlink ~7 KB per second, or ~4 MB per 10-minute pass.

## Development Phases

### Phase 1: Ground Prototype (DONE)

- ION-DTN over UHF amateur radio
- Two-node file transfer with LTP acknowledgment
- Simulated lunar delay
- Proven: BPv7, LTP, AX.25, KISS, serial CLA

### Phase 2: Embedded Prototype

- STM32H7 development board (Nucleo-H743ZI)
- FreeRTOS port
- Minimal LTP engine (no ION dependency)
- Minimal BPv7 CBOR codec
- KISS/AX.25 CLA (reuse ground demo code)
- UART to Mobilinkd TNC3 (same hardware as ground demo)
- Test against ground station running ION

### Phase 3: Flight Hardware

- Custom PCB with STM32H7 + QSPI flash + UHF radio
- Radiation testing
- Thermal vacuum testing
- Vibration testing
- EMC/EMI testing
- Flight software qualification

### Phase 4: Integration and Test

- Integration with CubeSat bus
- End-to-end testing with ground station
- Orbital simulation testing
- Contact plan upload and execution testing

### Phase 5: Launch and Operations

- Deploy and commission
- Upload initial contact plan
- Begin DTN operations
- Monitor and update contact plans

## Reusable Code from Ground Demo

The following code from this project can be reused directly on the STM32:

| Module | File | Reuse Level |
|--------|------|-------------|
| KISS encode/decode | `kiss.c`, `kiss.h` | Direct (no OS deps) |
| AX.25 frame build/parse | `ax25.c`, `ax25.h` | Direct (no OS deps) |
| BPv7 CBOR codec | `bp.c`, `bp.h` | Direct (needs completion) |
| LTP segment encode/decode | From test tools | Partial (need session mgmt) |
| Serial port I/O | `ionserialcla.c` | Adapt to STM32 HAL UART |

## Power Budget

```
Mode            Duration    Current    Energy
────────────────────────────────────────────
Sleep           80 min      5 mA       24 mAh
Data collect    10 min      50 mA      8.3 mAh
TX pass         10 min      500 mA     83.3 mAh
────────────────────────────────────────────
Per orbit (92 min):                    115.6 mAh
Per day (15.6 orbits):                 1803 mAh

Typical 1U battery: 2600 mAh
Typical 1U solar:   ~2000 mAh/day (average)
Margin:             positive (sustainable)
```

## References

- RFC 9171: Bundle Protocol Version 7
- RFC 5326: Licklider Transmission Protocol
- CCSDS 734.2-B-1: CCSDS Bundle Protocol Specification
- NASA ION-DTN: https://sourceforge.net/projects/ion-dtn/
- µD3TN: https://gitlab.com/d3tn/ud3tn
- FreeRTOS: https://www.freertos.org/
- STM32H7 Reference Manual: RM0433
