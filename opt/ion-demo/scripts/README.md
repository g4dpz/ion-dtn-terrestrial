# Scripts

## Node Startup

- **start_node_a.sh** `<serial_device>` — Start ION Node A (ipn:1, G4DPZ-1)
- **start_node_b.sh** `<serial_device>` — Start ION Node B (ipn:2, G4DPZ-2)

These scripts clean previous ION state, substitute the serial device path into ltprc, and run ionadmin/ltpadmin/bpadmin/ipnadmin. ION then launches `ionserialcla` automatically via the ltprc span config.

## Environment Variables

Set before running start scripts:

| Variable | Description |
|----------|-------------|
| `ION_SERIAL_DEBUG=1` | Enable verbose debug logging in ionserialcla |
| `ION_BIN` | Path to ION binaries (auto-detected from `../../ION-DTN/.libs`) |
