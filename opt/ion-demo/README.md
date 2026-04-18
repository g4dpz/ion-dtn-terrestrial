# ION-DTN Terrestrial Packet Radio Demo

DTN file transfer over 1200 baud VHF amateur packet radio using NASA ION-DTN, LTP, and AX.25/KISS via Mobilinkd TNC3 devices. An AMSAT-UK project by David Johnson, G4DPZ.

## Architecture

```
bpsendfile → ION BP → LTP → ionserialcla → AX.25/KISS → TNC → RF
RF → TNC → AX.25/KISS → ionserialcla → LTP → ION BP → bprecvfile
```

The `ionserialcla` Convergence Layer Adapter bridges ION's LTP engine to the amateur radio link. It replaces ION's standard UDP-based `udplso`/`udplsi` pair with a serial/KISS/AX.25 path.

## Station Identification (OFCOM Compliance)

Every LTP segment transmitted over the air is wrapped in an AX.25 UI frame containing the operator's callsign in the source address field. This happens automatically in `ionserialcla` — the callsigns are specified as command-line arguments and embedded in every transmitted frame.

The callsign configuration is in each node's `ltprc` file:

```
a span <remote_engine> ... 'ionserialcla <device> <src_callsign> <dst_callsign> <remote_engine_id>'
```

For example, on node A (G4DPZ-1) talking to node B (G4DPZ-2):
```
a span 2 128 128 64 256 2 'ionserialcla /dev/ttyACM0:9600 G4DPZ-1 G4DPZ-2 2'
```

- `G4DPZ-1` — source callsign (this station's callsign, appears in every AX.25 frame)
- `G4DPZ-2` — destination callsign (remote station)

On node B, the callsigns are reversed:
```
a span 1 128 128 64 256 2 'ionserialcla /dev/ttyACM0:9600 G4DPZ-2 G4DPZ-1 1'
```

For multi-station networks, each span has its own `ionserialcla` instance with the appropriate callsigns:
```
# Node A (G4DPZ-1) with spans to B and C:
a span 2 128 128 64 256 2 'ionserialcla /dev/ttyACM0:9600 G4DPZ-1 G4DPZ-2 2'
a span 3 128 128 64 256 2 'ionserialcla /dev/ttyACM1:9600 G4DPZ-1 G4DPZ-3 3'

# Node B (G4DPZ-2) with span to A:
a span 1 128 128 64 256 2 'ionserialcla /dev/ttyACM0:9600 G4DPZ-2 G4DPZ-1 1'

# Node C (G4DPZ-3) with span to A:
a span 1 128 128 64 256 2 'ionserialcla /dev/ttyACM0:9600 G4DPZ-3 G4DPZ-1 1'
```

Each station uses its own callsign as the source. Any packet radio monitor will see the callsigns in the AX.25 header of every frame.

## Directory Structure

```
opt/ion-demo/
├── cla/              # ionserialcla — integrated LTP/AX.25/KISS CLA
│   ├── ionserialcla.c
│   └── Makefile
├── config/           # ION configuration files per node
│   ├── node_a/       # ipn:1, G4DPZ-1 (laptop)
│   │   ├── ionrc
│   │   ├── ionconfig
│   │   ├── ltprc      # ← callsigns configured here
│   │   ├── bprc
│   │   └── ipnrc
│   ├── node_b/       # ipn:2, G4DPZ-2 (Raspberry Pi)
│   └── node_c/       # ipn:3, G4DPZ-3 (future)
├── scripts/          # Start/stop scripts
├── docs/             # Hardware setup guides
└── data/             # Bundle storage
```

## Hardware

- Mobilinkd TNC3 (USB KISS TNC, 1200 baud AFSK)
- Yaesu FT-817 (or any 1200 baud FM transceiver)
- Linux host with ION-DTN compiled (Ubuntu laptop, Raspberry Pi)
- USB cable from TNC to host

## Prerequisites

- ION-DTN built and installed (`make && sudo make install && sudo ldconfig`)
- `ionserialcla` built against the same ION version (see Building below)

## Building the CLA

The CLA must be built against the same ION source tree that was installed:

```bash
cd opt/ion-demo/cla
gcc -Wall -Wextra -O2 \
  -I$HOME/dev/ION-DTN/ici/include \
  -I$HOME/dev/ION-DTN/ltp/include \
  -I$HOME/dev/ION-DTN/ltp/library \
  -o ionserialcla ionserialcla.c \
  -L/usr/local/lib -lltp -lici -lpthread
```

Or using the Makefile:
```bash
make ION_DIR=$HOME/dev/ION-DTN
```

The CLA links against ION's private `ltpP.h` header for direct LTP segment access. If ION is rebuilt, the CLA must be rebuilt too.

## Quick Start

### Node B (receiver) — Raspberry Pi

```bash
export PATH=$PATH:~/dev/ion-dtn-terrestrial/opt/ion-demo/cla
cd ~/dev/ion-dtn-terrestrial/opt/ion-demo/config/node_b

# Edit ltprc to set your serial device
sed -i 's|DEVICE|/dev/ttyACM0|' ltprc

# Clean start
killm
rm -rf /tmp/ion_node_b && mkdir -p /tmp/ion_node_b

# Start ION
ionadmin ionrc
ionsecadmin <<< "1"
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc

# Verify ionserialcla is running
ps aux | grep ionserialcla

# Start bundle receiver
bpsink ipn:2.1
```

### Node A (sender) — Laptop

```bash
export PATH=$PATH:~/dev/ion-dtn-terrestrial/opt/ion-demo/cla
cd ~/dev/ion-dtn-terrestrial/opt/ion-demo/config/node_a

# Edit ltprc to set your serial device
sed -i 's|DEVICE|/dev/ttyACM0|' ltprc

# Clean start
killm
rm -rf /tmp/ion_node_a && mkdir -p /tmp/ion_node_a

# Start ION
ionadmin ionrc
ionsecadmin <<< "1"
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc

# Verify ionserialcla is running
ps aux | grep ionserialcla

# Send a text message
bpsource ipn:2.1 "Hello from ION over amateur radio"

# Send a file
bpsendfile ipn:1.2 ipn:2.2 /path/to/file
```

### Receiving files on Node B

```bash
cd /tmp/ion_node_b
bprecvfile ipn:2.2
```

## Key Parameters (1200 baud)

| Parameter | Value | Notes |
|-----------|-------|-------|
| LTP segment size | 64 bytes | Fits within AX.25 info field |
| Aggregation size | 256 bytes | Small blocks for faster recovery |
| Aggregation time | 2 seconds | |
| TX pacing | Auto-calculated | Based on RF baud rate (1200) |
| Contact window | 7200 seconds | 2 hours default, adjust in ionrc |

## Debugging

Enable debug logging for the CLA:
```bash
export ION_SERIAL_DEBUG=1
```

Check ION logs:
```bash
tail -f ion.log
```

Check CLA status:
```bash
ps aux | grep ionserialcla
grep -i serial ion.log
grep -i error ion.log | tail -10
```

## Troubleshooting

- `ionserialcla: command not found` — add the CLA directory to PATH
- `Assertion failed (snooze_usecs)` — version mismatch between CLA headers and installed ION libraries; rebuild both from the same source
- `Can't execute new process (ionserialcla)` — PATH not set when ION was started
- No response from receiver — check `ionserialcla` is running on both sides (`ps aux | grep ionserialcla`)
- Contact window expired — check `ionrc` contact times, extend with `ionadmin`

## Licence

Apache License 2.0. See [LICENSE](../../LICENSE).

Copyright 2026 AMSAT-UK.
