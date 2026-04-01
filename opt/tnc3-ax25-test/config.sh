#!/bin/bash
# AX.25 Test Configuration

# Serial device paths
SENDER_DEVICE="/dev/cu.usbmodem20A5329335531"
RECEIVER_DEVICE="/dev/ttyUSB0"

# Callsigns (must be valid amateur radio callsigns)
SENDER_CALL="G4DPZ-1"
RECEIVER_CALL="G4DPZ-2"

# Serial baud rate
BAUD_RATE="9600"

# AX.25 interface names
SENDER_IFACE="ax0"
RECEIVER_IFACE="ax0"

# Radio frequency (for reference only - set manually on radios)
FREQUENCY="433.500"

# Ping interval (seconds)
PING_INTERVAL="5"
