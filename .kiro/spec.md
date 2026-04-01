---
title: ION-DTN UHF Amateur Packet Demo
status: draft
---

# ION-DTN Terrestrial Demo over UHF Amateur Packet

Design outline: terrestrial ION-DTN demo over UHF amateur packet using AX.25 / KISS

## Overview

Three-node DTN demonstration using ION over IP-over-AX.25 with UHF packet radios.

## Architecture

```
Application A -> ION Node A -> AX.25/KISS/UHF -> ION Node B (relay) -> AX.25/KISS/UHF -> ION Node C -> Application C
```

## Requirements

- [ ] Configure three Linux nodes with ION-DTN
- [ ] Set up AX.25/KISS TNCs on each node
- [ ] Establish IP-over-AX.25 connectivity
- [ ] Implement point-to-point bundle transfer
- [ ] Implement delayed delivery with link outage
- [ ] Implement store-and-forward relay through Node B
- [ ] Create contact plan for scheduled operations
- [ ] Build demo applications for send/receive

## Tasks

### Phase 1: Bench Link Bring-up
- [ ] Configure TNCs in KISS mode
- [ ] Verify AX.25 packet exchange
- [ ] Validate IP over radio path
- [ ] Test stability at low data rates

### Phase 2: 2-Node ION Test
- [ ] Install ION on two hosts
- [ ] Establish BP connectivity
- [ ] Send small bundles end-to-end

### Phase 3: Delayed Delivery Test
- [ ] Send bundles while link is down
- [ ] Verify bundle queuing
- [ ] Restore link and verify delivery

### Phase 4: 3-Node Relay Test
- [ ] Add Node B as relay
- [ ] Route A to C via B
- [ ] Test store-and-forward behavior

### Phase 5: Scheduled Contact Demo
- [ ] Automate link enable/disable by schedule
- [ ] Demonstrate repeated store-carry-forward

#[[file:spec]]
