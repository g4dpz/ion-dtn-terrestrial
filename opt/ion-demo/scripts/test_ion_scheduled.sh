#!/bin/bash
#
# test_ion_scheduled.sh - Scheduled contact window test
#
# Submits a bundle immediately, but the contact window doesn't open
# until DELAY seconds later. ION queues the bundle and transmits
# when the window opens — demonstrating store-and-forward DTN behavior.
#
# Usage: ./test_ion_scheduled.sh <tx_device> <rx_device> [delay_seconds]
# Default delay: 60 seconds
#

set -e

TX_DEV="${1:?Usage: $0 <tx_device> <rx_device> [delay_seconds]}"
RX_DEV="${2:?Usage: $0 <tx_device> <rx_device> [delay_seconds]}"
DELAY="${3:-60}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ION_DIR="$(cd "$PROJECT_DIR/../../ION-DTN" && pwd)"
ION_BIN="$ION_DIR/.libs"
CLA_DIR="$PROJECT_DIR/cla"
CONFIG_A="$PROJECT_DIR/config/node_a"
DATA_A="/tmp/ion_node_a"
LSO_PORT=1114
RECV_PORT=11113

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

WINDOW_DURATION=120

echo "════════════════════════════════════════════════════"
echo "  ION-DTN Scheduled Contact Window Test"
echo "════════════════════════════════════════════════════"
echo "  TX: $TX_DEV"
echo "  RX: $RX_DEV"
echo "  Contact opens in: ${DELAY}s"
echo "  Contact duration: ${WINDOW_DURATION}s"
echo ""
echo "  Timeline:"
echo "    T+0s:       Bundle submitted (queued, no contact)"
echo "    T+${DELAY}s:  Contact window opens, bundle transmitted"
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
echo "[2/8] Starting test_lsi_recv..."
"$CLA_DIR/test_lsi_recv" "$RECV_PORT" &
PIDS+=($!)
sleep 0.3

echo "[3/8] Starting seriallsi on $RX_DEV..."
"$CLA_DIR/seriallsi" "$RX_DEV:9600" "$RECV_PORT" &
PIDS+=($!)
sleep 0.5

echo "[4/8] Starting seriallso bridge (hold ${DELAY}s)..."
"$CLA_DIR/seriallso" "$LSO_PORT" "$TX_DEV:9600" G4DPZ-1 G4DPZ-2 "$DELAY" &
PIDS+=($!)
sleep 0.5

# Create ionrc with delayed contact
echo "[5/8] Preparing ION config (contact at +${DELAY}s)..."
cat > "$DATA_A/ionrc" << EOF
1 1 ''
s
a contact +${DELAY} +$((DELAY + WINDOW_DURATION)) 1 2 100000
a contact +${DELAY} +$((DELAY + WINDOW_DURATION)) 2 1 100000
a range +${DELAY} +$((DELAY + WINDOW_DURATION)) 1 2 1
m production 1000000
m consumption 1000000
EOF

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
echo "  ION running. Contact window opens in ${DELAY}s."

sleep 3

# Submit bundle immediately (before contact window)
echo "[7/8] Submitting bundle NOW (contact not open yet)..."
PAYLOAD="$DATA_A/test_payload.txt"
PAYLOAD_SIZE="${LARGE_PAYLOAD:-small}"
if [ "$PAYLOAD_SIZE" = "small" ]; then
    echo "Scheduled DTN test - created $(date), contact in ${DELAY}s" > "$PAYLOAD"
else
    python3 -c "
import sys; size=int(sys.argv[1]); msg='ION-DTN scheduled test. '
sys.stdout.write((msg*((size//len(msg))+1))[:size])" "$PAYLOAD_SIZE" > "$PAYLOAD"
fi
echo "  Payload: $(wc -c < "$PAYLOAD") bytes"

bpsendfile ipn:1.1 ipn:2.1 "$PAYLOAD"
SUBMIT_TIME=$(date +%s)
echo "  Bundle submitted at $(date +%H:%M:%S). Queued in ION."
echo ""

# Wait for contact window
echo "[8/8] Waiting for contact window to open..."
TOTAL_WAIT=$((DELAY + 30))
for i in $(seq 1 $TOTAL_WAIT); do
    ELAPSED=$(($(date +%s) - SUBMIT_TIME))
    REMAINING=$((DELAY - ELAPSED))

    if [ $REMAINING -gt 0 ]; then
        printf "\r  Waiting... %ds until contact window opens  " "$REMAINING"
    else
        printf "\r  Contact window OPEN (%ds elapsed)          " "$ELAPSED"
    fi

    sleep 1
done

echo ""
echo ""
echo "════════════════════════════════════════════════════"
echo "  Test complete."
echo "  Bundle was queued for ~${DELAY}s before transmission."
echo "  Check seriallso/seriallsi output above."
echo "════════════════════════════════════════════════════"
