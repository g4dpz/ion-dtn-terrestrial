# LTP over Serial/KISS Architecture

## Overview

This document describes the updated architecture using LTP (Licklider Transmission Protocol) directly over serial KISS interfaces, replacing the original IP-over-AX.25 approach.

## Architecture Change

### Original Approach (IP-over-AX.25)
```
ION Bundle Protocol
       ↓
UDP/TCP Convergence Layer
       ↓
IP-over-AX.25
       ↓
AX.25 (kissattach)
       ↓
KISS TNC
       ↓
RF Link
```

### New Approach (LTP over Serial)
```
ION Bundle Protocol
       ↓
LTP Convergence Layer (ltpclo/ltpcli)
       ↓
Serial Device (/dev/ttyUSB0)
       ↓
KISS TNC (Mobilinkd TNC3)
       ↓
RF Link (FT-817)
```

## Advantages of LTP over Serial

1. **Simpler Stack**: Eliminates AX.25 and IP layers
2. **Better for DTN**: LTP is designed for delay-tolerant links
3. **No IP Configuration**: No need for AMPRNet addresses or IP routing
4. **Direct Control**: ION controls the serial device directly
5. **Efficient**: Less protocol overhead than IP/UDP stack

## ION Configuration Changes

### Node Configuration

**Node A (Source) - dtn://g4dpz-1/**
- LTP Engine ID: 1
- Serial Device: /dev/ttyUSB0
- Baud Rate: 9600 (to TNC)
- RF Data Rate: 1200 baud (over-the-air)

**Node B (Relay) - dtn://g4dpz-2/**
- LTP Engine ID: 2
- Serial Device: /dev/ttyUSB0
- Baud Rate: 9600 (to TNC)
- RF Data Rate: 1200 baud (over-the-air)

**Node C (Destination) - dtn://g4dpz-3/**
- LTP Engine ID: 3
- Serial Device: /dev/ttyUSB0
- Baud Rate: 9600 (to TNC)
- RF Data Rate: 1200 baud (over-the-air)

### ION Configuration Files

**ionconfig** - Same as before (node identity, memory)

**ionrc** - Contact plan (same concept, different engine IDs)
```
# Contact from Node 1 to Node 2
a contact +0 +3600 1 2 100000
a range +0 +3600 1 2 1
```

**bprc** - Bundle protocol (use DTN scheme)
```
a scheme dtn 'dtn2fw' 'dtn2adminep'
a endpoint dtn://g4dpz-1/admin q
a endpoint dtn://g4dpz-1/demo q

# LTP convergence layer
a protocol ltp 1400 100
a induct ltp 1 ltpcli
a outduct ltp 2 ltpclo
```

**ltprc** - LTP configuration (NEW - replaces IP configuration)
```
# Initialize LTP engine
1 32

# LTP span to Node 2 (AX.25/KISS serial with callsign ID)
a span 2 32 32 1400 10000 1 'seriallso /dev/ttyUSB0:9600 G4DPZ-1 G4DPZ-2'
```

## Implementation Steps

### Phase 1: TNC Initialization (COMPLETE)
- ✅ Configure serial port
- ✅ Initialize KISS mode (Mobilinkd TNC3)
- ✅ Verify device connectivity

### Phase 2: ION with LTP (NEXT)
- [ ] Create ltprc configuration files
- [ ] Update bprc to use LTP convergence layer
- [ ] Configure LTP spans between nodes
- [ ] Test LTP connectivity

### Phase 3: Bundle Transfer
- [ ] Send test bundles using bpsendfile
- [ ] Verify bundle reception using bprecvfile
- [ ] Test store-and-forward through relay node

## Key Differences from IP-over-AX.25

| Aspect | IP-over-AX.25 | LTP over Serial |
|--------|---------------|-----------------|
| Network Layer | IP | None (direct serial) |
| Convergence Layer | UDP/TCP | LTP |
| Address Type | IP addresses | LTP engine IDs |
| Interface Setup | kissattach + ifconfig | Direct serial open |
| Routing | IP routing | ION contact graph routing |
| Tools Needed | ax25-tools, kissattach | ION only |

## Configuration File Updates Needed

1. **bprc** - Change from UDP to LTP protocol
2. **ltprc** - New file for LTP configuration
3. **ionrc** - Update engine IDs (not IP addresses)
4. **Remove**: No need for IP addresses or AX.25 configuration

## Testing Approach

1. **Single Link Test**: Node A → Node B via LTP
2. **Loopback Test**: Send bundle from A, receive at A
3. **Two-Hop Test**: A → B → C with store-and-forward
4. **Scheduled Contacts**: Test with contact plan windows

## References

- ION LTP Documentation: ION.pdf Section 3.4
- LTP RFC: RFC 5326
- ION Configuration Guide: ionconfig(5), ltprc(5)
