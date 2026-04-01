# Mobilinkd TNC3 and Yaesu FT-817 Setup Guide

## Overview

This guide provides step-by-step instructions for configuring the Mobilinkd TNC3 and Yaesu FT-817 for KISS mode packet radio operation with ION-DTN.

## Hardware Components

### Mobilinkd TNC3
- Bluetooth/USB KISS TNC
- 1200 baud AFSK packet radio
- Rechargeable Li-ion battery (can operate standalone or USB-powered)
- USB interface for host connection
- 3.5mm audio jack for radio connection
- LED indicators: Power (green), TX (red), RX (yellow)

### Yaesu FT-817 (or FT-817ND)
- HF/VHF/UHF all-mode portable transceiver
- 5W output power (QRP)
- 6-pin mini-DIN data port
- Internal battery pack or 13.8V DC external power
- Frequency coverage: HF + 6m/2m/70cm

## Required Cables

1. **USB Cable**: Type-A to Micro-USB (TNC3 to Raspberry Pi)
2. **Data Cable**: 3.5mm (TNC3) to 6-pin mini-DIN (FT-817 DATA jack)
   - Available from Mobilinkd or build your own
   - Pinout: Audio in/out, PTT, Ground

## Initial Setup

### 1. FT-817 Configuration

**Set Frequency:**
1. Power on FT-817
2. Use VFO knob or keypad to set frequency
3. Recommended: 433.500 MHz (UHF) or 145.010 MHz (VHF)
4. Ensure all nodes use the same frequency

**Set Mode:**
1. Press [MODE] button
2. Select FM (not USB/LSB/CW)
3. Press [MODE] again to confirm

**Set Power Level:**
1. For bench testing: 2.5W or 5W
2. Press and hold [F] then press [PWR]
3. Rotate VFO knob to select power level
4. Press [F] to save

**Configure Packet Settings:**

**Set Power Level:**
1. Press `[FUNC]` then `[PWR]`
2. Rotate main dial to select power level:
   - `L1` (0.5W) for bench testing with dummy load
   - `L2` (1W) for bench testing
   - `L3` (2.5W) for field testing
   - `HI` (5W) for maximum range

**Configure Menu Settings:**
1. Press `[FUNC]` then `[SET]` to enter menu mode
2. Navigate to Menu 14 (DIG MODE): Set to `USER-U`
3. Navigate to Menu 15 (DIG SHIFT): Set to `0` Hz
4. Navigate to Menu 24 (PKT RATE): Set to `1200` baud
5. Press `[FUNC]` then `[SET]` to exit menu mode

**Set Squelch:**
1. Press `[SQL]` button
2. Rotate main dial to set squelch level
3. Typical setting: 2-3 (just above noise floor)

### 2. Mobilinkd TNC3 Configuration

**Charge Battery:**
1. Connect TNC3 to USB power source
2. Red LED indicates charging
3. Green LED indicates fully charged
4. Charge for 2-3 hours before first use

**Connect to Raspberry Pi:**
1. Connect TNC3 to Raspberry Pi via USB cable
2. TNC3 will power on automatically
3. Blue LED indicates power
4. Device will appear as /dev/ttyUSB0 or /dev/ttyACM0

**Connect Audio Cable:**
1. Connect 3.5mm plug to TNC3 audio jack
2. Connect 6-pin mini-DIN plug to FT-817 DATA port (rear panel)
3. Ensure connections are secure

### 3. TNC3 Configuration via Mobilinkd App (Optional)

For advanced configuration, use the Mobilinkd Configuration app:

**Android/iOS App:**
1. Install "Mobilinkd TNC3 Config" app
2. Enable Bluetooth on phone/tablet
3. Power on TNC3
4. Connect to TNC3 via Bluetooth (name: "Mobilinkd TNC3")
5. Configure settings:
   - TX Delay: 30 (300ms)
   - Persistence: 63 (25%)
   - Slot Time: 10 (100ms)
   - TX Tail: 5 (50ms)
   - Duplex: Half
   - Input Volume: Adjust for FT-817 (typically 4-6)
   - Output Volume: Adjust for FT-817 (typically 4-6)

**Note:** These settings can also be configured via KISS commands from the host.

## Running the Initialization Script

Once hardware is connected, run the init script:

```bash
# Make script executable
chmod +x /opt/ion-demo/scripts/init_tnc.sh

# Run script (no sudo needed for serial port configuration)
./opt/ion-demo/scripts/init_tnc.sh /dev/ttyUSB0 9600
```

The script will:
1. Configure serial port (9600 baud, 8N1, raw mode)
2. Send KISS initialization frames
3. Configure TNC parameters (TXDelay, Persistence, SlotTime)
4. Verify TNC connection

## Testing the Setup

### Audio Level Test

**Transmit Test:**
1. Set FT-817 to low power (L1 or L2)
2. Connect dummy load to FT-817 antenna port
3. Send test data from host computer
4. Observe FT-817 TX LED (should light during transmission)
5. Check TNC3 LED (should flash during transmission)

**Receive Test:**
1. Have another station transmit on same frequency
2. Observe FT-817 S-meter (should show signal)
3. Check TNC3 LED (should flash during reception)
4. Verify data received on host computer

### Audio Level Adjustment

If audio levels need adjustment:

**Too Low (no decode):**
- Increase TNC3 output volume
- Increase FT-817 AF gain

**Too High (distortion):**
- Decrease TNC3 output volume
- Decrease FT-817 AF gain

**Optimal Settings:**
- FT-817 AF gain: 50-70%
- TNC3 volumes: 4-6 (via Mobilinkd app)

## Bench Testing Configuration

**Physical Setup:**
- Place all three FT-817 units on bench
- Connect each to dummy load (50Ω, 10W minimum)
- Maintain 1-2 meter separation
- Use low power (L1 = 0.5W or L2 = 1W)

**Advantages:**
- No RF interference to other users
- Safe indoor operation
- Easy troubleshooting
- Repeatable test conditions

## Field Testing Configuration

**Physical Setup:**
- Separate nodes by 1-10 km
- Use external antennas (vertical recommended)
- Mount antennas at adequate height (>5m)
- Line-of-sight or near-line-of-sight preferred

**Power Settings:**
- Start with L2 (1W) and increase as needed
- Maximum L3 (2.5W) or HI (5W) for longer distances
- Monitor battery life (FT-817 internal battery: ~2 hours at 5W)

## Troubleshooting

### No TX/RX Activity

**Check Connections:**
- Verify USB cable connected (TNC3 to RPi)
- Verify audio cable connected (TNC3 to FT-817 DATA port)
- Check TNC3 power LED (blue)

**Check FT-817 Settings:**
- Mode: FM (not USB/LSB/AM)
- Frequency: Correct (all nodes same frequency)
- Squelch: Not too high (try setting to 0 temporarily)
- Power: Not zero

**Check TNC3:**
- Battery charged
- Firmware up to date
- KISS mode enabled

### Poor Audio Quality

**Symptoms:**
- Garbled audio
- No decode
- High error rate

**Solutions:**
- Adjust TNC3 input/output volumes
- Check audio cable connections
- Verify FT-817 AF gain setting
- Reduce RF power if overdriving

### PTT Not Working

**Check:**
- Audio cable includes PTT connection
- TNC3 PTT configured correctly (via Mobilinkd app)
- FT-817 not in VOX mode

## Node-Specific Configuration

### Node A (Source) - G4DPZ-1
- Callsign: G4DPZ-1
- Frequency: 433.500 MHz
- Power: L1 (0.5W) for bench, L2-L3 for field
- TNC Device: /dev/ttyUSB0

### Node B (Relay) - G4DPZ-2
- Callsign: G4DPZ-2
- Frequency: 433.500 MHz (same as Node A)
- Power: L1 (0.5W) for bench, L2-L3 for field
- TNC Device: /dev/ttyUSB0

### Node C (Destination) - G4DPZ-3
- Callsign: G4DPZ-3
- Frequency: 433.500 MHz (same as Nodes A & B)
- Power: L1 (0.5W) for bench, L2-L3 for field
- TNC Device: /dev/ttyUSB0

## Quick Reference

```
┌─────────────────────────────────────────────┐
│ Mobilinkd TNC3 + FT-817 Quick Setup        │
├─────────────────────────────────────────────┤
│ 1. FT-817: Set mode to FM                  │
│ 2. FT-817: Set frequency (433.500 MHz)     │
│ 3. FT-817: Set power (L1 for bench)        │
│ 4. FT-817: Menu 24 PKT RATE = 1200         │
│ 5. Connect TNC3 to RPi via USB             │
│ 6. Connect TNC3 to FT-817 DATA port        │
│ 7. Connect dummy load to FT-817 antenna    │
│ 8. Run init_tnc.sh script                  │
│ 9. Verify TNC3 and FT-817 LEDs flash       │
└─────────────────────────────────────────────┘
```

## References

- Mobilinkd TNC3 Manual: https://www.mobilinkd.com/
- Yaesu FT-817ND Manual: https://www.yaesu.com/
- KISS Protocol: http://www.ka9q.net/papers/kiss.html
- AX.25 Protocol: http://www.ax25.net/

1. Press [FUNC] then [SET] to enter menu mode
2. Menu 14 (PKT RATE): Set to 1200 (1200 baud)
3. Menu 62 (DATA MODE): Set to PKT (packet mode)
4. Menu 63 (PKT MIC): Set to appropriate level (start with 50)
5. Menu 64 (PKT OUT): Set to appropriate level (start with 50)
6. Press [FUNC] then [SET] to exit menu mode

**Audio Levels:**
- Start with PKT MIC and PKT OUT at 50
- Adjust based on TNC3 LED indicators during testing
- Too high: Distortion and errors
- Too low: Weak signal and missed packets

### 2. Mobilinkd TNC3 Configuration

**Charge Battery:**
1. Connect TNC3 to USB power
2. Charge indicator LED will show charging status
3. Full charge takes approximately 2-3 hours
4. Can operate while charging

**Connect to FT-817:**
1. Connect 3.5mm end to TNC3 audio jack
2. Connect 6-pin mini-DIN end to FT-817 DATA jack (rear panel)
3. Ensure connections are secure

**Connect to Raspberry Pi:**
1. Connect USB cable (Micro-USB to TNC3, Type-A to Raspberry Pi)
2. TNC3 will power on automatically
3. Green power LED should illuminate
4. Device will appear as /dev/ttyUSB0 or /dev/ttyACM0

### 3. Verify Connection

**Check Device on Linux:**
```bash
# List USB serial devices
ls -l /dev/ttyUSB* /dev/ttyACM*

# Check device information
dmesg | grep tty

# Verify permissions
ls -l /dev/ttyUSB0  # or /dev/ttyACM0
```

**Add User to dialout Group (if needed):**
```bash
sudo usermod -a -G dialout $USER
# Log out and back in for changes to take effect
```

## Running the Initialization Script

```bash
# Navigate to scripts directory
cd /opt/ion-demo/scripts

# Run init script (no sudo needed for basic TNC init)
./init_tnc.sh /dev/ttyUSB0 9600

# Or let it auto-detect
./init_tnc.sh
```

The script will:
1. Configure serial port (9600 baud, 8N1, raw mode)
2. Send KISS initialization frames
3. Set TXDelay (300ms)
4. Set Persistence (25%)
5. Set SlotTime (100ms)
6. Verify TNC connection

## LED Indicators

**Mobilinkd TNC3 LEDs:**
- **Green (Power)**: Solid = powered on, Flashing = low battery
- **Red (TX)**: Illuminates when transmitting
- **Yellow (RX)**: Illuminates when receiving valid packets

**FT-817 Display:**
- **TX indicator**: Shows when transmitting
- **Busy indicator**: Shows when receiving signal
- **S-meter**: Shows received signal strength

## Bench Testing Configuration

**RF Isolation:**
1. Connect FT-817 antenna jack to 50Ω dummy load (10W minimum)
2. Set FT-817 power to 2.5W or 5W
3. Place all three nodes 1-2 meters apart
4. Indoor operation is acceptable for bench testing

**Power Supply:**
- FT-817: Use internal battery pack or 13.8V DC supply
- TNC3: USB-powered from Raspberry Pi (or internal battery)
- Raspberry Pi: 5V 3A USB power supply

## Troubleshooting

### TNC3 Not Detected

**Check USB Connection:**
```bash
lsusb  # Should show Mobilinkd device
dmesg | tail -20  # Check for connection messages
```

**Try Different USB Port:**
- Some USB ports may not provide enough power
- Use USB 2.0 or 3.0 port directly on Raspberry Pi

### No TX/RX Activity

**Check FT-817 Settings:**
- Verify frequency is correct
- Verify mode is FM (not USB/LSB)
- Verify PKT MODE is enabled (Menu 62)
- Check audio levels (Menu 63, 64)

**Check TNC3 Connection:**
- Verify data cable is connected to DATA jack (not MIC/SP)
- Check cable for damage
- Verify TNC3 battery is charged

**Check Audio Levels:**
- TNC3 TX LED should flash when transmitting
- If no TX LED: Increase PKT MIC level on FT-817
- If TX LED but no transmission: Check PTT connection

### Poor Packet Success Rate

**Adjust Audio Levels:**
1. Start with PKT MIC = 50, PKT OUT = 50
2. Monitor TNC3 RX LED during reception
3. If no RX LED: Increase PKT OUT on FT-817
4. If RX LED but errors: Decrease PKT OUT (may be overdriving)

**Check RF Environment:**
- Ensure dummy loads are connected
- Check for RF interference
- Verify all nodes on same frequency

## Advanced Configuration

### TNC3 Configuration via Bluetooth

The Mobilinkd TNC3 can be configured via Bluetooth using the Mobilinkd Config app:

1. Download Mobilinkd Config app (Android/iOS)
2. Enable Bluetooth on phone/tablet
3. Power on TNC3
4. Connect to TNC3 via Bluetooth
5. Adjust settings:
   - TX Delay: 30 (300ms)
   - Persistence: 63 (25%)
   - Slot Time: 10 (100ms)
   - TX Tail: 5 (50ms)
   - Duplex: Half

### FT-817 Menu Reference

**Key Menus for Packet Operation:**
- Menu 14 (PKT RATE): 1200 baud
- Menu 62 (DATA MODE): PKT
- Menu 63 (PKT MIC): 30-70 (adjust for your setup)
- Menu 64 (PKT OUT): 30-70 (adjust for your setup)
- Menu 65 (DIG DISP): ±0 (frequency offset)
- Menu 66 (DIG MIC): 50 (if using front mic)
- Menu 67 (DIG SHIFT): 0 (no shift for FM packet)

## Quick Reference

```
┌─────────────────────────────────────────────┐
│ Mobilinkd TNC3 + FT-817 Quick Setup        │
├─────────────────────────────────────────────┤
│ FT-817:                                     │
│   1. Set frequency: 433.500 MHz             │
│   2. Set mode: FM                           │
│   3. Set power: 2.5W or 5W                  │
│   4. Menu 14: PKT RATE = 1200               │
│   5. Menu 62: DATA MODE = PKT               │
│   6. Menu 63: PKT MIC = 50                  │
│   7. Menu 64: PKT OUT = 50                  │
│                                             │
│ TNC3:                                       │
│   1. Charge battery                         │
│   2. Connect to FT-817 DATA jack            │
│   3. Connect USB to Raspberry Pi            │
│   4. Verify /dev/ttyUSB0 exists             │
│                                             │
│ Run: ./init_tnc.sh /dev/ttyUSB0 9600        │
└─────────────────────────────────────────────┘
```

## References

- Mobilinkd TNC3 Manual: https://www.mobilinkd.com/
- FT-817 Operating Manual: https://www.yaesu.com/
- KISS Protocol: http://www.ka9q.net/papers/kiss.html
