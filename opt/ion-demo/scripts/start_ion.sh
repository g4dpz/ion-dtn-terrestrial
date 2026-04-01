#!/bin/bash
#
# ION-DTN Startup Script
# Starts ION with LTP over serial configuration
#
# Usage: ./start_ion.sh <node_name>
#   node_name: node_a, node_b, or node_c
#
# Example: ./start_ion.sh node_a

set -e

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BASE_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
NODE_NAME="${1}"
CONFIG_DIR="$BASE_DIR/config"
LOG_DIR="$BASE_DIR/logs"
LOG_PREFIX="[$(date '+%Y-%m-%d %H:%M:%S')]"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${LOG_PREFIX} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check for required argument
if [ -z "$NODE_NAME" ]; then
    echo "Usage: $0 <node_name>"
    echo ""
    echo "Available nodes:"
    echo "  node_a - Source node (dtn://g4dpz-1/)"
    echo "  node_b - Relay node (dtn://g4dpz-2/)"
    echo "  node_c - Destination node (dtn://g4dpz-3/)"
    echo ""
    exit 1
fi

# Validate node name
if [ ! -d "$CONFIG_DIR/$NODE_NAME" ]; then
    log_error "Configuration directory not found: $CONFIG_DIR/$NODE_NAME"
    echo ""
    echo "Available nodes:"
    ls -1 "$CONFIG_DIR" | grep "node_"
    exit 1
fi

# Create log directory if it doesn't exist
mkdir -p "$LOG_DIR"

log_info "Starting ION-DTN for $NODE_NAME..."

# Check if ION is already running
if pgrep -x "ionadmin" > /dev/null; then
    log_warning "ION appears to be already running"
    echo "To stop ION, run: killm"
    echo "Or use: ./stop_ion.sh"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Step 1: Load ION configuration
log_info "Loading ION configuration..."
if ionadmin "$CONFIG_DIR/$NODE_NAME/ionconfig"; then
    log_success "ION configuration loaded"
else
    log_error "Failed to load ION configuration"
    exit 1
fi

sleep 1

# Step 2: Load ION administration (contact graph)
log_info "Loading ION administration (contact graph)..."
if ionadmin "$CONFIG_DIR/$NODE_NAME/ionrc"; then
    log_success "ION administration loaded"
else
    log_error "Failed to load ION administration"
    exit 1
fi

sleep 1

# Step 3: Load LTP configuration
log_info "Loading LTP configuration..."
if ltpadmin "$CONFIG_DIR/$NODE_NAME/ltprc"; then
    log_success "LTP configuration loaded"
else
    log_error "Failed to load LTP configuration"
    exit 1
fi

sleep 1

# Step 4: Load Bundle Protocol configuration
log_info "Loading Bundle Protocol configuration..."
if bpadmin "$CONFIG_DIR/$NODE_NAME/bprc"; then
    log_success "Bundle Protocol configuration loaded"
else
    log_error "Failed to load Bundle Protocol configuration"
    exit 1
fi

sleep 1

# Step 5: Verify ION is running
log_info "Verifying ION status..."

if pgrep -x "ltpcli" > /dev/null && pgrep -x "ltpclo" > /dev/null; then
    log_success "LTP convergence layer is running"
else
    log_warning "LTP convergence layer may not be running properly"
fi

if pgrep -x "bpclock" > /dev/null; then
    log_success "Bundle Protocol clock is running"
else
    log_warning "Bundle Protocol clock may not be running"
fi

# Display ION status
echo ""
log_info "ION-DTN Status:"
echo ""

# Show running ION processes
echo "Running ION processes:"
ps aux | grep -E "(ionadmin|ltpcli|ltpclo|bpclock|bptransit)" | grep -v grep || echo "  No ION processes found"

echo ""
log_success "ION-DTN started successfully for $NODE_NAME"
echo ""

# Display node information
case "$NODE_NAME" in
    node_a)
        echo "Node A (Source) - dtn://g4dpz-1/"
        echo "  LTP Engine ID: 1"
        echo "  Connects to: Node B (engine 2)"
        ;;
    node_b)
        echo "Node B (Relay) - dtn://g4dpz-2/"
        echo "  LTP Engine ID: 2"
        echo "  Connects to: Node A (engine 1) and Node C (engine 3)"
        ;;
    node_c)
        echo "Node C (Destination) - dtn://g4dpz-3/"
        echo "  LTP Engine ID: 3"
        echo "  Connects to: Node B (engine 2)"
        ;;
esac

echo ""
echo "Next steps:"
echo "  1. Check ION status: ionadmin"
echo "  2. View LTP status: ltpadmin"
echo "  3. View BP status: bpadmin"
echo "  4. Send test bundle: bpsendfile dtn://g4dpz-2/demo testfile.txt"
echo "  5. Receive bundles: bprecvfile dtn://g4dpz-1/demo"
echo ""
echo "To stop ION:"
echo "  ./stop_ion.sh"
echo "  or: killm"
echo ""
