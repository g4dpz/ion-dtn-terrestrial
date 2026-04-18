# Roadmap

Short-term improvements and extensions for the ION-DTN terrestrial packet radio demo.

## 1. Investigate TNC Frame Size Limit

The Mobilinkd TNC3 currently truncates AX.25 frames with info fields above ~80 bytes. This may be a firmware-configurable parameter rather than a hard limit. Increasing it to 200+ bytes would roughly triple effective throughput.

- Check Mobilinkd TNC3 firmware source for buffer size constants
- Ask Rob Riggs (Mobilinkd developer) if this is configurable
- Test with incremental increases: 80, 96, 128, 160, 200

## 2. Delayed Delivery Demo

Demonstrate DTN store-and-forward by sending a bundle while the receiver is offline, then starting the receiver and watching automatic delivery. No code changes needed — pure ION behavior.

- Stop Node B
- Send bundle from Node A: `bpsendfile ipn:1.1 ipn:2.1 /tmp/testfile.txt`
- Wait (minutes, hours — doesn't matter)
- Start Node B and run `bprecvfile ipn:2.1 1`
- Bundle should deliver automatically from Node A's queue

## 3. Three-Node Relay (A → B → C)

Add Node C for the full store-and-forward relay demo. Node B receives from A, stores, then forwards to C on a separate contact window.

- Create `start_node_c.sh` script
- Update `node_c` config files (currently stubs)
- Configure non-overlapping contact windows in ionrc
- Node B needs two TNCs/radios, or time-multiplexed single radio

## 4. Compile Out Debug Logging

Wrap debug code in `#ifdef DEBUG` and add a `make debug` target.

- Default `make` produces a clean production binary
- `make debug` includes all debug logging

## 5. 9600 Baud Support

The FT-817 natively supports 9600 baud packet via Menu 40 (PKT RATE). Uses direct FSK on DATA port pin 4 (vs pin 5 for 1200 baud AFSK).

- Wire TNC audio to FT-817 DATA port pin 4
- Set FT-817 Menu 40 PKT RATE to 9600
- Set `ION_SERIAL_RF_BAUD=9600`
- Test larger LTP segment sizes (128, 192, 256 bytes)
- Expected throughput: ~8× improvement over 1200 baud

## 6. M17 Protocol Support (TNC4)

The Mobilinkd TNC4 supports M17 — a modern open digital radio protocol using 4FSK at 9600 bps with built-in forward error correction. M17 packet mode allows payloads up to ~798 bytes.

Changes needed in `ionserialcla`:
- Replace `build_ax25()` / `strip_ax25()` with M17 packet framing
- Use M17 callsign encoding (base-40) instead of AX.25 shifted ASCII
- Update KISS command byte for M17 packet mode
- LTP and ION layers remain unchanged

## 7. Refactor to Transport-Agnostic KISS CLA (`kisscla`)

Refactor `ionserialcla` into a generic KISS CLA with pluggable transport backends. The LTP integration, AX.25/KISS framing, TX pacing, and half-duplex logic remain unchanged — only the byte transport layer becomes pluggable.

Architecture:
```
kisscla (main)
  ├── LTP integration (ltpDequeue / ltpHandle — unchanged)
  ├── AX.25/KISS framing (build / strip / encode / decode — unchanged)
  ├── TX pacing + half-duplex logic (unchanged)
  └── Transport backend (pluggable)
       ├── serial:// — physical serial port (current ionserialcla)
       ├── tcp://    — network TNC (Direwolf, kissnetd)
       └── pty://    — pseudo-terminal (for simulator)
```

Usage in ltprc:
```
a span 2 128 128 64 256 2 'kisscla serial:///dev/ttyACM0:9600?rf=1200 G4DPZ-1 G4DPZ-2'
a span 2 128 128 64 256 2 'kisscla tcp://192.168.1.10:8001?rf=9600 G4DPZ-1 G4DPZ-2'
```

The `rf=` parameter specifies the RF baud rate for TX pacing, replacing the `ION_SERIAL_RF_BAUD` env var.

Benefits:
- TCP transport enables use with Direwolf software TNC and network-attached TNCs
- PTY transport simplifies the link simulator (no socat needed)
- Same half-duplex pacing and OWLT behaviour across all transports
- Could be proposed as a contributed CLA to the ION-DTN project
- M17 framing (item 7) becomes another framing option alongside AX.25

Steps:
- Define a transport interface: `open()`, `read_bytes()`, `write_bytes()`, `close()`
- Extract serial code into `transport_serial.c`
- Add `transport_tcp.c` for network TNCs
- Add `transport_pty.c` for simulator
- Parse transport URI from first CLI argument
- Keep `ionserialcla` as a compatibility alias

## 8. Production Ground Station: IC-9100 + KPC-9612+

Upgrade from the FT-817 + Mobilinkd TNC3 bench setup to a production-capable ground station using an Icom IC-9100 transceiver and Kantronics KPC-9612+ TNC.

### IC-9100 Benefits
- 100W on HF/6m/2m, 75W on 70cm (vs 5W FT-817)
- Optional 1.2 GHz with UX-9100 module (higher-bandwidth CubeSat links)
- CI-V computer control for automated frequency/mode switching during scheduled contacts
- Dual independent receivers for simultaneous monitoring
- 9600 baud packet via DATA2 socket (pin 3, direct FSK)

### KPC-9612+ Benefits
- Dual-port: 1200 baud (Port 1) + 9600 baud (Port 2) simultaneously
- Large frame buffer — full 256-byte AX.25 info fields (vs 64-byte TNC3 limit)
- Speeds up to 38.4 kbps
- Proven reliability in packet networks

### Steps
- Obtain KPC-9612+ (or current 9612XE) and IC-9100
- Build DATA2-to-KPC-9612+ cable (9600 baud direct FSK pinout)
- Add KISS mode initialization to start script (`KISS ON` + `RESTART` command sequence)
- Test with LTP segment sizes: 128, 192, 240 bytes
- Test large file transfers that failed on TNC3
- Configure CI-V control for automated radio setup during contact windows
- RS-232 to USB adapter needed for Mac connection
