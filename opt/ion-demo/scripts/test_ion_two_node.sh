#!/bin/bash
#
# test_ion_two_node.sh - Two-node ION-DTN test over AX.25/KISS serial
#
# Starts ION on both nodes (same Mac, two TNC3s), sends a bundle
# from Node A to Node B over RF, and verifies receipt.
#
# Usage: ./test_ion_two_node.sh <tx_device> <rx_device>
# Example: ./test_ion_two_node.sh /dev/tty.usbmodem20A5329335531 /dev/tty.usbmodem2086327235531
#

set -e

TX_DEV="${1:?Usage: $0 <tx_device> <rx_device>}"
RX_DEV="${2:?Usage: $0 <tx_device> <rx_device>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ION_DIR="$(cd "$PROJECT_DIR/../../ION-DTN" && pwd)"
ION_BIN="$ION_DIR/.libs"
CLA_DIR="$PROJECT_DIR/cla"

CONFIG_A="$PROJECT_DIR/config/node_a"
CONFIG_B="$PROJECT_DIR/config/node_b"

DATA_A="/tmp/ion_node_a"
DATA_B="/tmp/ion_node_b"

PIDS=()

cleanup() {
    echo ""
    echo "════════════════════════════════════════════════════"
    echo "  Shutting down..."
    echo "════════════════════════════════════════════════════"

    # Stop ION instances
    cd "$DATA_A" 2>/dev/null && "$ION_BIN/ionstop" 2>/dev/null || true
    cd "$DATA_B" 2>/dev/null && "$ION_BIN/ionstop" 2>/dev/null || true

    # Kill background processes
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done

    echo "  Done."
}
trap cleanup EXIT

# Verify ION binaries
for bin in ionadmin ltpadmin bpadmin bpsendfile bprecvfile ionstop; do
    if [ ! -x "$ION_BIN/$bin" ]; then
        echo "ERROR: $ION_BIN/$bin not found"
        exit 1
    fi
done

# Verify CLA binaries
for bin in seriallso seriallsi; do
    if [ ! -x "$CLA_DIR/$bin" ]; then
        echo "ERROR: $CLA_DIR/$bin not found. Run 'make' in cla/"
        exit 1
    fi
done

# Verify devices
for dev in "$TX_DEV" "$RX_DEV"; do
    if [ ! -e "$dev" ]; then
        echo "ERROR: Device $dev not found"
        exit 1
    fi
done

# Add CLA dir to PATH so ION can find seriallso
export PATH="$CLA_DIR:$ION_BIN:$PATH"
export LD_LIBRARY_PATH="$ION_DIR/.libs:${LD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH="$ION_DIR/.libs:${DYLD_LIBRARY_PATH:-}"

echo "════════════════════════════════════════════════════"
echo "  ION-DTN Two-Node RF Test"
echo "════════════════════════════════════════════════════"
echo "  TX device (Node A): $TX_DEV"
echo "  RX device (Node B): $RX_DEV"
echo "  ION binaries: $ION_BIN"
echo "  CLA binaries: $CLA_DIR"
echo "════════════════════════════════════════════════════"
echo ""

# Update ltprc with actual device paths
echo "[1/7] Updating config with device paths..."
sed "s|/dev/tty.usbmodem2086327235531|$TX_DEV|g" "$CONFIG_A/ltprc" > "/tmp/node_a_ltprc"
sed "s|/dev/ttyUSB0|$RX_DEV|g" "$CONFIG_B/ltprc" > "/tmp/node_b_ltprc"

# Create data directories
echo "[2/7] Creating data directories..."
mkdir -p "$DATA_A" "$DATA_B"

# Initialize Node A
echo "[3/7] Initializing ION Node A (dtn://g4dpz-1/)..."
cd "$DATA_A"
"$ION_BIN/ionadmin" "$CONFIG_A/ionrc"
"$ION_BIN/ionadmin" "$CONFIG_A/ionconfig"
"$ION_BIN/ltpadmin" "/tmp/node_a_ltprc"
"$ION_BIN/bpadmin" "$CONFIG_A/bprc"
echo "  Node A started."

# Start seriallsi on Node B's receive device (feeds into Node B's ltpcli)
echo "[4/7] Starting seriallsi for Node B on $RX_DEV..."
"$CLA_DIR/seriallsi" "$RX_DEV:9600" 1113 &
PIDS+=($!)
sleep 1

# Initialize Node B
echo "[5/7] Initializing ION Node B (dtn://g4dpz-2/)..."
cd "$DATA_B"
"$ION_BIN/ionadmin" "$CONFIG_B/ionrc"
"$ION_BIN/ionadmin" "$CONFIG_B/ionconfig"
"$ION_BIN/ltpadmin" "/tmp/node_b_ltprc"
"$ION_BIN/bpadmin" "$CONFIG_B/bprc"
echo "  Node B started."

# Wait for ION to settle
echo "[6/7] Waiting for ION to initialize..."
sleep 3

# Send a test bundle from Node A to Node B
echo "[7/7] Sending test bundle..."
echo ""

# Create test payload
PAYLOAD_FILE="/tmp/ion_test_payload.txt"
echo "Hello from ION-DTN over AX.25/KISS RF! Sent at $(date)" > "$PAYLOAD_FILE"

echo "  Payload: $(cat $PAYLOAD_FILE)"
echo ""

# Start receiver on Node B
RECV_FILE="/tmp/ion_test_received.txt"
rm -f "$RECV_FILE"

cd "$DATA_B"
"$ION_BIN/bprecvfile" "dtn://g4dpz-2/demo" 1 &
PIDS+=($!)
sleep 1

# Send from Node A
cd "$DATA_A"
"$ION_BIN/bpsendfile" "dtn://g4dpz-1/demo" "dtn://g4dpz-2/demo" "$PAYLOAD_FILE" 0.1.0.0.30

echo "  Bundle sent from dtn://g4dpz-1/demo to dtn://g4dpz-2/demo"
echo ""

# Wait for RF transmission and reception
echo "  Waiting for RF delivery (up to 30 seconds)..."
for i in $(seq 1 30); do
    # Check if bprecvfile created an output file
    if ls "$DATA_B"/testfile* 2>/dev/null | head -1 > /dev/null 2>&1; then
        RECV_FILE=$(ls "$DATA_B"/testfile* 2>/dev/null | head -1)
        echo ""
        echo "════════════════════════════════════════════════════"
        echo "  BUNDLE RECEIVED after ${i} seconds!"
        echo "════════════════════════════════════════════════════"
        echo "  Content: $(cat "$RECV_FILE")"
        echo "════════════════════════════════════════════════════"
        exit 0
    fi
    printf "."
    sleep 1
done

echo ""
echo "  Timed out waiting for bundle delivery."
echo "  Check ION logs in $DATA_A and $DATA_B for errors."
