# DTN over KISS: Regulatory Compliance with OFCOM Amateur Radio Licence Conditions

## Executive Summary

AMSAT-UK proposes to conduct experimental transmissions using Delay-Tolerant Networking (DTN) protocols over standard 1200 baud VHF amateur packet radio links. DTN is a suite of networking protocols developed by NASA and the IETF for reliable communications over links with long delays and intermittent connectivity, such as deep-space and satellite links. NASA's reference implementation, Interplanetary Overlay Network (ION), has been deployed on the International Space Station and deep-space missions. AMSAT-UK seeks to evaluate these protocols on terrestrial amateur radio as a stepping stone toward their use on future amateur satellite missions.

The system uses standard amateur radio equipment (Yaesu FT-817 transceivers and Mobilinkd TNC3 terminal node controllers) operating within OFCOM-permitted frequency bands and power levels. The software is open-source (available at [https://github.com/g4dpz/ion-dtn-terrestrial](https://github.com/g4dpz/ion-dtn-terrestrial)) and implements a full DTN protocol stack: conventional AX.25 packet radio, Licklider Transmission Protocol (LTP) for reliable data transfer with acknowledgment and retransmission, and Bundle Protocol version 7 (BPv7) for DTN bundle encoding with fragmentation and reassembly. The system has been successfully tested over the air, including bit-perfect transfer of binary files using BPv7 fragmentation over LTP over 1200 baud VHF.

Station identification is maintained at all times in compliance with the OFCOM Amateur Radio Licence (2024 edition). In AX.25 mode, the operator's callsign is embedded in every transmitted frame. In LTP and BPv7 modes, DTN endpoint identifiers containing the operator's callsign (e.g. dtn://g4dpz-1) are used for addressing, supplemented by periodic APRS position beacons containing the operator's callsign in the AX.25 source address, ensuring the station remains identifiable to both conventional packet radio and APRS monitoring equipment.

All transmissions are conducted for the purposes of self-training and technical investigation, in accordance with the terms of the amateur radio licence and ITU Radio Regulations Article 25.

## Document Purpose

This document is a proposal from AMSAT-UK describing an experimental amateur radio data communications system that implements Delay-Tolerant Networking (DTN) protocols over 1200 baud VHF packet radio. It explains how the system conforms to OFCOM amateur radio licence conditions regarding station identification and is intended for presentation to OFCOM to demonstrate regulatory awareness and compliance.

## Proposing Organisation

AMSAT-UK is a registered charity and membership organisation dedicated to the advancement of amateur radio satellite communications, education, and technical research. AMSAT-UK members have a long history of designing, building, and operating amateur radio satellites and ground station systems, and of working constructively with OFCOM on spectrum matters relating to the amateur satellite service.

This proposal arises from AMSAT-UK's interest in applying Delay-Tolerant Networking techniques — originally developed for space communications — to terrestrial amateur radio links, with the goal of building practical experience and tools that can be applied to future amateur satellite missions.

## System Overview

The system implements a layered protocol stack for reliable data transfer between amateur radio stations over RF links characterised by long propagation delays and intermittent connectivity. The hardware consists of standard amateur radio equipment (Yaesu FT-817 transceivers) connected to Mobilinkd TNC3 terminal node controllers via USB, driven by custom open-source software running on Linux computers (Ubuntu laptops and Raspberry Pi).

The protocol stack, from lowest to highest layer:

1. RF physical layer: 1200 baud AFSK on VHF amateur bands
2. KISS framing: host-to-TNC serial protocol for packet delineation
3. AX.25 UI frames: standard amateur packet radio addressing and APRS beacons
4. LTP (Licklider Transmission Protocol): reliable data transfer with checkpoint/report acknowledgment
5. BPv7 (Bundle Protocol version 7): DTN bundle encoding with CBOR serialisation, fragmentation, and reassembly
6. Application data: user messages, ping payloads, or file transfers

## Applicable Regulations and Standards

### OFCOM Regulations

- OFCOM Amateur Radio Licence Conditions Booklet (OFW611), February 2024 edition. This is the primary regulatory document governing amateur radio operation in the United Kingdom. ([Source: OFCOM](https://www.ofcom.org.uk/manage-your-licence/radiocommunication-licences/amateur-radio/amateur-radio-info))

- OFCOM Interface Requirement IR 2028: "UK Interface Requirement for Radio Equipment in the Amateur and Amateur Satellite Services." This defines the technical parameters for amateur radio equipment. ([Source: OFCOM](https://www.ofcom.org.uk/manage-your-licence/radiocommunication-licences/regulations-technical-reference))

- Wireless Telegraphy Act 2006 (WTA 2006): The primary UK legislation governing the use of radio spectrum. ([Source: UK Government](https://www.legislation.gov.uk/ukpga/2006/36))

### International Regulations

- ITU Radio Regulations, Article 25: "Amateur Service and Amateur-Satellite Service." Provision 25.1 governs radiocommunications between amateur stations of different countries. Provision 25.6 requires that amateur operators demonstrate operational and technical qualifications. ([Source: ITU](https://www.itu.int/pub/T-SP-RR.25.1))

### Protocol Standards

- AX.25 Link Access Protocol for Amateur Packet Radio, Version 2.2 (July 1998). Published by the Tucson Amateur Packet Radio Corporation (TAPR). This is the standard amateur radio data link protocol used in the current implementation. ([Source: TAPR](https://web.tapr.org/tech_docs/AX25/ax25.doc))

- KISS Protocol: "A Simple Host-to-TNC Communications Protocol," by Mike Chepponis, K3MC, and Phil Karn, KA9Q (August 1986). Published by TAPR. Defines the serial framing protocol between host computer and TNC hardware. ([Source: TAPR](https://files.tapr.org/tech_docs/Packet/kiss.txt))

- RFC 5326: "Licklider Transmission Protocol — Specification," by M. Ramadas, S. Burleigh, and S. Farrell (September 2008). Defines the LTP reliable transport protocol for delay-tolerant links. ([Source: IETF](https://www.rfc-editor.org/rfc/rfc5326))

- RFC 5325: "Licklider Transmission Protocol — Motivation," by S. Burleigh, M. Ramadas, and S. Farrell (September 2008). Describes the rationale for LTP over high-delay links. ([Source: IETF](https://www.rfc-editor.org/rfc/rfc5325))

- RFC 9171: "Bundle Protocol Version 7," by S. Burleigh, K. Fall, and E. Birrane (January 2022). Defines the DTN Bundle Protocol for store-and-forward networking. ([Source: IETF](https://www.rfc-editor.org/rfc/rfc9171))

- RFC 4838: "Delay-Tolerant Networking Architecture," by V. Cerf et al. (April 2007). Defines the overall DTN architecture. ([Source: IETF](https://www.rfc-editor.org/rfc/rfc4838))

- CCSDS 734.1-B-1: "Licklider Transmission Protocol (LTP) for CCSDS." The Consultative Committee for Space Data Systems standard for LTP, used by NASA and ESA for deep-space communications. ([Source: CCSDS](https://public.ccsds.org/Pubs/734x1b1.pdf))

### Reference Implementations

- ION (Interplanetary Overlay Network): NASA/JPL's reference implementation of DTN protocols, including BP and LTP. ION is deployed on the International Space Station and has been used on deep-space missions including DINET (Deep Impact Network Experiment). ION is open-source and maintained by NASA/JPL. ([Source: NASA/JPL](https://sourceforge.net/projects/ion-dtn/))

## Station Identification Compliance

### OFCOM Licence Requirement

The OFCOM Amateur Radio Licence (2024 edition) requires that a station must be clearly identifiable at all times during transmission, and that the licensee must identify the station as frequently as practicable using the assigned callsign. Content rephrased for compliance with licensing restrictions.

The 2024 licence revision replaced the previous prescriptive "every 15 minutes" rule with a principles-based approach requiring stations to be clearly identifiable at all times. ([Source: Essex Ham](https://www.essexham.co.uk/ofcom-updated-amateur-licence-guidance.html))

### How This System Complies

#### Current Implementation (AX.25 Mode)

In the current AX.25-based implementation, every transmitted packet contains the operator's callsign embedded in the AX.25 frame header as both source and destination address fields. This is inherent to the AX.25 protocol design.

Each AX.25 UI frame contains:
- Destination address field (7 bytes): the remote station's callsign and SSID
- Source address field (7 bytes): the transmitting station's callsign and SSID

For example, a transmission from station G4DPZ-1 to G4DPZ-2 carries both callsigns in every single packet transmitted over the air. The callsigns are encoded in the standard AX.25 address format (characters left-shifted by one bit, per AX.25 v2.2 Section 2.2.13), and are decodable by any standard packet radio monitoring equipment.

This means the station is identified in every transmission, exceeding the OFCOM requirement for identification "as frequently as practicable."

The system's CLI requires the operator to specify their callsign explicitly:
```
./kiss_interface send --device /dev/ttyACM0 --src G4DPZ-1 --dst G4DPZ-2 "message"
```

#### LTP over KISS Mode (Implemented)

In the LTP-over-KISS implementation, the AX.25 framing layer is replaced by LTP segments encapsulated directly in KISS frames. LTP uses numeric Engine IDs rather than callsign strings in its segment headers.

To maintain compliance with OFCOM identification requirements, the system uses DTN endpoint identifiers that embed the operator's callsign:

```
dtn://g4dpz-1
```

The callsign is mapped to a numeric Engine ID using a deterministic hash function. The mapping between DTN endpoint identifier and Engine ID is maintained by both communicating stations, ensuring that any received LTP segment can be traced back to the originating callsign.

Additionally, the system implements periodic APRS position beacons during LTP sessions. At configurable intervals (default: every 2 minutes), the system transmits a standard AX.25 UI frame containing the operator's callsign as the source address, the APRS experimental TOCALL "APZ001" as the destination, and an APRS-formatted position report in the information field. For example:

```
!5228.02N/00201.32W-github.com/g4dpz/ion-dtn-terrestrial
```

These beacons are transmitted during idle periods when no LTP data segments are in-flight, respecting the half-duplex constraint of the radio link. The beacon is deferred by no more than 10 seconds if an LTP exchange is in progress, ensuring the station remains identifiable within the configured interval.

This APRS beacon approach provides three compliance benefits:
1. The operator's callsign appears in the AX.25 source address of every beacon, satisfying the OFCOM requirement that the station be "clearly identifiable at all times"
2. The beacon is decodable by any standard APRS monitoring software (e.g. direwolf, Xastir, APRS-IS), making the station visible to the wider amateur radio community
3. The beacon interval of 2 minutes exceeds the identification frequency that would be considered "as frequently as practicable" for a data station

The beacon can also be run as a standalone mode without LTP:
```
./kiss_interface beacon --device /dev/ttyACM0 --callsign G4DPZ-1 --lat 52.467 --lon -2.022
```

Or integrated with LTP receive mode:
```
./kiss_interface ltp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 --beacon --callsign G4DPZ-2 --lat 52.467 --lon -2.022
```

#### BPv7 over LTP Mode (Implemented)

The Bundle Protocol version 7 (BPv7) layer sits above LTP, providing DTN bundle encoding with CBOR serialisation, fragmentation, and reassembly. BPv7 bundles use DTN endpoint identifiers (e.g. `dtn://g4dpz-1`) for source and destination addressing, with the operator's callsign embedded in the endpoint URI.

For payloads larger than 800 bytes, the system automatically fragments the bundle into multiple BPv7 fragments, each delivered reliably via a separate LTP session. The receiving station reassembles fragments into the complete bundle. This has been successfully tested with binary file transfers (PDF documents) over the air.

APRS beacon identification is supported in BPv7 mode using the same `--beacon` flag. For `bp-send`, beacons are transmitted at the start and end of the transfer sequence. For `bp-recv`, beacons are transmitted periodically while waiting for incoming bundles, with APRS decode of received beacons from the remote station.

```
./kiss_interface bp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 \
  --owlt 6000 --beacon --callsign G4DPZ-1 --lat 52.467 --lon -2.022 \
  --file document.pdf
./kiss_interface bp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 \
  --owlt 6000 --beacon --callsign G4DPZ-2 --lat 52.467 --lon -2.022 \
  --outdir ./received/
```

The CLI requires the operator to specify their DTN endpoint (which contains their callsign):
```
./kiss_interface ltp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 "message"
```

### Summary of Identification Mechanisms

| Mode | Identification Method | Frequency |
|------|----------------------|-----------|
| AX.25 send/receive/echo | Callsign in every AX.25 frame header | Every packet |
| AX.25 ping | Callsign in every AX.25 frame header | Every ping/reply |
| LTP send/receive | DTN endpoint ID containing callsign + periodic APRS beacon | Every session + every 2 minutes |
| BPv7 send | DTN endpoint ID containing callsign + APRS beacon at start and end of transfer | Every transfer |
| BPv7 receive | DTN endpoint ID containing callsign + periodic APRS beacon | Every 2 minutes (configurable) |
| Standalone beacon | Callsign in AX.25 source address + APRS position report | Every 2 minutes (configurable) |

## Purpose of the Experiment

This system is being developed for the purpose of self-training and technical investigation, which are recognised purposes under the OFCOM amateur radio licence and ITU Radio Regulations Article 25. Specifically:

1. Investigating the application of Delay-Tolerant Networking protocols (originally developed by NASA/JPL for deep-space communications, and implemented in NASA's ION software suite) to terrestrial amateur radio links
2. Evaluating the performance of the Licklider Transmission Protocol and Bundle Protocol version 7 over 1200 baud VHF packet radio
3. Demonstrating reliable file transfer using BPv7 fragmentation over LTP over constrained RF links
4. Measuring round-trip times and link reliability characteristics for constrained RF links
5. Developing open-source tools for the amateur radio community (source code: [https://github.com/g4dpz/ion-dtn-terrestrial](https://github.com/g4dpz/ion-dtn-terrestrial))

All transmissions are non-commercial, between licensed amateur radio stations, using standard amateur radio equipment operating within the permitted frequency bands and power levels specified in the OFCOM licence.

## Equipment

| Component | Description |
|-----------|-------------|
| Transceiver | Yaesu FT-817 (5W VHF/UHF, operating within OFCOM-permitted bands and power levels) |
| TNC | Mobilinkd TNC3 (USB KISS TNC, 1200 baud AFSK) |
| Computer | Ubuntu Linux laptop / Raspberry Pi |
| Software | Custom open-source C tool (`kiss_interface`), licensed under the Apache License 2.0. Source code: [https://github.com/g4dpz/ion-dtn-terrestrial](https://github.com/g4dpz/ion-dtn-terrestrial) |

## Contact

This proposal is submitted on behalf of AMSAT-UK.

Licensee callsign: G4DPZ
Author: David Johnson, G4DPZ, Hon. Sec. AMSAT-UK
Organisation: AMSAT-UK
Website: [https://amsat-uk.org](https://amsat-uk.org)

---

Document version: 2.0
Date: April 2026
