#!/bin/bash
#
# start_node_a.sh - Start ION Node A with integrated serial CLA
#
# Usage: ./start_node_a.sh <serial_device>
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
echo "  CLA: ionserialcla (integrated, with backpressure)"
echo "════════════════════════════════════════════════════"

# Hard clean all ION state
ionstop 2>/dev/null || true
killm 2>/dev/null || true
rm -rf "$DATA"
rm -f /tmp/ion.sdrlog /tmp/ion.sdrxnlog /tmp/*.ionlock /tmp/ion.*.sdrlog
mkdir -p "$DATA"
sleep 1

# Prepare config with actual device path
sed "s|DEVICE|$DEVICE|g" "$CONFIG/ltprc" > "$DATA/ltprc"
cp "$CONFIG"/{ionrc,bprc,ipnrc} "$DATA/"

# Start ION — ionserialcla is invoked by ION automatically
echo "Starting ION..."
cd "$DATA"
ionadmin ionrc
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc

echo ""
echo "════════════════════════════════════════════════════"
echo "  Node A running."
echo "  Send:    bpsendfile ipn:1.1 ipn:2.1 <file>"
echo "  Receive: bprecvfile ipn:1.1 1"
echo "  Stop:    ionstop"
echo "════════════════════════════════════════════════════"
