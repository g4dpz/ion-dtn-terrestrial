# Requirements Document

## Introduction

This document specifies requirements for a terrestrial ION-DTN demonstration system operating over UHF amateur packet radio using AX.25/KISS protocols. The system demonstrates delay-tolerant networking capabilities including point-to-point delayed delivery and multi-hop store-and-forward relay operations. The demonstration uses a three-node topology (source, relay, destination) with scheduled contact windows to simulate satellite-pass operations and prove that applications can function without continuous end-to-end connectivity.

## Glossary

- **ION_Node**: An instance of ION-DTN software running on a Linux host
- **TNC**: Terminal Node Controller operating in KISS mode, interfacing between host and radio
- **Source_Node**: ION_Node A, the originator of bundles (Node A)
- **Relay_Node**: ION_Node B, the intermediate store-and-forward node (Node B)
- **Destination_Node**: ION_Node C, the final recipient of bundles (Node C)
- **Bundle**: A DTN protocol data unit containing application payload and metadata
- **Contact_Window**: A scheduled time period during which a radio link is available
- **Demo_Application**: A simple application that sends or receives bundles for demonstration purposes
- **KISS_Interface**: The serial or USB interface between host and TNC using KISS protocol
- **AX25_Link**: The amateur packet radio link layer using AX.25 protocol
- **RF_Path**: The UHF radio frequency communication path between nodes
- **Store_And_Forward**: The capability to receive, persist, and later transmit bundles
- **BP**: Bundle Protocol, the DTN protocol layer
- **Convergence_Layer**: The ION component that adapts BP to underlying transport (UDP/TCP)
- **Contact_Plan**: The schedule defining when links between nodes are available

## Requirements

### Requirement 1: Three-Node Network Topology

**User Story:** As a demonstration operator, I want a three-node network topology, so that I can demonstrate multi-hop store-and-forward DTN operations.

#### Acceptance Criteria

1. THE System SHALL consist of exactly three ION_Nodes: Source_Node, Relay_Node, and Destination_Node
2. THE Source_Node SHALL connect to the Relay_Node via an AX25_Link
3. THE Relay_Node SHALL connect to the Destination_Node via a separate AX25_Link
4. THE System SHALL NOT provide a direct link between Source_Node and Destination_Node

### Requirement 2: ION-DTN Software Installation

**User Story:** As a system integrator, I want ION-DTN installed on each node, so that the nodes can perform delay-tolerant networking operations.

#### Acceptance Criteria

1. THE Source_Node SHALL run ION-DTN software
2. THE Relay_Node SHALL run ION-DTN software
3. THE Destination_Node SHALL run ION-DTN software
4. WHEN an ION_Node starts, THE ION_Node SHALL initialize with a unique BP node identity
5. THE Source_Node SHALL use BP node identity in the ipn:10.x range
6. THE Relay_Node SHALL use BP node identity in the ipn:20.x range
7. THE Destination_Node SHALL use BP node identity in the ipn:30.x range

### Requirement 3: UHF Amateur Packet Radio Links

**User Story:** As a demonstration operator, I want UHF amateur packet radio links between nodes, so that I can demonstrate DTN over real RF paths.

#### Acceptance Criteria

1. THE Source_Node SHALL connect to a UHF transceiver via a TNC
2. THE Relay_Node SHALL connect to one or two UHF transceivers via TNCs
3. THE Destination_Node SHALL connect to a UHF transceiver via a TNC
4. WHEN operating in bench configuration, THE System SHALL use dummy loads or attenuated low-power transmission
5. WHEN operating in field configuration, THE System SHALL use over-the-air UHF packet transmission
6. THE RF_Path SHALL operate in simplex or half-duplex mode

### Requirement 4: KISS-Mode TNC Interface

**User Story:** As a system integrator, I want TNCs operating in KISS mode, so that the host can send and receive AX.25 frames directly.

#### Acceptance Criteria

1. THE TNC SHALL operate in KISS mode
2. THE TNC SHALL expose a serial or USB-serial interface to the host
3. WHEN the host sends a KISS frame, THE TNC SHALL transmit the corresponding AX.25 packet
4. WHEN the TNC receives an AX.25 packet, THE TNC SHALL deliver it to the host as a KISS frame
5. THE KISS_Interface SHALL maintain stable operation at the configured data rate

### Requirement 5: AX.25 Link Layer

**User Story:** As a system integrator, I want AX.25 protocol support, so that nodes can communicate using standard amateur packet radio protocols.

#### Acceptance Criteria

1. THE System SHALL use AX.25 as the link layer protocol
2. THE Source_Node SHALL have a unique amateur callsign and SSID for AX.25 addressing
3. THE Relay_Node SHALL have a unique amateur callsign and SSID for AX.25 addressing
4. THE Destination_Node SHALL have a unique amateur callsign and SSID for AX.25 addressing
5. WHEN a node transmits, THE node SHALL use its assigned callsign and SSID in the AX.25 frame

### Requirement 6: IP-over-AX.25 Transport

**User Story:** As a system integrator, I want IP carried over AX.25, so that ION can use standard UDP/TCP convergence layers without custom development.

#### Acceptance Criteria

1. THE System SHALL carry IP packets over the AX25_Link
2. WHEN an ION_Node sends IP packets, THE AX25_Link SHALL encapsulate them in AX.25 frames
3. WHEN an ION_Node receives AX.25 frames containing IP packets, THE ION_Node SHALL extract and process the IP packets
4. THE Convergence_Layer SHALL use UDP or TCP over the IP-over-AX.25 path
5. WHEN the RF_Path is available, THE System SHALL successfully transfer IP packets between connected nodes

### Requirement 7: Bundle Creation and Transmission

**User Story:** As a demonstration operator, I want the source node to create and transmit bundles, so that I can demonstrate DTN message origination.

#### Acceptance Criteria

1. WHEN a Demo_Application on Source_Node provides data, THE Source_Node SHALL create a Bundle containing that data
2. THE Source_Node SHALL address the Bundle to the Destination_Node using its BP node identity
3. WHEN a Contact_Window to Relay_Node is available, THE Source_Node SHALL transmit the Bundle toward Relay_Node
4. THE Bundle SHALL include creation timestamp metadata
5. THE Bundle SHALL include source and destination endpoint identifiers

### Requirement 8: Bundle Reception and Storage

**User Story:** As a demonstration operator, I want nodes to receive and persistently store bundles, so that bundles survive link outages and node operations.

#### Acceptance Criteria

1. WHEN a Bundle arrives at an ION_Node, THE ION_Node SHALL receive the Bundle
2. WHEN a Bundle is received, THE ION_Node SHALL store the Bundle persistently to local storage
3. THE stored Bundle SHALL survive ION_Node restart
4. WHEN a Bundle is successfully stored, THE ION_Node SHALL retain it until successful forwarding or delivery
5. THE ION_Node SHALL maintain a queue of stored bundles awaiting transmission

### Requirement 9: Relay Store-and-Forward Operation

**User Story:** As a demonstration operator, I want the relay node to store received bundles and forward them later, so that I can demonstrate multi-hop DTN relay behavior.

#### Acceptance Criteria

1. WHEN Relay_Node receives a Bundle destined for Destination_Node, THE Relay_Node SHALL store the Bundle
2. WHILE the Contact_Window to Destination_Node is unavailable, THE Relay_Node SHALL retain the Bundle in persistent storage
3. WHEN a Contact_Window to Destination_Node becomes available, THE Relay_Node SHALL forward the stored Bundle to Destination_Node
4. WHEN the Bundle is successfully forwarded, THE Relay_Node SHALL remove it from the queue
5. THE Relay_Node SHALL maintain queue depth information showing bundles awaiting forwarding

### Requirement 10: Bundle Delivery to Application

**User Story:** As a demonstration operator, I want the destination node to deliver bundles to the local application, so that I can demonstrate successful end-to-end DTN delivery.

#### Acceptance Criteria

1. WHEN Destination_Node receives a Bundle addressed to it, THE Destination_Node SHALL deliver the Bundle payload to the local Demo_Application
2. THE delivered payload SHALL match the original data provided by the source Demo_Application
3. THE Destination_Node SHALL provide delivery timestamp information to the Demo_Application
4. WHEN a Bundle is successfully delivered, THE Destination_Node SHALL remove it from storage

### Requirement 11: Scheduled Contact Windows

**User Story:** As a demonstration operator, I want scheduled contact windows between nodes, so that I can simulate satellite-pass operations and demonstrate DTN behavior during link outages.

#### Acceptance Criteria

1. THE System SHALL implement a Contact_Plan defining when each RF_Path is available
2. WHEN a Contact_Window is scheduled as available, THE corresponding RF_Path SHALL be enabled
3. WHEN a Contact_Window is scheduled as unavailable, THE corresponding RF_Path SHALL be disabled
4. THE Contact_Plan SHALL support non-overlapping windows for Source_Node-to-Relay_Node and Relay_Node-to-Destination_Node links
5. THE Contact_Plan SHALL support time gaps between contact windows to demonstrate store-and-forward behavior

### Requirement 12: Point-to-Point Real-Time Demonstration Mode

**User Story:** As a demonstration operator, I want to demonstrate point-to-point real-time transfer, so that I can prove the basic protocol stack functions correctly.

#### Acceptance Criteria

1. WHEN a two-node configuration is active with a continuous Contact_Window, THE System SHALL support immediate Bundle transfer
2. WHEN Source_Node sends a Bundle, THE Bundle SHALL arrive at the directly-connected node within the RF_Path transmission time
3. THE System SHALL successfully demonstrate this mode between Source_Node and Relay_Node
4. THE System SHALL successfully demonstrate this mode between Relay_Node and Destination_Node

### Requirement 13: Delayed Point-to-Point Demonstration Mode

**User Story:** As a demonstration operator, I want to demonstrate delayed point-to-point delivery, so that I can prove DTN queuing and delayed transmission capabilities.

#### Acceptance Criteria

1. WHEN the RF_Path is disabled and a Bundle is sent, THE Source_Node SHALL queue the Bundle locally
2. WHILE the RF_Path remains disabled, THE Bundle SHALL remain in the queue
3. WHEN the RF_Path is restored, THE Source_Node SHALL transmit the queued Bundle
4. THE Bundle SHALL be successfully delivered after the delay
5. THE System SHALL record both creation time and delivery time to demonstrate the delay

### Requirement 14: Relay Store-and-Forward Demonstration Mode

**User Story:** As a demonstration operator, I want to demonstrate multi-hop relay with store-and-forward, so that I can prove DTN relay capabilities across separate contact windows.

#### Acceptance Criteria

1. WHEN Source_Node sends a Bundle to Destination_Node via Relay_Node, THE Bundle SHALL be received by Relay_Node during the first Contact_Window
2. WHILE the Relay_Node-to-Destination_Node Contact_Window is unavailable, THE Relay_Node SHALL store the Bundle
3. WHEN the Relay_Node-to-Destination_Node Contact_Window becomes available, THE Relay_Node SHALL forward the Bundle
4. THE Bundle SHALL be successfully delivered to Destination_Node
5. THE System SHALL demonstrate that Source_Node and Destination_Node never have simultaneous connectivity

### Requirement 15: Interrupted Forwarding Demonstration Mode

**User Story:** As a demonstration operator, I want to demonstrate interrupted forwarding with later completion, so that I can prove DTN resilience to mid-transfer link failures.

#### Acceptance Criteria

1. WHEN Bundle forwarding begins and the RF_Path is interrupted mid-transfer, THE transmitting ION_Node SHALL retain the Bundle state
2. WHILE the RF_Path remains unavailable, THE ION_Node SHALL maintain the Bundle in the queue
3. WHEN the RF_Path is restored, THE ION_Node SHALL reattempt Bundle transmission
4. THE Bundle SHALL be successfully delivered on the subsequent contact
5. THE System SHALL demonstrate recovery from at least one mid-transfer interruption

### Requirement 16: Small Payload Support

**User Story:** As a demonstration operator, I want to send small payloads, so that demonstrations complete quickly and avoid fragmentation issues.

#### Acceptance Criteria

1. THE Demo_Application SHALL support sending timestamped text messages as Bundle payloads
2. THE Demo_Application SHALL support sending small JSON objects as Bundle payloads
3. WHERE small image thumbnails are used, THE Demo_Application SHALL support sending them as Bundle payloads
4. WHERE log files are used, THE Demo_Application SHALL support sending small log files as Bundle payloads
5. THE System SHALL successfully transfer payloads up to 1 kilobyte in size

### Requirement 17: Relay Node Status Monitoring

**User Story:** As a demonstration operator, I want to monitor relay node status, so that I can observe store-and-forward behavior during demonstrations.

#### Acceptance Criteria

1. THE Relay_Node SHALL provide information on the number of bundles received
2. THE Relay_Node SHALL provide information on the number of bundles currently queued
3. THE Relay_Node SHALL provide information on the status of each next-hop link
4. THE Relay_Node SHALL provide information on the number of bundles successfully forwarded
5. WHERE a display interface is implemented, THE Relay_Node SHALL present this information in human-readable form

### Requirement 18: Linux Host Platform

**User Story:** As a system integrator, I want each node to run on a Linux host, so that I can use standard Linux networking and ION-DTN software.

#### Acceptance Criteria

1. THE Source_Node SHALL run on a Linux operating system
2. THE Relay_Node SHALL run on a Linux operating system
3. THE Destination_Node SHALL run on a Linux operating system
4. THE Linux host SHALL support AX.25 utilities and kernel support
5. THE Linux host SHALL provide persistent storage for Bundle retention

### Requirement 19: Bench Testing Configuration

**User Story:** As a system integrator, I want a bench testing configuration, so that I can validate the system in a controlled environment before field deployment.

#### Acceptance Criteria

1. WHERE bench configuration is used, THE System SHALL operate with all three nodes in close proximity
2. WHERE bench configuration is used, THE radios SHALL transmit into dummy loads or use very low power with attenuation
3. WHEN operating in bench configuration, THE System SHALL provide an interference-free environment
4. THE bench configuration SHALL validate TNC operation, KISS framing, AX.25 addressing, IP connectivity, and ION routing
5. THE System SHALL successfully complete all four demonstration modes in bench configuration

### Requirement 20: Field Demonstration Configuration

**User Story:** As a demonstration operator, I want a field demonstration configuration, so that I can demonstrate the system over real geographic separation and RF paths.

#### Acceptance Criteria

1. WHERE field configuration is used, THE Source_Node and Relay_Node SHALL be geographically separated
2. WHERE field configuration is used, THE Relay_Node and Destination_Node SHALL be geographically separated
3. WHEN operating in field configuration, THE System SHALL use over-the-air UHF packet transmission
4. THE field configuration SHALL demonstrate true disrupted operations with scheduled forwarding
5. THE field configuration SHALL demonstrate relay persistence across real RF paths

### Requirement 21: Automated Contact Plan Execution

**User Story:** As a demonstration operator, I want automated contact plan execution, so that I can demonstrate repeated store-and-forward cycles without manual intervention.

#### Acceptance Criteria

1. THE System SHALL support automated enabling and disabling of RF_Paths according to the Contact_Plan schedule
2. WHEN a Contact_Window begins, THE System SHALL automatically enable the corresponding RF_Path
3. WHEN a Contact_Window ends, THE System SHALL automatically disable the corresponding RF_Path
4. THE System SHALL execute the Contact_Plan for multiple cycles without manual intervention
5. THE System SHALL maintain accurate timing for Contact_Window transitions within 1 second

### Requirement 22: Demonstration Applications

**User Story:** As a demonstration operator, I want simple demonstration applications, so that I can easily send and receive bundles during demonstrations.

#### Acceptance Criteria

1. THE Source_Node SHALL provide a Demo_Application that sends bundles
2. THE Destination_Node SHALL provide a Demo_Application that receives and logs bundles
3. WHEN the send Demo_Application is invoked with a message, THE Demo_Application SHALL create a Bundle containing that message
4. WHEN the receive Demo_Application receives a Bundle, THE Demo_Application SHALL log the payload, creation timestamp, and delivery timestamp
5. THE Demo_Applications SHALL use simple command-line or scripted interfaces

### Requirement 23: Timestamped Message Payloads

**User Story:** As a demonstration operator, I want timestamped message payloads, so that I can clearly show creation time versus delivery time during demonstrations.

#### Acceptance Criteria

1. WHEN a Demo_Application creates a message payload, THE payload SHALL include a creation timestamp
2. THE creation timestamp SHALL use ISO 8601 format
3. THE payload SHALL include an origin node identifier
4. THE payload SHALL include a message sequence number or identifier
5. WHEN a message is received, THE System SHALL display both creation timestamp and delivery timestamp

### Requirement 24: System Monitoring and Logging

**User Story:** As a demonstration operator, I want system monitoring and logging capabilities, so that I can troubleshoot issues and verify correct operation.

#### Acceptance Criteria

1. THE ION_Node SHALL provide tools to inspect Bundle queues
2. THE ION_Node SHALL provide tools to inspect routing tables and contact schedules
3. THE System SHALL provide tools to check TNC and KISS_Interface status
4. THE System SHALL provide tools to check AX25_Link status
5. THE ION_Node SHALL log significant events including Bundle reception, storage, forwarding, and delivery

### Requirement 25: Phased Implementation Approach

**User Story:** As a system integrator, I want a phased implementation approach, so that I can incrementally build and validate the system.

#### Acceptance Criteria

1. THE System SHALL support Phase 1: bench link bring-up with two TNCs validating AX.25 and IP connectivity
2. THE System SHALL support Phase 2: two-node ION test establishing BP connectivity and sending small bundles
3. THE System SHALL support Phase 3: delayed-delivery test demonstrating queuing and later delivery
4. THE System SHALL support Phase 4: three-node relay test with store-and-forward via Relay_Node
5. THE System SHALL support Phase 5: scheduled-contact demonstration with automated repeated cycles

### Requirement 26: Link Stability and Data Rate

**User Story:** As a system integrator, I want stable link operation at configured data rates, so that the demonstration operates reliably.

#### Acceptance Criteria

1. WHEN a Contact_Window is active, THE RF_Path SHALL maintain stable connectivity
2. THE KISS_Interface SHALL operate without frame loss at the configured data rate
3. THE AX25_Link SHALL successfully transfer IP packets without excessive retransmission
4. WHEN operating in bench configuration, THE System SHALL achieve link stability exceeding 95% successful packet delivery
5. THE System SHALL use data rates appropriate for UHF amateur packet radio (typically 1200 or 9600 baud)

### Requirement 27: Unique Node Addressing

**User Story:** As a system integrator, I want unique addressing at both AX.25 and BP layers, so that routing and delivery function correctly.

#### Acceptance Criteria

1. THE Source_Node SHALL have a unique amateur callsign and SSID distinct from other nodes
2. THE Relay_Node SHALL have a unique amateur callsign and SSID distinct from other nodes
3. THE Destination_Node SHALL have a unique amateur callsign and SSID distinct from other nodes
4. THE Source_Node SHALL have a unique BP node identity distinct from other nodes
5. THE Relay_Node SHALL have a unique BP node identity distinct from other nodes
6. THE Destination_Node SHALL have a unique BP node identity distinct from other nodes

### Requirement 28: Persistent Bundle Storage

**User Story:** As a demonstration operator, I want persistent bundle storage, so that bundles survive node restarts and power cycles.

#### Acceptance Criteria

1. WHEN an ION_Node stores a Bundle, THE Bundle SHALL be written to persistent storage
2. WHEN an ION_Node restarts, THE ION_Node SHALL recover all stored bundles from persistent storage
3. THE stored Bundle data SHALL include all metadata necessary for forwarding and delivery
4. WHEN a Bundle is successfully forwarded or delivered, THE ION_Node SHALL remove it from persistent storage
5. THE persistent storage SHALL survive host power cycles

### Requirement 29: Demonstration Success Criteria

**User Story:** As a demonstration operator, I want clear success criteria, so that I can determine if the demonstration achieved its goals.

#### Acceptance Criteria

1. FOR minimum success, THE System SHALL transfer at least one Bundle point-to-point over the UHF packet link
2. FOR good success, THE System SHALL queue a Bundle during an outage and deliver it after link restoration
3. FOR strong success, THE System SHALL deliver a Bundle from Source_Node to Destination_Node through Relay_Node with store-and-forward across separate contacts
4. FOR excellent success, THE System SHALL execute repeated automated contact-plan operations over multiple cycles
5. THE demonstration operator SHALL be able to observe and verify each success level

### Requirement 30: JSON Telemetry Payload Format

**User Story:** As a demonstration operator, I want a standard JSON telemetry payload format, so that demonstration messages are structured and easy to parse.

#### Acceptance Criteria

1. WHERE JSON payloads are used, THE payload SHALL include an "origin" field identifying the source node
2. WHERE JSON payloads are used, THE payload SHALL include a "created" field with ISO 8601 timestamp
3. WHERE JSON payloads are used, THE payload SHALL include a "type" field identifying the message type
4. WHERE JSON payloads are used, THE payload SHALL include a "value" field containing the message content
5. THE Demo_Application SHALL successfully parse and display JSON payloads in this format

### Requirement 31: Contact Plan Configuration

**User Story:** As a system integrator, I want to configure the contact plan, so that I can define when links are available for different demonstration scenarios.

#### Acceptance Criteria

1. THE System SHALL provide a mechanism to configure Contact_Window start times
2. THE System SHALL provide a mechanism to configure Contact_Window durations
3. THE System SHALL provide a mechanism to configure which RF_Path each Contact_Window applies to
4. WHEN the Contact_Plan is loaded, THE ION_Node SHALL schedule contact windows according to the configuration
5. THE Contact_Plan configuration SHALL support creating store-and-forward scenarios with non-overlapping windows

### Requirement 32: Relay Node Dual-Radio Support

**User Story:** As a system integrator, I want the relay node to optionally support two radios, so that I can demonstrate simultaneous independent upstream and downstream links.

#### Acceptance Criteria

1. WHERE dual-radio configuration is used, THE Relay_Node SHALL connect to two separate UHF transceivers
2. WHERE dual-radio configuration is used, THE Relay_Node SHALL use separate TNCs for each transceiver
3. WHERE dual-radio configuration is used, THE Relay_Node SHALL independently manage the Source_Node link and Destination_Node link
4. WHERE single-radio configuration is used, THE Relay_Node SHALL time-multiplex access to the single transceiver
5. THE System SHALL successfully demonstrate store-and-forward in both single-radio and dual-radio configurations

### Requirement 33: Convergence Layer Configuration

**User Story:** As a system integrator, I want to configure the ION convergence layer, so that ION can communicate over the IP-over-AX.25 transport.

#### Acceptance Criteria

1. THE ION_Node SHALL support UDP convergence layer over IP-over-AX.25
2. WHERE TCP is preferred, THE ION_Node SHALL support TCP convergence layer over IP-over-AX.25
3. WHEN the Convergence_Layer is configured, THE ION_Node SHALL establish BP connectivity over the configured transport
4. THE Convergence_Layer configuration SHALL specify the IP addresses of neighboring nodes
5. THE Convergence_Layer SHALL successfully transfer bundles when the underlying IP-over-AX.25 path is available

### Requirement 34: Public Demonstration Script

**User Story:** As a demonstration operator, I want a clear demonstration script, so that I can effectively communicate DTN concepts to an audience.

#### Acceptance Criteria

1. THE demonstration script SHALL show all three nodes and their callsigns at the start
2. THE demonstration script SHALL show that the Source_Node-to-Relay_Node link is up and Relay_Node-to-Destination_Node link is down
3. THE demonstration script SHALL send a message from Source_Node to Destination_Node
4. THE demonstration script SHALL show the message arriving at Relay_Node only
5. THE demonstration script SHALL show the message queued on Relay_Node
6. THE demonstration script SHALL enable the Relay_Node-to-Destination_Node link
7. THE demonstration script SHALL show Relay_Node forwarding the message
8. THE demonstration script SHALL show delivery at Destination_Node with both original creation timestamp and received timestamp

### Requirement 35: Error Handling and Recovery

**User Story:** As a demonstration operator, I want error handling and recovery capabilities, so that the system can handle and recover from common failure modes.

#### Acceptance Criteria

1. WHEN a TNC connection is lost, THE ION_Node SHALL detect the failure and log an error
2. WHEN a TNC connection is restored, THE ION_Node SHALL resume normal operation
3. IF a Bundle cannot be forwarded after the configured retry period, THE ION_Node SHALL retain the Bundle for the next Contact_Window
4. WHEN an AX25_Link experiences high error rates, THE System SHALL continue operation with degraded performance rather than failing completely
5. THE System SHALL provide diagnostic information to help operators identify and resolve common issues

### Requirement 36: Accurate Timestamp Generation

**User Story:** As a demonstration operator, I want accurate timestamps, so that I can precisely measure delays and demonstrate DTN timing behavior.

#### Acceptance Criteria

1. THE ION_Node SHALL generate Bundle creation timestamps with accuracy within 1 second
2. THE Demo_Application SHALL generate message creation timestamps with accuracy within 1 second
3. THE Demo_Application SHALL record delivery timestamps with accuracy within 1 second
4. WHERE GPS or network time synchronization is available, THE System SHALL use it for improved timestamp accuracy
5. WHERE GPS or network time synchronization is unavailable, THE System SHALL use the host real-time clock

### Requirement 37: Minimal Custom Software

**User Story:** As a system integrator, I want to minimize custom software development, so that I can focus on demonstrating ION-DTN capabilities rather than building custom transport layers.

#### Acceptance Criteria

1. THE System SHALL use standard ION-DTN software without modifications
2. THE System SHALL use standard Linux AX.25 utilities without modifications
3. THE System SHALL use IP-over-AX.25 rather than custom convergence layer adaptors
4. WHERE custom software is required, it SHALL be limited to demonstration applications and monitoring scripts
5. THE System SHALL use standard KISS protocol without custom extensions

### Requirement 38: Hardware Standardization

**User Story:** As a system integrator, I want standardized hardware across nodes, so that I can reduce configuration complexity and improve reliability.

#### Acceptance Criteria

1. WHERE practical, THE System SHALL use the same TNC model for all nodes
2. WHERE practical, THE System SHALL use the same UHF transceiver model for all nodes
3. THE chosen TNC model SHALL reliably support KISS mode operation
4. THE chosen UHF transceiver model SHALL reliably support amateur packet radio operation
5. THE System SHALL document the specific hardware models used for reproducibility

### Requirement 39: Link Enable and Disable Control

**User Story:** As a demonstration operator, I want to manually or automatically enable and disable links, so that I can control contact windows for demonstration purposes.

#### Acceptance Criteria

1. THE System SHALL provide a mechanism to manually disable an RF_Path
2. THE System SHALL provide a mechanism to manually enable an RF_Path
3. THE System SHALL provide a mechanism to automatically disable an RF_Path at a scheduled time
4. THE System SHALL provide a mechanism to automatically enable an RF_Path at a scheduled time
5. WHEN an RF_Path is disabled, THE System SHALL prevent Bundle transmission over that path until it is re-enabled

### Requirement 40: Queue Depth Visibility

**User Story:** As a demonstration operator, I want visibility into queue depths at each node, so that I can observe store-and-forward behavior during demonstrations.

#### Acceptance Criteria

1. THE ION_Node SHALL provide the current count of bundles in the outbound queue
2. THE ION_Node SHALL provide the current count of bundles in the inbound queue awaiting delivery
3. THE ION_Node SHALL provide the total size in bytes of queued bundles
4. WHEN a Bundle is added to a queue, THE queue depth information SHALL be updated within 1 second
5. WHEN a Bundle is removed from a queue, THE queue depth information SHALL be updated within 1 second

### Requirement 41: Fragmentation Avoidance

**User Story:** As a system integrator, I want to avoid bundle fragmentation, so that demonstrations complete quickly and reliably.

#### Acceptance Criteria

1. THE Demo_Application SHALL limit payload sizes to avoid fragmentation over the AX25_Link
2. THE System SHALL successfully transfer bundles without fragmentation for payloads up to 256 bytes
3. WHERE larger payloads are used, THE System SHALL document the fragmentation behavior
4. THE System SHALL prioritize small, complete bundles over large fragmented bundles for initial demonstrations
5. WHEN fragmentation occurs, THE System SHALL successfully reassemble fragments at the receiving node

### Requirement 42: Routing Configuration

**User Story:** As a system integrator, I want to configure ION routing, so that bundles are correctly routed through the relay node to the destination.

#### Acceptance Criteria

1. THE Source_Node SHALL be configured to route bundles destined for Destination_Node via Relay_Node
2. THE Relay_Node SHALL be configured to forward bundles destined for Destination_Node directly to Destination_Node
3. THE Relay_Node SHALL be configured to accept bundles from Source_Node
4. THE Destination_Node SHALL be configured to accept bundles from Relay_Node
5. WHEN a Bundle is sent from Source_Node to Destination_Node, THE Bundle SHALL follow the configured route through Relay_Node

### Requirement 43: Demonstration Repeatability

**User Story:** As a demonstration operator, I want repeatable demonstrations, so that I can reliably show DTN capabilities to different audiences.

#### Acceptance Criteria

1. WHEN the demonstration is reset and repeated, THE System SHALL produce consistent results
2. THE System SHALL support running multiple demonstration cycles without requiring node restarts
3. THE System SHALL clear previous demonstration state between runs
4. THE demonstration script SHALL produce the same observable behavior across multiple executions
5. THE System SHALL document the steps required to reset and repeat demonstrations

### Requirement 44: Operational Simplicity

**User Story:** As a demonstration operator, I want simple operation, so that I can focus on explaining DTN concepts rather than managing complex system operations.

#### Acceptance Criteria

1. THE System SHALL provide simple commands or scripts to start each node
2. THE System SHALL provide simple commands or scripts to send demonstration messages
3. THE System SHALL provide simple commands or scripts to view received messages
4. THE System SHALL provide simple commands or scripts to view queue status
5. THE System SHALL minimize the number of manual steps required to execute a demonstration

### Requirement 45: Documentation and Reproducibility

**User Story:** As a system integrator, I want comprehensive documentation, so that others can reproduce and build upon this demonstration system.

#### Acceptance Criteria

1. THE System SHALL document the hardware configuration for each node
2. THE System SHALL document the software installation and configuration steps
3. THE System SHALL document the ION configuration files for each node
4. THE System SHALL document the contact plan configuration format and examples
5. THE System SHALL document the demonstration procedures for each of the four demonstration modes


### Requirement 46: AMPRNet IP Address Allocation

**User Story:** As a system integrator, I want to use AMPRNet IP addresses, so that the demonstration complies with amateur radio networking standards and can interoperate with other amateur radio networks.

#### Acceptance Criteria

1. THE System SHALL support IP addresses from the AMPRNet address space (44.0.0.0/8)
2. WHERE production deployment is used, THE System SHALL use IP addresses allocated from https://portal.ampr.org/
3. THE System documentation SHALL provide instructions for obtaining AMPRNet IP addresses
4. WHERE development or testing is performed, THE System MAY use private IP addresses (192.168.x.x) temporarily
5. THE ION_Node configuration SHALL be easily updated to use AMPRNet addresses instead of private addresses
6. THE System SHALL document the mapping between callsigns and allocated AMPRNet addresses

**Note:** AMPRNet (Amateur Packet Radio Network) provides globally routable IP addresses within the amateur radio network. Addresses must be requested from the AMPR portal using a valid amateur radio callsign.

**Example Allocation:**
- Node A (G4DPZ-1): 44.x.y.1
- Node B (G4DPZ-2): 44.x.y.2  
- Node C (G4DPZ-3): 44.x.y.3

Where x.y represents the subnet allocated to the operator's callsign.
