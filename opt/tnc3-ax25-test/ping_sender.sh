#!/bin/bash
# AX.25 Ping Sender
# Usage: ./ping_sender.sh [destination_callsign]

source config.sh

DEST_CALL="${1:-$RECEIVER_CALL}"
INTERVAL="${2:-$PING_INTERVAL}"

echo "Starting AX.25 ping sender..."
echo "Destination: $DEST_CALL"
echo "Interval: ${INTERVAL}s"
echo ""

# Check if ax0 interface exists
if ! ifconfig ax0 &>/dev/null; then
    echo "ERROR: ax0 interface not found"
    echo "Run: sudo ./setup_ax25.sh sender $SENDER_DEVICE $SENDER_CALL"
    exit 1
fi

# Ping loop
COUNT=1
while true; do
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$TIMESTAMP] Sending ping #$COUNT to $DEST_CALL"
    
    # Send ping using axcall
    echo "PING $COUNT from $SENDER_CALL" | axcall ax0 "$DEST_CALL" 2>&1 | grep -v "^$" || true
    
    COUNT=$((COUNT + 1))
    sleep "$INTERVAL"
done
