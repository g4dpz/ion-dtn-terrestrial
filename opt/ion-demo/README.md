# ION-DTN Terrestrial Packet Radio Demo

DTN file transfer over 1200 baud UHF amateur packet radio using ION-DTN, LTP, and AX.25/KISS via Mobilinkd TNC3 devices.

## Architecture

```
bpsendfile → ION BP → LTP → ionserialcla → AX.25/KISS → TNC → RF
RF → TNC → AX.25/KISS → ionserialcla → LTP → ION BP → bprecvfile
```

## Directory Structure

```
opt/ion-demo/
├── cla/              # ionserialcla — integrated LTP/AX.25/KISS CLA
├── config/           # ION configuration files per node
│   ├── node_a/       # ipn:1, G4DPZ-1
│   ├── node_b/       # ipn:2, G4DPZ-2
│   └── node_c/       # ipn:3, G4DPZ-3 (future)
├── scripts/          # Start/stop scripts
├── docs/             # Hardware setup guides
├── logs/             # ION log output
└── data/             # Bundle storage
```

## Hardware

- 2× Mobilinkd TNC3 (USB KISS TNC)
- 2× Yaesu FT-817 (or any 1200 baud FM transceiver)
- 2× Mac/Linux hosts with ION-DTN compiled
- USB cables, TNC-to-radio audio cables

## Quick Start

```bash
# Build the CLA
cd opt/ion-demo/cla
make

# Start Node B (receiver) first
export ION_SERIAL_DEBUG=1
./scripts/start_node_b.sh /dev/tty.usbmodemXXX

# On Node B, start the file receiver
cd /tmp/ion_node_b
bprecvfile ipn:2.1 1

# Start Node A (sender)
export ION_SERIAL_DEBUG=1
./scripts/start_node_a.sh /dev/tty.usbmodemYYY

# Send a file
cd /tmp/ion_node_a
bpsendfile ipn:1.1 ipn:2.1 /path/to/file

# Stop
ionstop
```

## Key Parameters (1200 baud)

- LTP segment size: 64 bytes (TNC limit)
- TX pacing: ~783ms per frame (auto-calculated from RF baud)
- OWLT: 30 seconds
- Aggregation: 512 bytes per LTP block

## Callsigns

Every AX.25 frame carries source and destination callsigns (G4DPZ-1, G4DPZ-2) in the header, satisfying amateur radio identification requirements.
