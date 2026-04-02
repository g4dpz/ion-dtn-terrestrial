#!/bin/bash
#
# start_node_b.sh - Start ION Node B with serial CLA bridge
#
# Usage: ./start_node_b.sh <serial_device>
# Example: ./start_node_b.sh /dev/tty.usbmodem2086327235531
#

set -e

DEVICE="${1:?Usage: $0 <serial_device>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ION_BIN="${ION_BIN:-$(cd "$PROJECT_DIR/../../ION-DTN/.libs" 2>/dev/null && pwd)}"
CLA_DIR="$PROJECT_DIR/cla"
CONFIG="$PROJECT_DIR/config/node_b"
DATA="/tmp/ion_node_b"

export PATH="$CLA_DIR:$ION_BIN:$PATH"
export DYLD_LIBRARY_PATH="$ION_BIN:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$ION_BIN:${LD_LIBRARY_PATH:-}"

echo "════════════════════════════════════════════════════"
echo "  Starting ION Node B (ipn:2) - G4DPZ-2"
echo "  Device: $DEVICE"
echo "════════════════════════════════════════════════════"

# Clean
rm -rf "$DATA"
mkdir -p "$DATA"
killall serialcla 2>/dev/null || true
lsof -ti udp:1114 2>/dev/null | xargs kill -9 2>/dev/null || true
lsof -ti udp:1113 2>/dev/null | xargs kill -9 2>/dev/null || true
sleep 0.5

# Start CLA bridges
# Start combined CLA (single process for TX+RX on same serial device)
echo "Starting serialcla (TX: UDP:1114→serial, RX: serial→UDP:1113)..."
"$CLA_DIR/serialcla" "$DEVICE:9600" G4DPZ-2 G4DPZ-1 1114 1113 2:30 &
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
echo "  Node B running. To receive a bundle:"
echo "    bprecvfile ipn:2.1 1"
echo "  To send back:"
echo "    bpsendfile ipn:2.1 ipn:1.1 <file>"
echo "  To stop:"
echo "    ionstop"
echo "════════════════════════════════════════════════════"
