# Contact Plans

This directory contains contact plan definitions for different demonstration scenarios.

## Contact Plan Files

- `continuous.plan`: Continuous contact for Phase 2 testing (two-node)
- `delayed.plan`: Scheduled outage and restoration for delayed delivery demo
- `relay.plan`: Non-overlapping windows for three-node relay demo
- `interrupted.plan`: Mid-transfer interruption scenarios

## Contact Plan Format

Contact plans use ION format with the following structure:

```
# Contact definition: start_time duration from_node to_node data_rate
a contact +0 +300 10 20 100000
a contact +600 +900 20 30 100000

# Range definition: start_time duration from_node to_node distance
a range +0 +3600 10 20 1
a range +0 +3600 20 30 1
```

## Time Format

- Relative times: `+seconds` (e.g., `+300` = 300 seconds from now)
- Absolute times: Unix timestamp

## Node Numbers

- Node A (Source): 10
- Node B (Relay): 20
- Node C (Destination): 30

## Loading Contact Plans

Use the load_contact_plan.sh script:

```bash
/opt/ion-demo/scripts/load_contact_plan.sh relay.plan
```

## Creating Custom Plans

Copy an existing plan and modify the contact windows to match your demonstration timing requirements.
