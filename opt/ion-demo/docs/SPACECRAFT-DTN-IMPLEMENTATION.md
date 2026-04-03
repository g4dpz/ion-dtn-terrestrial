# Spacecraft DTN Implementation with ION-DTN

## How NASA Implements ION-DTN on Spacecraft

### Flight Computer Environment

NASA spacecraft run ION-DTN on flight computers with a full POSIX-compatible operating system. The typical configuration:

- Processor: RAD750 (PowerPC, radiation-hardened, used on Mars rovers and orbiters), LEON3/4 (SPARC, used on ESA missions), or BAE RAD5545 (newer missions)
- RTOS: VxWorks (Wind River) — the standard for NASA flight software
- Memory: 256 MB RAM typical, radiation-hardened SRAM or SDRAM
- Storage: Radiation-hardened flash or MRAM for persistent bundle storage

ION has a lightweight task build mode (`ION_LWT`) designed specifically for VxWorks. In this mode, all ION daemons run as tasks within a single VxWorks address space rather than separate Unix processes. They share memory directly instead of using POSIX shared memory segments. This is more efficient and avoids the overhead of inter-process communication.

### ION Task Architecture on VxWorks

```
┌─────────────────────────────────────────────┐
│           VxWorks Address Space              │
│                                             │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ rfxclock │ │ bpclock  │ │ ltpclock │   │
│  │ (task)   │ │ (task)   │ │ (task)   │   │
│  └──────────┘ └──────────┘ └──────────┘   │
│                                             │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ ipnfw    │ │ ltpcli   │ │ ltpclo   │   │
│  │ (task)   │ │ (task)   │ │ (task)   │   │
│  └──────────┘ └──────────┘ └──────────┘   │
│                                             │
│  ┌──────────────────────────────────────┐   │
│  │     SDR (Simple Data Recorder)       │   │
│  │     Persistent bundle storage        │   │
│  │     Mapped to rad-hard flash/MRAM    │   │
│  └──────────────────────────────────────┘   │
│                                             │
│  ┌──────────────────────────────────────┐   │
│  │     CLA (Convergence Layer Adapter)  │   │
│  │     Connects to spacecraft radio     │   │
│  │     via CCSDS protocols              │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### Convergence Layer: CCSDS Protocols

On spacecraft, ION does not use UDP/TCP or serial KISS. Instead, the CLA interfaces with the spacecraft radio subsystem via CCSDS (Consultative Committee for Space Data Systems) protocols. The radio handles modulation, coding, and RF — ION just provides data frames.

The two main CCSDS link protocols used with ION:

1. CCSDS Proximity-1 — for short-range relay links (surface-to-orbit)
2. CCSDS Space Link Extension (SLE) — for deep space links (spacecraft-to-Earth)

### Contact Plan Management

Contact plans are uploaded from the ground via command sequences. For deep space missions, the contact plan comes from the Deep Space Network (DSN) scheduling system, which knows precisely when each DSN antenna has line-of-sight to the spacecraft.

For relay scenarios (e.g., Mars rover → Mars orbiter → Earth), both the rover and orbiter have contact plans that define when the relay link is available (based on orbital geometry).

### Flight Heritage

ION-DTN has been demonstrated in space:
- DINET experiment on the International Space Station
- Various NASA technology demonstrations
- LunaNet architecture for Artemis lunar communications is based on DTN

### CubeSat Approach

For CubeSats, the approach is simpler:
- Linux on a capable processor (Raspberry Pi CM, Xilinx Zynq, or similar)
- ION runs as standard Linux processes (not VxWorks tasks)
- CLA connects to a COTS UHF radio via UART/KISS (as in our ground demo)
- Several CubeSat missions have flown Linux-based computers running ION

---

## CCSDS Proximity-1 Protocol

### Overview

Proximity-1 (CCSDS 211.0-B-6) is a space data link protocol designed for short-range communications between spacecraft in close proximity — typically between a surface asset (lander, rover) and an orbiting relay spacecraft.

It was developed specifically for Mars relay operations, where rovers on the surface communicate with orbiters overhead during brief pass windows. This is directly analogous to our ground demo — scheduled contact windows, store-and-forward, half-duplex operation.

### Key Characteristics

- Designed for relay links (surface-to-orbit, orbit-to-orbit)
- Short range: typically under 10,000 km
- Data rates: 8 kbps to 2 Mbps (configurable)
- Half-duplex or full-duplex operation
- Frame-based protocol with sequence numbers
- Built-in link establishment and teardown procedures
- Supports both reliable (ARQ) and unreliable delivery

### Protocol Layers

```
┌─────────────────────────────┐
│  DTN Bundle Protocol (BP)   │
├─────────────────────────────┤
│  LTP or other CL protocol   │
├─────────────────────────────┤
│  Proximity-1 Data Link      │  ← This layer
│  (Transfer Frames)          │
├─────────────────────────────┤
│  Proximity-1 Physical       │
│  (Coding, Modulation)       │
├─────────────────────────────┤
│  RF (UHF typically)         │
└─────────────────────────────┘
```

### Frame Structure

A Proximity-1 Transfer Frame contains:

```
┌──────────────────────────────────────────────┐
│ Attached Sync Marker (ASM): 32 bits          │
├──────────────────────────────────────────────┤
│ Frame Header: 40 bits                        │
│  - Version (3 bits)                          │
│  - Spacecraft ID (10 bits)                   │
│  - Frame Type (1 bit): data or control       │
│  - Sequence Count (24 bits)                  │
│  - Frame Length (10 bits)                     │
├──────────────────────────────────────────────┤
│ Data Field: variable length                  │
│  (LTP segments or BP bundles)                │
├──────────────────────────────────────────────┤
│ Frame Error Control: 32 bits (CRC-32)        │
└──────────────────────────────────────────────┘
```

### Operating Modes

1. Hailing: One spacecraft transmits a hailing signal, the other responds. Establishes the link.

2. Data Transfer: Frames are exchanged. In reliable mode, each frame is acknowledged. In unreliable mode, frames are sent without acknowledgment (LTP handles reliability at a higher layer).

3. Session Management: The protocol manages link establishment, maintenance, and teardown. This maps to ION's contact plan — the link is established at the start of a contact window and torn down at the end.

### Comparison with Our AX.25/KISS Approach

| Aspect | Proximity-1 | Our AX.25/KISS |
|--------|-------------|----------------|
| Designed for | Space relay links | Amateur packet radio |
| Data rates | 8 kbps - 2 Mbps | 1200 - 9600 baud |
| Addressing | Spacecraft ID (10 bit) | Callsign (6 char + SSID) |
| Frame sync | ASM (32 bit) | KISS FEND (8 bit) |
| Error detection | CRC-32 | None (LTP handles it) |
| Link management | Built-in hailing | Manual/scheduled |
| Duplex | Half or full | Half (simplex radio) |
| Reliability | Optional ARQ | LTP handles it |

### Relevance to Our Project

Our AX.25/KISS CLA serves the same role as a Proximity-1 CLA would on a real spacecraft. The protocol stack is:

```
Ground Demo:    BP → LTP → AX.25/KISS → serial → TNC → UHF radio
Spacecraft:     BP → LTP → Proximity-1 → radio interface → UHF radio
```

The BP and LTP layers are identical. Only the link layer changes. This means:
- Our BPv7 and LTP implementations transfer directly to a spacecraft
- Only the CLA needs to be replaced (AX.25/KISS → Proximity-1)
- The contact plan, routing, and store-and-forward logic are the same

### Mars Relay Architecture

The operational Mars relay architecture uses Proximity-1:

```
Mars Rover (surface)
    │
    │ Proximity-1 UHF relay link
    │ (during orbiter pass, ~10 min window)
    │
Mars Orbiter (MRO, MAVEN, TGO)
    │
    │ CCSDS Space Link (X-band or Ka-band)
    │ (during DSN contact, ~8 hour window)
    │
Deep Space Network (Earth)
    │
    │ Internet
    │
Mission Operations Center
```

Each hop is a DTN store-and-forward link. The rover queues data, transmits during the orbiter pass, the orbiter stores it and transmits during its DSN contact. This is exactly what our three-node demo architecture was designed to demonstrate — just with amateur radio instead of space-qualified hardware.

## References

- CCSDS 211.0-B-6: Proximity-1 Space Link Protocol — Data Link Layer
- CCSDS 211.1-B-4: Proximity-1 Space Link Protocol — Physical Layer
- CCSDS 211.2-B-3: Proximity-1 Space Link Protocol — Coding and Synchronization
- CCSDS 734.2-B-1: CCSDS Bundle Protocol Specification
- ION-DTN Design and Operations Guide (NASA JPL)
- LunaNet Interoperability Specification (NASA)
