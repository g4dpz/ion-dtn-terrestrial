#!/bin/bash
#
# start_node_b.sh - Start ION Node B with integrated serial CLA and APRS beacon
#
# Usage: ./start_node_b.sh <serial_device>
#

set -e

DEVICE="${1:?Usage: $0 <serial_device>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CLA_DIR="$PROJECT_DIR/cla"
CONFIG="$PROJECT_DIR/config/node_b"
DATA="/tmp/ion_node_b"

export PATH="$CLA_DIR:$PATH"

# APRS beacon configuration
export ION_SERIAL_BEACON_LAT=52.467
export ION_SERIAL_BEACON_LON=-2.022
export ION_SERIAL_BEACON_INTERVAL=120
export ION_SERIAL_BEACON_COMMENT="github.com/g4dpz/ion-dtn-terrestrial"

echo "════════════════════════════════════════════════════"
echo "  Starting ION Node B (ipn:2) - G4DPZ-2"
echo "  Device: $DEVICE"
echo "  CLA: ionserialcla (with APRS beacon)"
echo "  Beacon: G4DPZ-2 every ${ION_SERIAL_BEACON_INTERVAL}s"
echo "════════════════════════════════════════════════════"

# Clean all ION state
killm 2>/dev/null || true
killall -9 ionserialcla ltpcli ltpclo bpclock bptransit ipnfw rfxclock ionwarn ltpmeter ltpdeliv udplsi bpadmin ltpadmin ionadmin ipnadmin 2>/dev/null || true
rm -rf "$DATA"
mkdir -p "$DATA"
sleep 1

# Prepare config with actual device path
sed "s|ionserialcla [^ ]*:|ionserialcla $DEVICE:|g" "$CONFIG/ltprc" > "$DATA/ltprc"
cp "$CONFIG"/ionrc "$DATA/"
cp "$CONFIG"/ionconfig "$DATA/"
cp "$CONFIG"/bprc "$DATA/"
cp "$CONFIG"/ipnrc "$DATA/"

# Start ION
echo "Starting ION..."
cd "$DATA"
ionadmin ionrc
ionsecadmin <<< "1"
ltpadmin ltprc
bpadmin bprc
ipnadmin ipnrc

# Verify CLA is running
sleep 3
if pgrep -f ionserialcla > /dev/null; then
    echo "  ionserialcla: running"
else
    echo "  WARNING: ionserialcla not running — check ion.log"
fi

echo ""
echo "════════════════════════════════════════════════════"
echo "  Node B running."
echo "  Recv text:  bpsink ipn:2.1"
echo "  Recv file:  bprecvfile ipn:2.2"
echo "  Send text:  bpsource ipn:1.1 \"Hello\""
echo "  Send file:  bpsendfile ipn:2.2 ipn:1.2 <file>"
echo "  Stop:       ionstop"
echo "════════════════════════════════════════════════════"
