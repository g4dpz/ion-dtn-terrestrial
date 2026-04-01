# Scripts Directory

This directory contains operational scripts, demo applications, and utilities for the ION-DTN demonstration system.

## Script Categories

### System Initialization

**init_tnc.sh** - Generic KISS TNC initialization
- Works with any KISS-capable TNC (Mobilinkd, Direwolf, KPC-3+, etc.)
- Sends KISS initialization frames (reset, TXDelay, Persistence, SlotTime)
- Includes timeout protection for serial writes
- Usage: `./init_tnc.sh [device] [baud_rate]`

Other initialization scripts:
- `setup_ltp.sh`: Configure LTP over serial KISS interface
- `start_ion.sh`: Start ION with configuration loading
- `stop_ion.sh`: Graceful ION shutdown

### Demo Applications
- `bpsend.py`: Send bundles with JSON payloads
- `bprecv.py`: Receive and display bundles

### Contact Plan Management
- `load_contact_plan.sh`: Load contact plan into ION
- `control_link.sh`: Manually enable/disable RF paths
- `contact_executor.py`: Automated contact plan execution

### Monitoring and Status
- `queue_status.sh`: Display ION queue depths and link status
- `relay_monitor.py`: Real-time relay node status display
- `monitor.py`: Real-time system status monitor (curses-based)
- `diagnostics.sh`: System diagnostic checks

### Demonstration Modes
- `demo_mode1.sh`: Point-to-point real-time demonstration
- `demo_mode2.sh`: Delayed delivery demonstration
- `demo_mode3.sh`: Relay store-and-forward demonstration
- `demo_mode4.sh`: Interrupted forwarding demonstration
- `public_demo.sh`: Complete public demonstration script

### Testing and Validation
- `test_link.sh`: Validate link connectivity
- `run_demo.sh`: Orchestrate complete demonstration
- `reset_demo.sh`: Reset demonstration state

## Usage

All scripts should be executable:
```bash
chmod +x /opt/ion-demo/scripts/*.sh
chmod +x /opt/ion-demo/scripts/*.py
```

Run scripts from any directory - they reference absolute paths.
