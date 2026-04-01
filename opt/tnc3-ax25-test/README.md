# TNC3 AX.25 Ping Test

Minimal test setup for two TNC3 devices communicating over AX.25 via serial/RF.

## Hardware Requirements

- 2x Mobilinkd TNC3
- 2x Yaesu FT-817 (or compatible radios)
- 2x Raspberry Pi (or any Linux host)
- 2x Dummy loads (50Ω, 10W minimum)
- USB and audio cables

## Quick Start

### Sender Node
```bash
cd opt/tnc3-ax25-test
sudo ./setup_ax25.sh sender /dev/ttyUSB0 CALL1-1
./ping_sender.sh CALL2-1
```

### Receiver Node
```bash
cd opt/tnc3-ax25-test
sudo ./setup_ax25.sh receiver /dev/ttyUSB0 CALL2-1
./ping_receiver.sh
```

## Configuration

Edit `config.sh` to set your callsigns and device paths.

## Testing

The sender transmits ping packets every 5 seconds. The receiver logs received packets.
