#!/bin/bash
# AX.25 Ping Receiver
# Usage: ./ping_receiver.sh

source config.sh

echo "Starting AX.25 ping receiver..."
echo "Listening on: $RECEIVER_CALL"
echo ""

# Check if ax0 interface exists
if ! ifconfig ax0 &>/dev/null; then
    echo "ERROR: ax0 interface not found"
    echo "Run: sudo ./setup_ax25.sh receiver $RECEIVER_DEVICE $RECEIVER_CALL"
    exit 1
fi

# Listen for incoming connections
echo "Waiting for incoming pings..."
axlisten -a 2>&1 | while read -r line; do
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$TIMESTAMP] $line"
done
