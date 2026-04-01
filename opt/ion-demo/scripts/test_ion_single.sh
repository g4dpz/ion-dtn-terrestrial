#!/bin/bash
#
# test_ion_single.sh - Single ION node test over AX.25/KISS serial RF
#
# Data flow:
#   ION ltpclo → udplso → UDP:1114 → seriallso → AX.25/KISS → TNC → RF
#   RF → TNC → seriallsi → UDP:11113 → test_lsi_recv
#
# Usage: ./test_ion_single.sh <tx_device> <rx_device>
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
DATA_A="/tmp/ion_node_a"
LSO_PORT=1114     # seriallso listens here, udplso sends here
LSI_PORT=1113     # udplsi listens here, seriallsi sends here
RECV_PORT=11113   # test_lsi_recv listens here (for observing)

PIDS=()

cleanup() {
    echo ""
    echo "Shutting down..."
    cd "$DATA_A" 2>/dev/null
    ionstop 2>/dev/null || true
    sleep 1
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    echo "Done."
}
trap cleanup EXIT

export PATH="$CLA_DIR:$ION_BIN:$PATH"
export DYLD_LIBRARY_PATH="$ION_DIR/.libs:${DYLD_LIBRARY_PATH:-}"

echo "════════════════════════════════════════════════════"
echo "  ION-DTN Single Node RF Test"
echo "════════════════════════════════════════════════════"
echo "  TX: $TX_DEV"
echo "  RX: $RX_DEV"
echo "  ION → udplso:$LSO_PORT → seriallso → RF"
echo "  RF → seriallsi → UDP:$RECV_PORT → test_lsi_recv"
echo "════════════════════════════════════════════════════"
echo ""

# Clean
echo "[1/8] Cleaning previous state..."
rm -rf "$DATA_A"
mkdir -p "$DATA_A"
killall seriallso seriallsi test_lsi_recv 2>/dev/null || true
lsof -ti udp:$LSO_PORT 2>/dev/null | xargs kill -9 2>/dev/null || true
lsof -ti udp:$RECV_PORT 2>/dev/null | xargs kill -9 2>/dev/null || true
sleep 0.5

# Start receive side
echo "[2/8] Starting test_lsi_recv on UDP:$RECV_PORT..."
"$CLA_DIR/test_lsi_recv" "$RECV_PORT" &
PIDS+=($!)
sleep 0.3

echo "[3/8] Starting seriallsi on $RX_DEV → UDP:$RECV_PORT..."
"$CLA_DIR/seriallsi" "$RX_DEV:9600" "$RECV_PORT" &
PIDS+=($!)
sleep 0.5

# Start TX bridge
echo "[4/8] Starting seriallso bridge UDP:$LSO_PORT → $TX_DEV..."
"$CLA_DIR/seriallso" "$LSO_PORT" "$TX_DEV:9600" G4DPZ-1 G4DPZ-2 &
PIDS+=($!)
sleep 0.5

# Prepare ION config
echo "[5/8] Preparing ION config..."
cp "$CONFIG_A/ionrc" "$DATA_A/ionrc"
cp "$CONFIG_A/ltprc" "$DATA_A/ltprc"
cp "$CONFIG_A/bprc" "$DATA_A/bprc"
cp "$CONFIG_A/ipnrc" "$DATA_A/ipnrc"

# Start ION
echo "[6/8] Starting ION Node A..."
cd "$DATA_A"
ionadmin ionrc
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc
echo "  ION running."

sleep 5

# Send bundle
echo "[7/8] Sending bundle ipn:1.1 → ipn:2.1..."
PAYLOAD="$DATA_A/test_payload.txt"

# Generate payload - use LARGE_PAYLOAD env var to test fragmentation
PAYLOAD_SIZE="${LARGE_PAYLOAD:-small}"
if [ "$PAYLOAD_SIZE" = "small" ]; then
    echo "Hello from ION-DTN over AX.25 RF! $(date)" > "$PAYLOAD"
else
    # Generate a payload larger than LTP max_segment_size (1400 bytes)
    # to force LTP segmentation
    python3 -c "
import sys
size = int(sys.argv[1])
msg = 'ION-DTN fragmentation test payload. '
data = (msg * ((size // len(msg)) + 1))[:size]
sys.stdout.write(data)
" "$PAYLOAD_SIZE" > "$PAYLOAD"
fi
echo "  Payload: $(wc -c < "$PAYLOAD") bytes"
echo "  First 80 chars: $(head -c 80 "$PAYLOAD")"
echo ""

bpsendfile ipn:1.1 ipn:2.1 "$PAYLOAD"
echo "  Bundle submitted."
echo ""

# Wait
echo "[8/8] Waiting 30 seconds for RF delivery..."
sleep 30

echo ""
echo "════════════════════════════════════════════════════"
echo "  Test complete. Check output above."
echo "════════════════════════════════════════════════════"
