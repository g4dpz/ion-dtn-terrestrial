#!/bin/bash
# Setup AX.25 interface for TNC3
# Usage: sudo ./setup_ax25.sh [sender|receiver] [device] [callsign]

set -e

# Source configuration defaults
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$SCRIPT_DIR/config.sh" ]; then
    source "$SCRIPT_DIR/config.sh"
fi

MODE="${1:-sender}"
DEVICE="${2:-/dev/ttyUSB0}"
CALLSIGN="${3:-NOCALL-1}"
BAUD_RATE="${BAUD_RATE:-9600}"

echo "Setting up AX.25 interface..."
echo "Mode: $MODE"
echo "Device: $DEVICE"
echo "Callsign: $CALLSIGN"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must run as root (use sudo)"
    exit 1
fi

# Check if device exists
if [ ! -e "$DEVICE" ]; then
    echo "ERROR: Device $DEVICE not found"
    exit 1
fi

# Load AX.25 kernel modules
echo "Loading AX.25 kernel modules..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "WARNING: macOS does not have native AX.25 kernel support."
    echo "         Skipping modprobe. Serial port will be configured for direct KISS access."
    echo "         Use the ION serial CLA (seriallso/seriallsi) instead of kissattach."
else
    modprobe mkiss 2>/dev/null || true
    modprobe ax25 2>/dev/null || true
fi

# Configure serial port for KISS mode
echo "Configuring serial port at $BAUD_RATE baud..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    stty -f "$DEVICE" speed "$BAUD_RATE" raw -echo cs8 -parenb -cstopb -ixon -ixoff -crtscts
else
    stty -F "$DEVICE" "$BAUD_RATE" raw -echo cs8 -parenb -cstopb -ixon -ixoff -crtscts
fi

# On macOS, skip Linux-only AX.25 tools
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo "Serial port configured for KISS mode."
    echo "macOS does not support kissattach/axparms."
    echo ""
    echo "For ION-DTN, use the serial CLA directly:"
    echo "  seriallsi $DEVICE:$BAUD_RATE &"
    echo "  # seriallso is invoked by ION via ltprc config"
    echo ""
    echo "For standalone KISS testing, use the Python or C tools:"
    echo "  cd macos && python3 ping_sender.py $DEVICE"
    exit 0
fi

# Create KISS interface using kissattach
echo "Creating KISS interface..."
kissattach "$DEVICE" ax0 2>&1 || {
    echo "ERROR: kissattach failed"
    echo "Make sure ax25-tools is installed: sudo apt-get install ax25-tools"
    exit 1
}

# Configure AX.25 parameters
echo "Configuring AX.25 parameters..."
kissparms -p ax0 -t 300 -s 100 -r 63 -l 100 2>/dev/null || true

# Set callsign
echo "Setting callsign..."
axparms --setcall ax0 "$CALLSIGN"

# Bring interface up
echo "Bringing interface up..."
ifconfig ax0 up

echo "AX.25 interface configured successfully!"
echo "Interface: ax0"
echo "Callsign: $CALLSIGN"
echo ""
echo "Verify with: ifconfig ax0"
