#!/bin/bash
#
# start_node_a.sh - Start ION Node A with serial CLA bridge
#
# Usage: ./start_node_a.sh <serial_device>
# Example: ./start_node_a.sh /dev/tty.usbmodem20A5329335531
#

set -e

DEVICE="${1:?Usage: $0 <serial_device>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ION_BIN="${ION_BIN:-$(cd "$PROJECT_DIR/../../ION-DTN/.libs" 2>/dev/null && pwd)}"
CLA_DIR="$PROJECT_DIR/cla"
CONFIG="$PROJECT_DIR/config/node_a"
DATA="/tmp/ion_node_a"

export PATH="$CLA_DIR:$ION_BIN:$PATH"
export DYLD_LIBRARY_PATH="$ION_BIN:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$ION_BIN:${LD_LIBRARY_PATH:-}"

echo "════════════════════════════════════════════════════"
echo "  Starting ION Node A (ipn:1) - G4DPZ-1"
echo "  Device: $DEVICE"
echo "════════════════════════════════════════════════════"

# Clean
rm -rf "$DATA"
mkdir -p "$DATA"
killall seriallso seriallsi 2>/dev/null || true
lsof -ti udp:1114 2>/dev/null | xargs kill -9 2>/dev/null || true
lsof -ti udp:1113 2>/dev/null | xargs kill -9 2>/dev/null || true
sleep 0.5

# Start CLA bridges
echo "Starting seriallso (UDP:1114 → serial → RF)..."
"$CLA_DIR/seriallso" 1114 "$DEVICE:9600" G4DPZ-1 G4DPZ-2 0 2:30 &
echo "Starting seriallsi (RF → serial → UDP:1113)..."
"$CLA_DIR/seriallsi" "$DEVICE:9600" 1113 &
sleep 1

# Start ION
echo "Starting ION..."
cp "$CONFIG"/{ionrc,ltprc,bprc,ipnrc} "$DATA/"
cd "$DATA"
ionadmin ionrc
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc

echo ""
echo "════════════════════════════════════════════════════"
echo "  Node A running. To send a bundle:"
echo "    bpsendfile ipn:1.1 ipn:2.1 <file>"
echo "  To receive:"
echo "    bprecvfile ipn:1.1 1"
echo "  To stop:"
echo "    ionstop"
echo "════════════════════════════════════════════════════"
