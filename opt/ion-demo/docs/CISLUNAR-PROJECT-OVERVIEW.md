# ION-DTN Cislunar Demonstration — Project Overview

## What We're Building

A phased demonstration of Delay-Tolerant Networking (DTN) using NASA JPL's ION implementation, progressing from terrestrial amateur packet radio through a CubeSat in LEO to a cislunar payload operating at lunar distances.

The project is led by AMSAT-UK and AMSAT-DL, and is open to amateur radio operators, university research groups, and space agencies worldwide.

## Why DTN?

Current space communication uses point-to-point links scheduled by mission control. As we expand beyond Earth orbit, a store-and-forward networking layer becomes essential. DTN provides this — bundles of data are stored at intermediate nodes and forwarded when links become available, enabling reliable communication across disrupted, delayed, and intermittent links.

ION-DTN is the same software stack running on the International Space Station and deep space missions. This project makes it accessible to the amateur and academic communities.

## Three Phases

### Phase 1 — Terrestrial Testbed (Active Now)

We have a working two-node ION-DTN network transferring files over 1200 baud UHF amateur packet radio using Mobilinkd TNC3 devices and Yaesu FT-817 transceivers. The stack is:

```
ION Bundle Protocol → LTP → ionserialcla (custom CLA) → AX.25/KISS → TNC → RF
```

What works today:
- End-to-end file transfer over RF at 1200 baud
- LTP with retransmission and acknowledgment over half-duplex packet radio
- AX.25 framing with proper callsign identification (G4DPZ-1/G4DPZ-2)
- TX pacing calibrated for 1200 baud RF rate
- Custom integrated CLA (`ionserialcla`) linking directly against ION's libltp

What's next for Phase 1:
- 9600 baud support (hardware supports it, needs cable/config work)
- M17 protocol support via Mobilinkd TNC4 (on order)
- Three-node relay demo (A → B → C store-and-forward)
- Delayed delivery demo (send while receiver offline)
- Web-based mission operations dashboard

### Phase 2 — CubeSat (Conditional)

ION-DTN payload hosted on an amateur CubeSat in LEO. Ground stations communicate via UHF at 9600 bps (BPSK). Federated amateur ground station network provides pass coverage. Conditional on finding a suitable CubeSat host.

### Phase 3 — Cislunar Payload (Proposal)

DTN communication at lunar distances with ~2.5 second round-trip delay and periodic link occlusion. Validates protocol performance under the most challenging Earth-Moon conditions. Subject to ESA ARTES programme review.

## How You Can Contribute

The project needs people with skills in:

- **Amateur radio / packet radio** — ground station operators, TNC experience, AX.25/KISS
- **Embedded systems** — ION-DTN on constrained platforms (Raspberry Pi, flight computers)
- **SDR / GNU Radio** — software-defined radio links, BPSK modems
- **FPGA** — hardware acceleration for space-grade implementations
- **Ground station networks** — SatNOGS integration, federated ground segment
- **Web development** — mission operations dashboard, public telemetry display
- **Testing** — property-based testing, protocol conformance, link simulation

### Immediate Opportunities

1. **Set up a terrestrial node** — Raspberry Pi + Mobilinkd TNC + UHF radio. All software is open source.
2. **Help with 9600 baud testing** — we have the hardware, need to validate the RF link
3. **M17 protocol integration** — the TNC4 supports M17 with 9600 bps and FEC, needs CLA adaptation
4. **Three-node relay** — configure and test store-and-forward through an intermediate node
5. **Ground station preparation** — plan the federated ground segment for Phase 2

## Technical Details

- **Protocol**: ION-DTN (C), Bundle Protocol v7 (RFC 9171), LTP, Contact Graph Routing
- **Radio**: AX.25/KISS over FM/AFSK (1200 baud), BPSK (9600 bps) planned
- **Hardware**: Raspberry Pi / Mac, Mobilinkd TNC3/TNC4, Yaesu FT-817
- **Licence**: Apache License 2.0 — all software, hardware designs, and data are open

## Repositories

- Terrestrial demo: https://github.com/g4dpz/ion-dtn-terrestrial
- Cislunar project: https://github.com/g4dpz/ion-dtn-cislunar

## Contact

Project leads at AMSAT-UK and AMSAT-DL. Join the discussion at https://esa-competition.amsat-uk.org/discussions/thread/14
