# ION-DTN UHF Demonstration System

This directory contains the complete ION-DTN demonstration system for deployment to target platforms.

## Directory Structure

```
/opt/ion-demo/
├── config/           # ION configuration files for each node
│   ├── node_a/      # Source node configuration
│   ├── node_b/      # Relay node configuration
│   └── node_c/      # Destination node configuration
├── scripts/         # Operational scripts and demo applications
├── contact_plans/   # Contact plan definitions
├── logs/           # System and event logs
└── data/           # Bundle storage
    └── bundles/    # Persistent bundle storage
```

## Deployment

### Prerequisites

**AMPRNet IP Address Allocation (Required for Production):**

Before deploying to production, you MUST obtain AMPRNet IP addresses (44.x.y.z) for each node:

1. Visit https://portal.ampr.org/
2. Register or log in with your amateur radio callsign
3. Request IP address allocation for your nodes
4. Update all configuration files to replace 192.168.25.x addresses with your allocated AMPRNet addresses

**Note:** The 192.168.25.x addresses in the configuration files are placeholders for development and testing only. They will NOT work for over-the-air amateur radio operation.

### Installation Steps

1. Copy this entire directory to `/opt/ion-demo/` on each target node
2. Ensure proper permissions: `sudo chown -R $USER:$USER /opt/ion-demo`
3. Follow the installation guide in the project documentation
4. Configure node-specific settings in the appropriate config directory

## Node Assignment

- **Node A (Source)**: Use `config/node_a/` configuration (G4DPZ-1)
- **Node B (Relay)**: Use `config/node_b/` configuration (G4DPZ-2)
- **Node C (Destination)**: Use `config/node_c/` configuration (G4DPZ-3)

## Hardware Requirements

**Recommended Setup:**
- 3x Mobilinkd TNC3 (Bluetooth/USB KISS TNC)
- 3x Yaesu FT-817 transceivers (or FT-817ND)
- 3x Raspberry Pi 4 (or similar Linux hosts)
- 3x USB cables (Type-A to Micro-USB for TNC3)
- 3x TNC-to-radio data cables (3.5mm to 6-pin mini-DIN for FT-817)
- 3x Dummy loads (50Ω, 10W minimum) for bench testing
- Antennas for field operation
- 3x Yaesu FT-817 transceivers (HF/VHF/UHF portable)
- 3x Mobilinkd TNC3 (Bluetooth/USB KISS TNC)
- 3x Raspberry Pi 4 (or similar Linux hosts)
- 3x USB cables (for TNC3 to Raspberry Pi connection)
- 3x Audio cables (TNC3 to FT-817 data port)
- Antennas or attenuated bench setup

## Quick Start

See the project documentation for detailed setup and operation instructions.
