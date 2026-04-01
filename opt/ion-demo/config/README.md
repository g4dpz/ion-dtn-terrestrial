# Configuration Files

This directory contains ION-DTN configuration files for each node in the demonstration system.

## Node Directories

- **node_a/**: Source node (ipn:10.x, callsign G0AAA-1)
- **node_b/**: Relay node (ipn:20.x, callsign G0BBB-1)
- **node_c/**: Destination node (ipn:30.x, callsign G0CCC-1)

## Configuration Files

Each node directory should contain:

- `ionconfig`: ION memory and path settings
- `ionrc`: Node identity, contact plan, and range definitions
- `bprc`: Bundle protocol configuration (schemes, endpoints, convergence layers)
- `ipnrc`: IPN routing plans

## Usage

When starting ION on a node, specify the appropriate configuration directory:

```bash
export ION_CONFIG_DIR=/opt/ion-demo/config/node_a
ionstart -I ionconfig
```

## Customization

Before deployment, update the following in each configuration:

- IP addresses to match your network
- Callsigns to match your amateur radio licenses
- Contact plan schedules to match your demonstration timing
- Serial port devices to match your TNC connections
