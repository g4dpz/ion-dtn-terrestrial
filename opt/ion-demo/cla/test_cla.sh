#!/bin/bash
#
# test_cla.sh - End-to-end test of seriallso/seriallsi over RF
#
# Runs both the sender and receiver sides on the same machine
# (two TNC3 units connected to two radios on the same frequency).
#
# Usage: ./test_cla.sh <tx_device> <rx_device> [message] [count] [interval]
# Example: ./test_cla.sh /dev/tty.usbmodem20A5329335531 /dev/tty.usbmodem20B6123456781
#

set -e

TX_DEV="${1:?Usage: $0 <tx_device> <rx_device> <src_call> <dest_call> [message] [count] [interval]}"
RX_DEV="${2:?Usage: $0 <tx_device> <rx_device> <src_call> <dest_call> [message] [count] [interval]}"
SRC_CALL="${3:?Usage: $0 <tx_device> <rx_device> <src_call> <dest_call> [message] [count] [interval]}"
DEST_CALL="${4:?Usage: $0 <tx_device> <rx_device> <src_call> <dest_call> [message] [count] [interval]}"
MESSAGE="${5:-Hello from seriallso CLA test}"
COUNT="${6:-5}"
INTERVAL="${7:-3}"
UDP_PORT=11113  # Use non-privileged port to avoid conflicts

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PIDS=()

cleanup() {
    echo ""
    echo "Cleaning up..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    echo "Done."
}
trap cleanup EXIT

# Check devices exist
for dev in "$TX_DEV" "$RX_DEV"; do
    if [ ! -e "$dev" ]; then
        echo "ERROR: Device $dev not found"
        exit 1
    fi
done

# Check binaries exist
for bin in seriallso seriallsi test_lso_feed test_lsi_recv; do
    if [ ! -x "$SCRIPT_DIR/$bin" ]; then
        echo "ERROR: $bin not found. Run 'make' first."
        exit 1
    fi
done

echo "════════════════════════════════════════════════════"
echo "  Serial CLA End-to-End Test"
echo "════════════════════════════════════════════════════"
echo "  TX device:  $TX_DEV"
echo "  RX device:  $RX_DEV"
echo "  Src call:   $SRC_CALL"
echo "  Dest call:  $DEST_CALL"
echo "  Message:    $MESSAGE"
echo "  Count:      $COUNT"
echo "  Interval:   ${INTERVAL}s"
echo "  UDP port:   $UDP_PORT"
echo "════════════════════════════════════════════════════"
echo ""

# Kill any stale process on the UDP port
lsof -ti udp:$UDP_PORT 2>/dev/null | xargs kill -9 2>/dev/null || true
sleep 0.2

# Step 1: Start UDP listener (simulates ltpcli)
echo "[1/3] Starting UDP listener on port $UDP_PORT..."
"$SCRIPT_DIR/test_lsi_recv" "$UDP_PORT" &
PIDS+=($!)
sleep 0.5

# Step 2: Start seriallsi (reads KISS from RX serial, forwards to UDP)
echo "[2/3] Starting seriallsi on $RX_DEV..."
"$SCRIPT_DIR/seriallsi" "$RX_DEV:9600" "$UDP_PORT" &
PIDS+=($!)
sleep 1

# Step 3: Feed test segments through seriallso (writes KISS to TX serial)
echo "[3/3] Sending $COUNT segments via seriallso on $TX_DEV..."
echo ""
"$SCRIPT_DIR/test_lso_feed" "$MESSAGE" "$COUNT" "$INTERVAL" | "$SCRIPT_DIR/seriallso" "$TX_DEV:9600" "$SRC_CALL" "$DEST_CALL"

# Wait a bit for last segment to propagate over RF
echo ""
echo "Waiting for final segment to arrive..."
sleep 3

echo ""
echo "════════════════════════════════════════════════════"
echo "  Test complete. Check output above for received"
echo "  segments on the UDP listener."
echo "════════════════════════════════════════════════════"
