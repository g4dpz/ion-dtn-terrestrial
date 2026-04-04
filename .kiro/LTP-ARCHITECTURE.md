# LTP over Serial/KISS Architecture

## Overview

ION-DTN Bundle Protocol over LTP, transported via AX.25/KISS frames on 1200 baud packet radio using Mobilinkd TNC3 devices.

## Working Architecture

```
ION Bundle Protocol (bpsendfile / bprecvfile)
       ↓
LTP (Licklider Transmission Protocol)
       ↓
ionserialcla (integrated CLA — handles both TX and RX)
       ↓
AX.25 UI frames with callsign addressing
       ↓
KISS framing
       ↓
Serial USB (9600 baud to TNC)
       ↓
Mobilinkd TNC3
       ↓
RF Link (1200 baud packet, FT-817)
```

## Key Component: ionserialcla

Single integrated CLA that replaces separate `seriallso`/`seriallsi` and links directly against ION's `libltp`:

- **TX**: `ltpDequeueOutboundSegment()` → AX.25/KISS encode → serial → TNC → RF
- **RX**: RF → TNC → serial → KISS/AX.25 decode → `ltpHandleInboundSegment()`
- **Pacing**: Per-frame delay based on 1200 baud RF rate (not 9600 serial rate)
- **KISS TNC config**: Sends TX-delay and TX-tail parameters on startup
- **Debug**: Enable via `ION_SERIAL_DEBUG=1` environment variable

### Usage
```
ionserialcla <device>:<baud> <src_call> <dest_call> <remote_engine_id>
```

Launched automatically by ION via the `ltprc` span configuration.

## Node Configuration

### Node A (Source) — ipn:1
- LTP Engine ID: 1
- Callsign: G4DPZ-1
- Serial: /dev/tty.usbmodem* at 9600 baud

### Node B (Destination/Relay) — ipn:2
- LTP Engine ID: 2
- Callsign: G4DPZ-2
- Serial: /dev/tty.usbmodem* at 9600 baud

## Critical Parameters (1200 baud)

| Parameter | Value | Reason |
|-----------|-------|--------|
| LTP max segment size | **64 bytes** | TNC truncates frames >~80 bytes AX.25 info field |
| LTP aggregation size | 512 bytes | Smaller blocks = faster completion per block |
| LTP aggregation time | 2 seconds | Allow small aggregation window |
| BP protocol max payload | 384 bytes | Aligned with aggregation size |
| OWLT (one-way light time) | 30 seconds | Accounts for slow TX + report round-trip |
| Contact data rate | 10000 bytes/sec | Used by CGR only, not actual pacing |
| TX pacing | ~783ms per frame | Auto-calculated: frame_bytes × 8.33ms/byte + 100ms |
| Serial VMIN/VTIME | 1 / 10 | Allows KISS decoder to accumulate split reads |

### Frame Size Budget (64-byte LTP segment)
```
LTP segment:    64 bytes
AX.25 header:  +16 bytes (dest 7 + src 7 + ctrl 1 + PID 1)
AX.25 frame:    80 bytes
KISS overhead:  +3 bytes (FEND + cmd + FEND, plus escaping)
KISS frame:    ~83 bytes → well under TNC limit
```

## ION Config Files

### ltprc (per node)
```
1 128
a span <remote_engine> 128 128 64 512 2 'ionserialcla DEVICE:9600 <src_call> <dst_call> <remote_engine>'
s 'udplsi 0.0.0.0:1113'
```

### bprc (per node)
```
1
a scheme ipn 'ipnfw' 'ipnadminep'
a endpoint ipn:<node>.1 q
a endpoint ipn:<node>.2 q
a protocol ltp 384 100
a induct ltp <node> ltpcli
a outduct ltp <remote> ltpclo
s
```

### ionrc (per node)
```
1 <node> ''
s
a contact +1 +7200 <node> <remote> 10000
a contact +1 +7200 <remote> <node> 10000
a range +1 +7200 <node> <remote> 30
m production 1000000
m consumption 1000000
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ION_SERIAL_DEBUG` | off | Set to `1` for verbose debug logging |
| `ION_SERIAL_RF_BAUD` | 1200 | RF baud rate for TX pacing calculation |
| `ION_SERIAL_TX_DELAY_MS` | auto | Override per-frame pacing delay (ms) |
| `ION_SERIAL_TXTAIL_MS` | 300 | KISS TX-tail sent to TNC on startup |
| `ION_SERIAL_TXDELAY_MS` | 500 | KISS TX-delay sent to TNC on startup |

## Operating Procedure

```bash
# Start Node B (receiver) first
export ION_SERIAL_DEBUG=1
./opt/ion-demo/scripts/start_node_b.sh /dev/tty.usbmodemXXX

# Start receiver on Node B
cd /tmp/ion_node_b
bprecvfile ipn:2.1 1

# Start Node A (sender)
export ION_SERIAL_DEBUG=1
./opt/ion-demo/scripts/start_node_a.sh /dev/tty.usbmodemYYY

# Send a file from Node A
cd /tmp/ion_node_a
bpsendfile ipn:1.1 ipn:2.1 /path/to/file
```

## Lessons Learned

1. **TNC frame size limit**: Mobilinkd TNC3 truncates AX.25 frames with info fields >~80 bytes. 64-byte LTP segments are the safe maximum.
2. **TX pacing is essential**: The serial port (9600) is 8× faster than RF (1200). Without pacing, the TNC buffer overflows and frames are lost.
3. **Split serial reads**: The TNC delivers frames in chunks. VMIN=1/VTIME=10 and the KISS decoder's byte-at-a-time accumulation handle this correctly.
4. **tcdrain is not RF backpressure**: It only waits for UART→TNC transfer, not actual RF transmission. Software pacing is required.
