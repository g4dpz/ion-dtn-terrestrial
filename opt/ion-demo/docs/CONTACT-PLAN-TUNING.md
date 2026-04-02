# Contact Plan Data Rate Tuning for Serial RF Links

## The Problem

ION's contact plan `a contact` data rate parameter serves two purposes:
1. CGR (Contact Graph Routing) uses it to decide if a bundle fits within the contact window
2. `udplso` uses it to pace segment delivery (ION 4.x reads rate from contact plan)

For our serial RF CLA architecture, ION hands segments to `udplso` via UDP, which forwards them to `serialcla`, which writes them to the serial port. There's no backpressure from the serial link back to ION — UDP is fire-and-forget.

## Rate Calculation

### Raw Link Capacity

```
RF baud rate:           1200 baud (AFSK)
Bits per symbol:        1
Raw bit rate:           1200 bps
Bytes per second:       150 B/s
```

### Protocol Overhead

Each LTP segment gets wrapped in AX.25 + KISS:
```
LTP segment:            up to 1400 bytes (max_segment_size in ltprc)
AX.25 header:           16 bytes (7 dest + 7 src + control + PID)
KISS framing:           ~4 bytes (FEND + cmd + FEND + escaping)
Total per segment:      ~1420 bytes on air

Overhead ratio:         1420/1400 = 1.014 (1.4% overhead)
```

Half-duplex turnaround adds dead time:
```
TNC TX delay:           300ms (TXDelay setting)
TNC TX tail:            50ms
Turnaround time:        ~500ms per transmission burst
```

### Effective Throughput

```
Segment on-air time:    1420 bytes × 8 bits / 1200 bps = 9.47 seconds
Turnaround overhead:    0.5 seconds
Effective per segment:  ~10 seconds

Effective data rate:    1400 bytes / 10 seconds = 140 bytes/sec
```

### Simulated Propagation Delay Impact

With Earth-Moon OWLT (1.3s simulated):
```
TX delay:               1.3 seconds added before each segment
RX delay:               1.3 seconds added before forwarding to ION
Effective per segment:  10s + 1.3s = 11.3 seconds
Effective data rate:    1400 / 11.3 = ~124 bytes/sec
```

## Contact Plan Configuration

### The Balancing Act

| Data Rate | CGR Behavior | LTP Behavior | Result |
|-----------|-------------|--------------|--------|
| 150 B/s | Rejects large bundles (not enough capacity) | Paces well | Bundles stuck in queue |
| 1200 B/s | Accepts most bundles | Slight buffer buildup | Works for small files |
| 10000 B/s | Accepts all bundles | Moderate buffer buildup | Good compromise |
| 100000 B/s | Accepts everything | Massive buffer overflow | Retransmission storms |

### Recommended Settings

For 1200 baud half-duplex with Mobilinkd TNC3:

```
# ionrc - contact plan
a contact +1 +7200 1 2 10000    # 10 KB/s — CGR headroom
a contact +1 +7200 2 1 10000    # bidirectional

# Range (OWLT) — affects LTP retransmission timers
a range +1 +7200 1 2 15         # 15 seconds OWLT
```

### Why Not Match the Actual Rate?

Setting the contact plan to the actual 150 B/s causes CGR to reject bundles larger than `150 × remaining_window_seconds`. A 60KB file needs 400 seconds of capacity. If the contact window has been open for a while, CGR may calculate insufficient remaining capacity and refuse to forward.

Setting it higher (10000) gives CGR plenty of headroom. The actual pacing happens at the serial link layer — the TNC can only transmit at 1200 baud regardless of how fast ION feeds it.

### LTP Retransmission Timer

The OWLT range affects when ION considers a segment lost:

```
Retransmission timeout ≈ 2 × OWLT × (1 + margin)
With OWLT = 15s:  timeout ≈ 60-90 seconds
```

This must be longer than the actual round trip:
```
Actual round trip = TX time + propagation delay + RX processing + report TX time + propagation delay
                  = 10s + 1.3s + 0.5s + 2s + 1.3s = ~15 seconds (small file)
                  = 60s + 1.3s + 0.5s + 2s + 1.3s = ~65 seconds (large file, many segments)
```

For large files, increase OWLT to avoid premature retransmissions.

## Future Improvement

A proper ION-integrated serial LSO (linking against `libltp`) would read segments directly from ION's SDR, providing natural backpressure. It would only pull the next segment after the current one finishes transmitting. This eliminates the buffer mismatch entirely and allows the contact plan data rate to match the actual link capacity.
