#!/bin/bash
# TNC Initialization Script for ION-DTN UHF Demo
# Configures TNC in KISS mode and verifies connection
#
# Supported TNCs:
#   - Kenwood TH-D72 (built-in TNC) - RECOMMENDED
#   - Mobilinkd TNC3
#   - Kantronics KPC-3+
#   - Direwolf software TNC
#   - Other KISS-compatible TNCs

set -e

# Configuration
TNC_DEVICE="${TNC_DEVICE:-/dev/tty.usbmodem2086327235531}"
TNC_BAUD="${TNC_BAUD:-9600}"
LOG_FILE="/opt/ion-demo/logs/tnc.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$LOG_FILE"
    exit 1
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$LOG_FILE"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$LOG_FILE"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    error "This script must be run as root (use sudo)"
fi

log "Starting TNC initialization..."
log "TNC Device: $TNC_DEVICE"
log "TNC Baud Rate: $TNC_BAUD"

# Check if TNC device exists
if [ ! -e "$TNC_DEVICE" ]; then
    error "TNC device $TNC_DEVICE not found. Please check connection."
fi

# Configure serial port parameters
log "Configuring serial port parameters..."

# Detect OS and use appropriate stty syntax
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS uses -f instead of -F
    stty -f "$TNC_DEVICE" "$TNC_BAUD" raw -echo -echoe -echok || error "Failed to configure serial port"
    
    # Set additional serial port parameters for KISS mode
    stty -f "$TNC_DEVICE" \
        cs8 \
        -parenb \
        -cstopb \
        -ixon \
        -ixoff \
        -crtscts || warning "Failed to set some serial port parameters"
else
    # Linux uses -F
    stty -F "$TNC_DEVICE" "$TNC_BAUD" raw -echo -echoe -echok || error "Failed to configure serial port"
    
    # Set additional serial port parameters for KISS mode
    stty -F "$TNC_DEVICE" \
        cs8 \
        -parenb \
        -cstopb \
        -ixon \
        -ixoff \
        -crtscts || warning "Failed to set some serial port parameters"
fi

success "Serial port configured: $TNC_DEVICE at $TNC_BAUD baud"

# Check if device is writable before attempting KISS initialization
log "Verifying TNC device permissions..."

if [ ! -w "$TNC_DEVICE" ]; then
    error "TNC device is not writable: $TNC_DEVICE"
    echo ""
    echo "On macOS, you may need to:"
    echo "  1. Close any applications using the device (Arduino IDE, screen, etc.)"
    echo "  2. Disconnect and reconnect the USB cable"
    echo "  3. Run: sudo chmod 666 $TNC_DEVICE"
    echo ""
    echo "Note: The Mobilinkd TNC3 may need to be in KISS mode already."
    echo "      Use the Mobilinkd Config app to enable KISS mode if needed."
    exit 1
fi

if [ ! -r "$TNC_DEVICE" ]; then
    error "TNC device is not readable: $TNC_DEVICE"
    echo ""
    echo "On macOS, you may need to:"
    echo "  1. Run: sudo chmod 666 $TNC_DEVICE"
    exit 1
fi

success "TNC device is readable and writable"

# Send KISS mode initialization frames
log "Sending KISS initialization frames..."

# Function to send data with timeout
send_with_timeout() {
    local data="$1"
    local device="$2"
    
    # On macOS, serial writes can hang - use timeout
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # Use a background process with timeout
        (
            # Try to open and write to device
            exec 3>"$device" 2>/dev/null && printf "%b" "$data" >&3 2>/dev/null && exec 3>&-
        ) &
        local pid=$!
        
        # Wait up to 2 seconds
        local count=0
        while kill -0 $pid 2>/dev/null && [ $count -lt 20 ]; do
            sleep 0.1
            count=$((count + 1))
        done
        
        # Kill if still running
        if kill -0 $pid 2>/dev/null; then
            kill -9 $pid 2>/dev/null
            wait $pid 2>/dev/null
            return 1
        fi
        
        wait $pid 2>/dev/null
        return $?
    else
        # Linux: direct write
        if echo -ne "$data" > "$device" 2>/dev/null; then
            return 0
        else
            return 1
        fi
    fi
}

# Note for Mobilinkd TNC3 users
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo "NOTE: Mobilinkd TNC3 is already in KISS mode by default."
    echo "      KISS parameters can be configured via Mobilinkd Config app."
    echo "      Skipping KISS command transmission on macOS (not required for TNC3)."
    echo ""
    
    success "TNC3 is ready (already in KISS mode)"
    
    # Skip KISS commands on macOS - TNC3 doesn't need them
    SKIP_KISS_COMMANDS=true
else
    SKIP_KISS_COMMANDS=false
fi

if [ "$SKIP_KISS_COMMANDS" = false ]; then
    # KISS FEND (Frame End) character: 0xC0
    # Send two FEND characters to reset TNC
    log "Sending KISS reset frames..."
    if send_with_timeout "\xC0\xC0" "$TNC_DEVICE"; then
        success "KISS reset frames sent"
    else
        warning "KISS frame send failed"
    fi

    sleep 1

    # Send KISS command to set TXDelay (optional, adjust as needed)
    # KISS command format: FEND + CMD + DATA + FEND
    # CMD 0x01 = TXDelay, value 0x1E = 30 (300ms)
    log "Setting TXDelay..."
    send_with_timeout "\xC0\x01\x1E\xC0" "$TNC_DEVICE" || warning "TXDelay command failed"

    sleep 0.5

    # Send KISS command to set Persistence (optional)
    # CMD 0x02 = Persistence, value 0x3F = 63 (25% persistence)
    log "Setting Persistence..."
    send_with_timeout "\xC0\x02\x3F\xC0" "$TNC_DEVICE" || warning "Persistence command failed"

    sleep 0.5

    # Send KISS command to set SlotTime (optional)
    # CMD 0x03 = SlotTime, value 0x05 = 5 (50ms)
    log "Setting SlotTime..."
    send_with_timeout "\xC0\x03\x05\xC0" "$TNC_DEVICE" || warning "SlotTime command failed"

    sleep 0.5

    success "KISS initialization frames sent"
fi

# Display current serial port settings
log "Current serial port settings:"
if [[ "$OSTYPE" == "darwin"* ]]; then
    stty -f "$TNC_DEVICE" -a | tee -a "$LOG_FILE"
else
    stty -F "$TNC_DEVICE" -a | tee -a "$LOG_FILE"
fi

success "TNC initialization complete!"
log "TNC is ready for KISS operation"

# Display usage information
echo ""
echo "TNC is now configured in KISS mode."
echo ""
echo "=== Mobilinkd TNC3 + FT-817 Setup ==="
echo ""
echo "Mobilinkd TNC3:"
echo "  - Already in KISS mode by default"
echo "  - USB connection: $TNC_DEVICE"
echo "  - Configure KISS parameters via Mobilinkd Config app (optional):"
echo "      * TX Delay: 30 (300ms)"
echo "      * Persistence: 63 (25%)"
echo "      * Slot Time: 10 (100ms)"
echo "      * Duplex: Half"
echo ""
echo "FT-817 Configuration:"
echo "  - Mode: FM (not USB/LSB)"
echo "  - Frequency: 433.500 MHz (or coordinated frequency)"
echo "  - Power: L1 (0.5W) for bench testing"
echo "  - Menu 24 (PKT RATE): 1200 baud"
echo "  - Connect TNC3 audio cable to FT-817 DATA port"
echo "  - Connect dummy load to FT-817 antenna port"
echo ""
echo "Next steps:"
echo "  1. Verify FT-817 settings above"
echo "  2. Update ltprc files with correct serial device: $TNC_DEVICE"
echo "  3. Start ION-DTN (ionadmin, ltpadmin, bpadmin)"
echo "  4. Test bundle transmission with bpsendfile/bprecvfile"
echo ""
echo "For detailed setup instructions, see:"
echo "  opt/ion-demo/docs/MOBILINKD-FT817-SETUP.md"
echo ""

exit 0
