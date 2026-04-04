# ION Configuration Files

Per-node ION-DTN configuration for LTP over serial AX.25/KISS.

## Nodes

- **node_a/**: ipn:1, callsign G4DPZ-1 (source)
- **node_b/**: ipn:2, callsign G4DPZ-2 (destination/relay)
- **node_c/**: ipn:3, callsign G4DPZ-3 (future)

## Files Per Node

| File | Purpose |
|------|---------|
| `ionrc` | Node identity, contacts, ranges |
| `ltprc` | LTP spans — launches ionserialcla |
| `bprc` | Bundle protocol, endpoints, LTP induct/outduct |
| `ipnrc` | IPN routing plans |
| `ionconfig` | ION memory settings (if present) |

## Key Settings

The `ltprc` span line controls the serial CLA:
```
a span <engine> 128 128 64 512 2 'ionserialcla DEVICE:9600 <src_call> <dst_call> <engine>'
```

- `64` = max LTP segment size (must stay ≤64 for Mobilinkd TNC3 at 1200 baud)
- `512` = aggregation size limit
- `DEVICE` is replaced by the start script with the actual serial port path

## Customization

Update callsigns in `ltprc` to match your amateur radio license. Update the serial device path in the start script or ltprc.
