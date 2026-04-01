# TNC3 APRS Test - C Implementation

Native C implementation for APRS and LTP communication with TNC3.

## Build

```bash
cd opt/tnc3-ax25-test/c
make
```

## APRS Usage

### Send APRS Position Beacons
```bash
./aprs_sender /dev/tty.usbmodem123 G4DPZ-1 APRS 51.5074 -0.1278 "London Test" 30
```

### Receive APRS Packets
```bash
./aprs_receiver /dev/tty.usbmodem456 G4DPZ-2
```

## LTP Test Usage

### Send LTP Segments
```bash
./ltp_test_sender /dev/tty.usbmodem123 "Hello LTP" 5
```

Parameters:
- Device path
- Message to send
- Interval in seconds (optional, default: 5)

### Receive LTP Segments
```bash
./ltp_test_receiver /dev/tty.usbmodem456
```

Parameters:
- Device path

## Features

- KISS protocol encoding/decoding
- AX.25 frame creation/parsing (for APRS)
- LTP segment encoding/decoding (simplified)
- APRS position beacon generation
- APRS packet type detection
- No external dependencies (pure C)
