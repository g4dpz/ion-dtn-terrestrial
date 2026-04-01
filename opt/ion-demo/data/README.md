# Data Directory

This directory contains persistent data storage for the ION-DTN system.

## Subdirectories

- `bundles/`: Persistent bundle storage for ION-DTN

## Bundle Storage

ION-DTN stores bundles persistently in this directory to survive:
- Link outages
- Node restarts
- Power cycles

The bundle storage ensures store-and-forward capability and delayed delivery.

## Storage Management

Monitor storage usage:

```bash
du -sh /opt/ion-demo/data/bundles/
```

Clean bundle storage (when ION is stopped):

```bash
rm -rf /opt/ion-demo/data/bundles/*
```

## Storage Requirements

- Minimum: 100MB for demonstration purposes
- Recommended: 1GB for extended operations
- Each bundle: typically <1KB for demo payloads

## Permissions

Ensure the data directory is writable by the ION process:

```bash
chmod 755 /opt/ion-demo/data
chmod 755 /opt/ion-demo/data/bundles
```

## Backup

For important demonstrations, consider backing up bundle storage:

```bash
tar czf bundles-backup.tar.gz /opt/ion-demo/data/bundles/
```
