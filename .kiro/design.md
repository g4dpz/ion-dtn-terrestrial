# Design Document: ION-DTN UHF Demonstration System

## Overview

This design document specifies the technical architecture for a terrestrial ION-DTN demonstration system operating over UHF amateur packet radio. The system demonstrates delay-tolerant networking capabilities including point-to-point delayed delivery and multi-hop store-and-forward relay operations using a three-node topology with scheduled contact windows.

### System Purpose

The demonstration system proves that DTN applications can function without continuous end-to-end connectivity by:
- Storing bundles during link outages
- Forwarding bundles when links become available
- Delivering bundles across multiple hops with intermediate storage
- Operating over real RF paths with scheduled contact windows

### Key Design Principles

1. **Minimal Custom Development**: Use standard ION-DTN software without modifications
2. **LTP over Serial/KISS**: Use ION's LTP convergence layer directly over serial KISS interface
3. **Phased Implementation**: Support incremental build-up from basic link validation to full multi-hop demonstrations
4. **Operational Simplicity**: Provide simple commands and scripts for demonstration execution
5. **Hardware Standardization**: Use Mobilinkd TNC3 and Yaesu FT-817 across all nodes

## Architecture

### System Topology

The system consists of three nodes arranged in a linear topology:

```
Source_Node (A) <--RF--> Relay_Node (B) <--RF--> Destination_Node (C)
   dtn://g4dpz-1/        dtn://g4dpz-2/        dtn://g4dpz-3/
```

Key architectural constraints:
- No direct RF path between Source_Node and Destination_Node
- All traffic from A to C must transit through B
- Each RF link operates independently with scheduled contact windows
- Relay_Node provides store-and-forward capability

### Layered Architecture

```
┌─────────────────────────────────────────┐
│     Demo Applications (send/receive)     │
├─────────────────────────────────────────┤
│     ION-DTN Bundle Protocol Layer       │
├─────────────────────────────────────────┤
│  LTP Convergence Layer (ltpcli/ltpclo)  │
├─────────────────────────────────────────┤
│     Serial KISS Interface                │
├─────────────────────────────────────────┤
│     AX.25 Link Layer (via KISS)         │
├─────────────────────────────────────────┤
│     TNC Hardware (KISS mode)            │
├─────────────────────────────────────────┤
│     UHF Transceiver (Packet Radio)      │
└─────────────────────────────────────────┘
```

### Node Architecture

Each node consists of:
- **Linux Host**: Provides ION-DTN runtime, AX.25 stack, and persistent storage
- **TNC Interface**: KISS-mode terminal node controller connected via serial/USB
- **UHF Radio**: Amateur packet radio transceiver operating in simplex/half-duplex mode
- **ION-DTN Instance**: Bundle protocol agent with unique node identity
- **Demo Applications**: Simple send/receive applications for demonstration

### Contact Plan Architecture

The contact plan defines temporal availability of RF links:

```
Time:     0s    30s   60s   90s   120s  150s
Link A-B: [====UP====][--DOWN--][====UP====]
Link B-C: [--DOWN--][====UP====][--DOWN--]
```

This non-overlapping schedule forces store-and-forward behavior at the relay node.

## Components and Interfaces

### ION-DTN Node Component

**Responsibilities:**
- Bundle creation, storage, forwarding, and delivery
- Contact plan execution and routing decisions
- Persistent bundle queue management
- Convergence layer protocol handling

**Configuration:**
- Node identity (ipn scheme): ipn:10.x, ipn:20.x, or ipn:30.x
- Contact plan schedule defining link availability
- Routing table specifying next-hop for each destination
- Convergence layer endpoints (UDP/TCP ports and IP addresses)

**Interfaces:**
- Application interface: bpsendfile/bprecvfile or custom demo apps
- Network interface: UDP/TCP sockets over IP-over-AX.25
- Storage interface: Persistent file-based bundle storage
- Management interface: ION administrative tools (ionadmin, bpadmin, ipnadmin)

### TNC Interface Component

**Responsibilities:**
- KISS protocol framing and de-framing
- AX.25 packet transmission and reception
- Serial/USB communication with host
- RF keying control (PTT)

**Configuration:**
- Serial port device (e.g., /dev/ttyUSB0)
- Baud rate (typically 9600 for TNC, 1200/9600 for RF)
- KISS parameters (TXDelay, Persistence, SlotTime)
- AX.25 callsign and SSID

**Interfaces:**
- Host interface: Serial/USB KISS frames
- Radio interface: Audio tones (AFSK) or direct modulation
- Control interface: PTT signal for transmit/receive switching

### AX.25 Network Interface Component

**Responsibilities:**
- AX.25 frame addressing and routing
- IP packet encapsulation over AX.25
- Link-layer addressing using amateur callsigns
- Frame acknowledgment and retransmission (if using connected mode)

**Configuration:**
- Interface name (e.g., ax0)
- Local callsign and SSID
- IP address for IP-over-AX.25
- MTU size (typically 256 bytes for packet radio)

**Interfaces:**
- Upper layer: IP packets from Linux network stack
- Lower layer: KISS frames to/from TNC
- Configuration: Linux AX.25 utilities (axports, kissattach)

### Demo Application Component

**Responsibilities:**
- Generate demonstration payloads (text, JSON, small files)
- Send bundles via ION API
- Receive and display bundles
- Log timestamps for delay measurement

**Configuration:**
- Source endpoint ID (e.g., ipn:10.1)
- Destination endpoint ID (e.g., ipn:30.1)
- Payload format (text, JSON)
- Message sequence numbering

**Interfaces:**
- ION interface: bp_send() and bp_receive() API calls
- User interface: Command-line arguments or scripts
- Logging interface: File or console output with timestamps

### Contact Plan Manager Component

**Responsibilities:**
- Schedule contact window start and end times
- Enable/disable RF paths according to schedule
- Provide manual override for demonstration control
- Maintain timing accuracy for window transitions

**Configuration:**
- Contact window definitions (start time, duration, link ID)
- Automation mode (manual vs. automatic)
- Timing source (system clock, GPS, NTP)

**Interfaces:**
- ION interface: Contact plan updates via ionadmin
- Control interface: Scripts or commands for manual link control
- Monitoring interface: Status display showing current contact state

### Monitoring and Logging Component

**Responsibilities:**
- Display bundle queue depths
- Show link status (up/down)
- Log bundle events (received, stored, forwarded, delivered)
- Provide diagnostic information for troubleshooting

**Configuration:**
- Log level and output destination
- Refresh rate for status displays
- Event filtering and formatting

**Interfaces:**
- ION interface: Query ION statistics and queue state
- Display interface: Console output or GUI display
- Log interface: File-based event logging

## Data Models

### Bundle Data Model

```
Bundle {
  primary_block: {
    version: 7,
    bundle_flags: uint,
    destination_eid: string,  // e.g., "ipn:30.1"
    source_eid: string,       // e.g., "ipn:10.1"
    report_to_eid: string,
    creation_timestamp: {
      time: uint64,           // DTN epoch seconds
      sequence: uint
    },
    lifetime: uint            // seconds
  },
  payload_block: {
    block_type: 1,
    data: bytes               // application payload
  },
  metadata: {
    size_bytes: uint,
    storage_path: string,
    queue_time: timestamp,
    forwarding_attempts: uint
  }
}
```

### Contact Plan Data Model

```
ContactPlan {
  contacts: [
    {
      start_time: uint64,     // seconds since epoch or relative
      duration: uint,         // seconds
      from_node: uint,        // e.g., 10 for ipn:10.x
      to_node: uint,          // e.g., 20 for ipn:20.x
      data_rate: uint,        // bits per second
      confidence: float       // 0.0 to 1.0
    }
  ],
  ranges: [
    {
      start_time: uint64,
      duration: uint,
      from_node: uint,
      to_node: uint,
      distance: uint          // one-way light time in seconds
    }
  ]
}
```

### Demo Message Payload Data Model

```
DemoMessage {
  origin: string,             // e.g., "Node_A" or "ipn:10.1"
  created: string,            // ISO 8601 timestamp
  type: string,               // e.g., "text", "telemetry", "status"
  sequence: uint,             // message sequence number
  value: string | object      // message content
}
```

Example JSON payload:
```json
{
  "origin": "ipn:10.1",
  "created": "2024-01-15T14:30:00Z",
  "type": "telemetry",
  "sequence": 42,
  "value": "Temperature: 23.5C, Pressure: 1013 hPa"
}
```

### Node Configuration Data Model

```
NodeConfig {
  node_identity: {
    ipn_number: uint,         // e.g., 10, 20, 30
    service_number: uint,     // e.g., 1 for demo app
    full_eid: string          // e.g., "ipn:10.1"
  },
  ax25_identity: {
    callsign: string,         // e.g., "N0CALL"
    ssid: uint,               // 0-15
    full_address: string      // e.g., "N0CALL-1"
  },
  network: {
    ip_address: string,       // e.g., "192.168.25.10"
    netmask: string,
    interface: string         // e.g., "ax0"
  },
  hardware: {
    tnc_device: string,       // e.g., "/dev/ttyUSB0"
    tnc_baud: uint,           // e.g., 9600
    rf_baud: uint,            // e.g., 1200 or 9600
    radio_model: string
  },
  storage: {
    ion_data_path: string,    // e.g., "/var/ion"
    bundle_storage_mb: uint
  }
}
```

### Queue Status Data Model

```
QueueStatus {
  node_id: string,
  timestamp: string,
  outbound_queues: [
    {
      destination: string,    // e.g., "ipn:30.*"
      bundle_count: uint,
      total_bytes: uint,
      oldest_bundle_age: uint // seconds
    }
  ],
  inbound_queue: {
    bundle_count: uint,
    total_bytes: uint
  },
  link_status: [
    {
      link_id: string,        // e.g., "A-to-B"
      state: string,          // "up", "down", "scheduled_up", "scheduled_down"
      next_contact: uint      // seconds until next contact (if down)
    }
  ]
}
```


## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Bundle Creation Includes Required Metadata

*For any* data provided to a Demo_Application, when a Bundle is created, the Bundle SHALL include creation timestamp, source endpoint identifier, and destination endpoint identifier.

**Validates: Requirements 7.4, 7.5**

### Property 2: Bundle Persistent Storage Round-Trip

*For any* Bundle stored by an ION_Node, when the node restarts, the Bundle SHALL be recovered from persistent storage with all metadata intact and necessary for forwarding and delivery.

**Validates: Requirements 8.2, 8.3, 28.1, 28.2, 28.3**

### Property 3: End-to-End Payload Integrity

*For any* data provided by the source Demo_Application, when delivered to the destination Demo_Application, the payload SHALL match the original data exactly.

**Validates: Requirements 10.2**

### Property 4: Bundle Queue Retention During Outage

*For any* Bundle sent when an RF_Path is disabled, the Bundle SHALL remain in the local queue until the RF_Path is restored.

**Validates: Requirements 13.1, 13.2, 8.4**

### Property 5: Bundle Transmission After Path Restoration

*For any* Bundle queued during an RF_Path outage, when the RF_Path is restored, the Bundle SHALL be transmitted.

**Validates: Requirements 13.3, 15.3**

### Property 6: Successful Forwarding Removes Bundle from Queue

*For any* Bundle successfully forwarded or delivered, the ION_Node SHALL remove it from storage.

**Validates: Requirements 9.4, 10.4, 28.4**

### Property 7: Contact Window Enables RF Path

*For any* Contact_Window scheduled as available, the corresponding RF_Path SHALL be enabled at the scheduled time.

**Validates: Requirements 11.2, 21.2**

### Property 8: Contact Window Disables RF Path

*For any* Contact_Window scheduled as unavailable, the corresponding RF_Path SHALL be disabled at the scheduled time.

**Validates: Requirements 11.3, 21.3**

### Property 9: Disabled Path Prevents Transmission

*For any* RF_Path that is disabled, Bundle transmission over that path SHALL be prevented until the path is re-enabled.

**Validates: Requirements 39.5**

### Property 10: KISS Frame Transmission

*For any* KISS frame sent by the host, the TNC SHALL transmit the corresponding AX.25 packet.

**Validates: Requirements 4.3**

### Property 11: KISS Frame Reception

*For any* AX.25 packet received by the TNC, the TNC SHALL deliver it to the host as a KISS frame.

**Validates: Requirements 4.4**

### Property 12: AX.25 Frame Contains Correct Callsign

*For any* transmission from a node, the AX.25 frame SHALL contain that node's assigned callsign and SSID.

**Validates: Requirements 5.5**

### Property 13: IP Packet Encapsulation

*For any* IP packet sent by an ION_Node, the AX25_Link SHALL encapsulate it in an AX.25 frame.

**Validates: Requirements 6.2**

### Property 14: IP Packet De-encapsulation

*For any* AX.25 frame containing an IP packet received by an ION_Node, the ION_Node SHALL extract and process the IP packet.

**Validates: Requirements 6.3**

### Property 15: IP Transfer When Path Available

*For any* IP packet, when the RF_Path is available, the System SHALL successfully transfer the packet between connected nodes.

**Validates: Requirements 6.5**

### Property 16: Bundle Creation from Application Data

*For any* data provided by a Demo_Application, the node SHALL create a Bundle containing that data.

**Validates: Requirements 7.1, 22.3**

### Property 17: Bundle Transmission During Contact Window

*For any* Bundle, when a Contact_Window to the next hop is available, the node SHALL transmit the Bundle.

**Validates: Requirements 7.3**

### Property 18: Bundle Reception and Storage

*For any* Bundle arriving at an ION_Node, the node SHALL receive and store the Bundle persistently to local storage.

**Validates: Requirements 8.1, 8.2**

### Property 19: Bundle Queue Maintenance

*For any* ION_Node, the node SHALL maintain a queue of stored bundles awaiting transmission.

**Validates: Requirements 8.5**

### Property 20: Bundle Delivery to Application

*For any* Bundle addressed to a node, when received, the node SHALL deliver the Bundle payload to the local Demo_Application with delivery timestamp information.

**Validates: Requirements 10.1, 10.3**

### Property 21: Multi-Hop Bundle Delivery

*For any* Bundle sent from Source_Node to Destination_Node via Relay_Node, the Bundle SHALL eventually be successfully delivered to Destination_Node.

**Validates: Requirements 14.4**

### Property 22: Bundle Delivery After Interruption

*For any* Bundle transmission interrupted mid-transfer, when the RF_Path is restored, the Bundle SHALL be successfully delivered on a subsequent contact.

**Validates: Requirements 15.4**

### Property 23: Payload Size Support

*For any* payload up to 1 kilobyte in size, the System SHALL successfully transfer it.

**Validates: Requirements 16.5**

### Property 24: Demo Application Logging

*For any* Bundle received by the receive Demo_Application, the application SHALL log the payload, creation timestamp, and delivery timestamp.

**Validates: Requirements 22.4**

### Property 25: Message Payload Structure

*For any* message payload created by a Demo_Application, the payload SHALL include a creation timestamp, origin node identifier, and message sequence number.

**Validates: Requirements 23.1, 23.3, 23.4**

### Property 26: ISO 8601 Timestamp Format

*For any* creation timestamp in a message payload, the timestamp SHALL use valid ISO 8601 format.

**Validates: Requirements 23.2**

### Property 27: JSON Payload Structure

*For any* JSON payload, the payload SHALL include "origin", "created", "type", and "value" fields.

**Validates: Requirements 30.1, 30.2, 30.3, 30.4**

### Property 28: JSON Payload Round-Trip

*For any* valid JSON payload in the specified format, the Demo_Application SHALL successfully parse and display it.

**Validates: Requirements 30.5**

### Property 29: TNC Connection Failure Detection

*For any* TNC connection loss, the ION_Node SHALL detect the failure and log an error.

**Validates: Requirements 35.1**

### Property 30: TNC Connection Recovery

*For any* TNC connection restoration, the ION_Node SHALL resume normal operation.

**Validates: Requirements 35.2**

### Property 31: Bundle Retention After Forward Failure

*For any* Bundle that cannot be forwarded after the configured retry period, the ION_Node SHALL retain the Bundle for the next Contact_Window.

**Validates: Requirements 35.3**

### Property 32: Queue Depth Monitoring

*For any* ION_Node, the node SHALL provide the current count of bundles in both outbound and inbound queues.

**Validates: Requirements 40.1, 40.2**

### Property 33: Fragmentation Avoidance

*For any* payload up to 256 bytes, the System SHALL transfer the bundle without fragmentation.

**Validates: Requirements 41.2**

### Property 34: Fragment Reassembly

*For any* fragmented bundle, the System SHALL successfully reassemble the fragments at the receiving node.

**Validates: Requirements 41.5**

### Property 35: Demonstration Repeatability

*For any* demonstration sequence, when reset and repeated, the System SHALL produce consistent results.

**Validates: Requirements 43.1**

### Property 36: Node Initialization with Unique Identity

*For any* ION_Node, when started, the node SHALL initialize with a unique BP node identity.

**Validates: Requirements 2.4**

## Error Handling

### TNC and Hardware Errors

**TNC Connection Failures:**
- Detection: Monitor serial/USB interface for connection loss
- Response: Log error, mark link as down, queue bundles for later transmission
- Recovery: Automatically resume operation when connection is restored
- User notification: Display TNC status in monitoring interface

**Radio Transmission Errors:**
- Detection: Monitor for excessive AX.25 retransmissions or timeouts
- Response: Continue operation with degraded performance, log warnings
- Recovery: Automatic retry with exponential backoff
- User notification: Display link quality metrics

**KISS Protocol Errors:**
- Detection: Invalid KISS frame format or checksum errors
- Response: Discard invalid frames, log errors, continue operation
- Recovery: Automatic - next valid frame will be processed
- User notification: Log KISS frame errors for troubleshooting

### Bundle Protocol Errors

**Bundle Storage Failures:**
- Detection: Disk write errors or insufficient storage space
- Response: Reject new bundles, log critical error, attempt to free space
- Recovery: Manual intervention required to resolve storage issues
- User notification: Critical alert to operator

**Bundle Forwarding Failures:**
- Detection: Timeout waiting for acknowledgment or contact window expiration
- Response: Retain bundle in queue, schedule retry for next contact window
- Recovery: Automatic retry on next contact
- User notification: Log forwarding attempts and failures

**Invalid Bundle Format:**
- Detection: Bundle parsing errors or missing required fields
- Response: Reject bundle, log error with bundle details
- Recovery: Automatic - discard invalid bundle
- User notification: Log bundle validation errors

**Routing Failures:**
- Detection: No route to destination or next hop unavailable
- Response: Queue bundle, wait for contact plan to provide route
- Recovery: Automatic when contact becomes available
- User notification: Display routing status in monitoring interface

### Network and Link Errors

**IP-over-AX.25 Failures:**
- Detection: ARP failures, IP routing errors, or interface down
- Response: Mark link as unavailable, queue bundles
- Recovery: Automatic when interface comes up
- User notification: Display interface status

**Contact Window Timing Errors:**
- Detection: Contact window transitions not occurring at scheduled times
- Response: Log timing errors, continue with manual control if needed
- Recovery: Verify system clock synchronization, reload contact plan
- User notification: Alert operator to timing discrepancies

**Link Quality Degradation:**
- Detection: High packet loss rate or excessive retransmissions
- Response: Continue operation, reduce transmission rate if possible
- Recovery: Automatic improvement when RF conditions improve
- User notification: Display link quality metrics

### Application Errors

**Invalid Payload Format:**
- Detection: JSON parsing errors or missing required fields
- Response: Reject payload, log error, notify user
- Recovery: User provides corrected payload
- User notification: Display validation error message

**Payload Size Exceeded:**
- Detection: Payload larger than configured maximum
- Response: Reject payload, log error, notify user
- Recovery: User reduces payload size or system is reconfigured
- User notification: Display size limit error

**Endpoint ID Errors:**
- Detection: Invalid or unreachable destination endpoint
- Response: Reject bundle creation, log error
- Recovery: User provides valid endpoint ID
- User notification: Display endpoint validation error

### Demonstration-Specific Error Handling

**Contact Plan Execution Errors:**
- Detection: Failed to enable/disable RF path at scheduled time
- Response: Log error, attempt manual control, continue demonstration
- Recovery: Manual intervention to correct contact plan or timing
- User notification: Alert operator immediately

**Queue Overflow:**
- Detection: Bundle queue exceeds configured maximum
- Response: Reject new bundles or apply custody transfer policies
- Recovery: Wait for bundles to be forwarded and queue to drain
- User notification: Display queue status and warnings

**Demonstration State Inconsistency:**
- Detection: Unexpected bundle delivery order or missing bundles
- Response: Log state information, continue operation
- Recovery: Reset demonstration state and restart
- User notification: Provide diagnostic information to operator

### Error Recovery Strategies

**Graceful Degradation:**
- System continues operating with reduced functionality when non-critical components fail
- Example: Continue bundle queuing even if monitoring display fails

**Automatic Retry:**
- Failed operations are automatically retried with exponential backoff
- Example: Bundle forwarding retried on subsequent contact windows

**State Preservation:**
- Critical state (bundles, configuration) is preserved across failures
- Example: Bundles survive node restarts via persistent storage

**Manual Intervention:**
- Some errors require operator intervention with clear guidance
- Example: Resolving storage space issues or hardware failures

**Diagnostic Logging:**
- All errors are logged with sufficient detail for troubleshooting
- Logs include timestamps, component identifiers, and error context

## Testing Strategy

### Dual Testing Approach

The testing strategy employs both unit testing and property-based testing as complementary approaches:

**Unit Tests:**
- Verify specific examples and edge cases
- Test integration points between components
- Validate error conditions and recovery paths
- Focus on concrete scenarios from demonstration modes

**Property-Based Tests:**
- Verify universal properties across all inputs
- Provide comprehensive input coverage through randomization
- Validate correctness properties from the design document
- Each property test runs minimum 100 iterations

Together, unit tests catch concrete bugs while property tests verify general correctness across the input space.

### Property-Based Testing Framework

**Framework Selection:**
- Python: Use Hypothesis library for property-based testing
- C: Use theft or QuickCheck-style framework
- Shell scripts: Use property-based testing patterns with random input generation

**Test Configuration:**
- Minimum 100 iterations per property test
- Each test tagged with reference to design document property
- Tag format: `# Feature: ion-dtn-uhf-demo, Property N: [property text]`

**Property Test Implementation:**
- Each correctness property maps to exactly one property-based test
- Tests generate random valid inputs (bundles, payloads, configurations)
- Tests verify the property holds for all generated inputs
- Failures report the specific input that violated the property

### Unit Testing Strategy

**Component-Level Tests:**

1. **TNC Interface Tests:**
   - KISS frame encoding/decoding
   - Serial communication with mock TNC
   - Error handling for connection loss
   - Example: Send specific KISS frame, verify AX.25 packet format

2. **AX.25 Network Tests:**
   - IP packet encapsulation/de-encapsulation
   - Callsign addressing validation
   - Interface configuration
   - Example: Verify specific callsign appears in transmitted frame

3. **ION-DTN Integration Tests:**
   - Bundle creation with specific payloads
   - Bundle storage and retrieval
   - Queue management operations
   - Example: Create bundle with known payload, verify storage

4. **Demo Application Tests:**
   - JSON payload parsing and generation
   - Timestamp format validation
   - Message sequence numbering
   - Example: Parse specific JSON payload, verify fields present

5. **Contact Plan Tests:**
   - Contact window scheduling
   - RF path enable/disable control
   - Timing accuracy validation
   - Example: Schedule specific contact, verify path enabled at correct time

**Integration Tests:**

1. **Two-Node Link Tests:**
   - Point-to-point bundle transfer
   - IP connectivity over AX.25
   - KISS and TNC operation
   - Example: Send bundle from A to B, verify delivery

2. **Three-Node Relay Tests:**
   - Store-and-forward through relay
   - Multi-hop routing
   - Queue persistence across hops
   - Example: Send bundle A→B→C, verify relay storage and forwarding

3. **Contact Window Tests:**
   - Delayed delivery with scheduled contacts
   - Bundle queuing during outages
   - Automatic forwarding when contact available
   - Example: Send during outage, verify queuing, restore contact, verify delivery

4. **Interruption Recovery Tests:**
   - Mid-transfer link failure
   - Bundle retention and retry
   - Successful delivery after recovery
   - Example: Interrupt transfer, verify bundle retained, restore link, verify delivery

**End-to-End Tests:**

1. **Demonstration Mode Tests:**
   - Point-to-point real-time (Mode 1)
   - Delayed point-to-point (Mode 2)
   - Relay store-and-forward (Mode 3)
   - Interrupted forwarding (Mode 4)
   - Each mode tested with specific scenarios

2. **Payload Type Tests:**
   - Text messages
   - JSON telemetry
   - Small binary files
   - Various payload sizes up to 1KB

3. **Error Scenario Tests:**
   - TNC connection loss and recovery
   - Storage failures
   - Invalid payloads
   - Routing failures

### Test Environment Setup

**Bench Configuration:**
- Three Linux hosts (physical or virtual)
- Three TNCs in KISS mode (or simulated)
- Dummy loads or very low power RF
- Controlled environment for repeatable tests

**Simulated Environment:**
- Software simulation of TNC and RF links
- Configurable delays and error rates
- Faster test execution
- Automated test suite execution

**Test Data Generation:**
- Random bundle payloads (text, JSON, binary)
- Random contact schedules
- Random failure injection (link drops, delays)
- Deterministic seed for reproducibility

### Test Execution and Validation

**Automated Test Suite:**
- Run all unit tests before integration tests
- Run property-based tests with 100+ iterations
- Collect coverage metrics
- Generate test reports

**Validation Criteria:**
- All unit tests pass
- All property tests pass (no counterexamples found)
- Code coverage >80% for critical components
- All four demonstration modes execute successfully

**Continuous Testing:**
- Run tests on each code change
- Automated regression testing
- Performance benchmarking
- Long-duration stability tests

### Test Documentation

Each test shall document:
- Test purpose and requirements validated
- Test setup and preconditions
- Test execution steps
- Expected results
- Actual results and pass/fail status
- For property tests: reference to design document property number

### Phased Testing Approach

Aligned with phased implementation:

**Phase 1: Link Bring-Up**
- Unit tests for KISS, AX.25, IP-over-AX.25
- Two-node connectivity tests
- TNC and radio hardware validation

**Phase 2: Two-Node ION**
- Bundle creation and delivery tests
- ION configuration validation
- Point-to-point transfer tests

**Phase 3: Delayed Delivery**
- Queue management tests
- Contact window control tests
- Delayed delivery scenario tests

**Phase 4: Three-Node Relay**
- Relay store-and-forward tests
- Multi-hop routing tests
- End-to-end delivery tests

**Phase 5: Scheduled Contacts**
- Automated contact plan tests
- Repeated cycle tests
- Demonstration repeatability tests


## Implementation Details

### IP Addressing: AMPRNet vs Private Addresses

**IMPORTANT:** For production amateur radio use, nodes should use AMPRNet (Amateur Packet Radio Network) IP addresses from the 44.0.0.0/8 address space.

**Obtaining AMPRNet Addresses:**
1. Register at https://portal.ampr.org/
2. Request IP allocation for your callsign
3. Receive allocated 44.x.y.z subnet
4. Update node configurations with allocated addresses

**Address Assignment Example:**
- Node A (G4DPZ-1): 44.x.y.1
- Node B (G4DPZ-2): 44.x.y.2
- Node C (G4DPZ-3): 44.x.y.3

**Development vs Production:**
- **Development/Testing:** Use private addresses (192.168.25.x) as shown in examples below
- **Production/Field:** Use AMPRNet addresses (44.x.y.z) allocated from AMPR portal
- Configuration files use 192.168.25.x as placeholders - replace with your AMPRNet allocation

**Benefits of AMPRNet Addresses:**
- Globally routable within amateur radio network
- Interoperability with other amateur radio IP networks
- Compliance with amateur radio networking standards
- Integration with AMPRNet gateways and routing

### Node Configuration Files

Each node requires configuration files for:

**ION Configuration (ionconfig):**
```
wmKey 0
wmSize 5000000
configFlags 1
pathName /var/ion
```

**ION Administration (ionrc):**
```
## Node A (Source) - ipn:10.1
1 10 ''
a contact +0 +3600 10 20 100000
a range +0 +3600 10 20 1
m production 1000000
m consumption 1000000
```

**Bundle Protocol Administration (bprc):**
```
1
a scheme ipn 'ipnfw' 'ipnadminep'
a endpoint ipn:10.0 q
a endpoint ipn:10.1 q
a protocol udp 1113 ''
a induct udp 192.168.25.10:4556 udpcli
a outduct udp 192.168.25.20:4556 udpclo
s
```

**IPN Administration (ipnrc):**
```
a plan 20 udp/192.168.25.20:4556
a plan 30 udp/192.168.25.20:4556
```

### AX.25 Configuration

**AX.25 Ports (/etc/ax25/axports):**
```
radio   N0CALL-1  9600  255  2  Source Node
```

**KISS Attach Command:**
```bash
kissattach /dev/ttyUSB0 radio 192.168.25.10
```

**Interface Configuration:**
```bash
ifconfig ax0 192.168.25.10 netmask 255.255.255.0
route add -net 192.168.25.0 netmask 255.255.255.0 dev ax0
```

### TNC Configuration

**KISS Mode Setup:**
```
# Enter KISS mode (TNC-specific command)
# For many TNCs:
KISS ON
RESTART
```

**Serial Port Configuration:**
```bash
stty -F /dev/ttyUSB0 9600 raw -echo
```

### Contact Plan Management

**Contact Plan Script Structure:**
```python
#!/usr/bin/env python3
import time
import subprocess
from datetime import datetime, timedelta

class ContactPlan:
    def __init__(self, schedule):
        self.schedule = schedule  # List of (start_time, duration, link_id)
    
    def execute(self):
        for start_time, duration, link_id in self.schedule:
            # Wait until start time
            wait_seconds = (start_time - datetime.now()).total_seconds()
            if wait_seconds > 0:
                time.sleep(wait_seconds)
            
            # Enable link
            self.enable_link(link_id)
            
            # Wait for duration
            time.sleep(duration)
            
            # Disable link
            self.disable_link(link_id)
    
    def enable_link(self, link_id):
        # Implementation: update ION contact plan or enable RF path
        pass
    
    def disable_link(self, link_id):
        # Implementation: update ION contact plan or disable RF path
        pass
```


### Demo Application Implementation

**Send Application (bpsend.py):**
```python
#!/usr/bin/env python3
import sys
import json
from datetime import datetime
import subprocess

def send_bundle(dest_eid, message):
    # Create JSON payload
    payload = {
        "origin": "ipn:10.1",
        "created": datetime.utcnow().isoformat() + "Z",
        "type": "text",
        "sequence": get_next_sequence(),
        "value": message
    }
    
    # Write payload to temp file
    with open("/tmp/bundle_payload.json", "w") as f:
        json.dump(payload, f)
    
    # Send via ION
    result = subprocess.run([
        "bpsendfile",
        "ipn:10.1",
        dest_eid,
        "/tmp/bundle_payload.json"
    ], capture_output=True)
    
    if result.returncode == 0:
        print(f"Bundle sent to {dest_eid}")
        print(f"Created: {payload['created']}")
    else:
        print(f"Error sending bundle: {result.stderr.decode()}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: bpsend.py <dest_eid> <message>")
        sys.exit(1)
    
    send_bundle(sys.argv[1], sys.argv[2])
```

**Receive Application (bprecv.py):**
```python
#!/usr/bin/env python3
import json
import subprocess
from datetime import datetime

def receive_bundles(local_eid):
    print(f"Listening for bundles on {local_eid}...")
    
    while True:
        # Receive bundle via ION
        result = subprocess.run([
            "bprecvfile",
            local_eid,
            "/tmp/received_bundle.json"
        ], capture_output=True)
        
        if result.returncode == 0:
            # Parse payload
            with open("/tmp/received_bundle.json", "r") as f:
                payload = json.load(f)
            
            # Display with timestamps
            delivery_time = datetime.utcnow().isoformat() + "Z"
            print("\n=== Bundle Received ===")
            print(f"Origin: {payload['origin']}")
            print(f"Created: {payload['created']}")
            print(f"Delivered: {delivery_time}")
            print(f"Type: {payload['type']}")
            print(f"Sequence: {payload['sequence']}")
            print(f"Message: {payload['value']}")
            print("=" * 25)

if __name__ == "__main__":
    receive_bundles("ipn:30.1")
```


### Monitoring and Status Display

**Queue Status Monitor (queue_status.sh):**
```bash
#!/bin/bash
# Display ION queue status

echo "=== ION Queue Status ==="
echo "Node: $(hostname)"
echo "Time: $(date -Iseconds)"
echo

# Get bundle statistics
bpstats

echo
echo "=== Outbound Queues ==="
# Query ION for outbound queue depths
ionadmin << EOF
l contact
EOF

echo
echo "=== Link Status ==="
# Check AX.25 interface status
ifconfig ax0 | grep -E "inet|RX|TX"

echo
echo "=== TNC Status ==="
# Check serial port connection
if [ -e /dev/ttyUSB0 ]; then
    echo "TNC connected on /dev/ttyUSB0"
else
    echo "WARNING: TNC not detected"
fi
```

**Continuous Monitor (monitor.py):**
```python
#!/usr/bin/env python3
import time
import subprocess
import curses

def get_ion_stats():
    """Query ION for current statistics"""
    result = subprocess.run(["bpstats"], capture_output=True, text=True)
    # Parse output for bundle counts
    return parse_bpstats(result.stdout)

def display_status(stdscr):
    """Display real-time status using curses"""
    curses.curs_set(0)
    stdscr.nodelay(1)
    
    while True:
        stdscr.clear()
        
        # Header
        stdscr.addstr(0, 0, "=== ION-DTN Status Monitor ===", curses.A_BOLD)
        stdscr.addstr(1, 0, f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
        
        # Get statistics
        stats = get_ion_stats()
        
        # Display queue depths
        stdscr.addstr(3, 0, "Outbound Queue:", curses.A_BOLD)
        stdscr.addstr(4, 2, f"Bundles: {stats['outbound_count']}")
        stdscr.addstr(5, 2, f"Bytes: {stats['outbound_bytes']}")
        
        stdscr.addstr(7, 0, "Inbound Queue:", curses.A_BOLD)
        stdscr.addstr(8, 2, f"Bundles: {stats['inbound_count']}")
        
        # Display link status
        stdscr.addstr(10, 0, "Link Status:", curses.A_BOLD)
        stdscr.addstr(11, 2, f"AX0: {get_interface_status('ax0')}")
        
        stdscr.addstr(13, 0, "Press 'q' to quit")
        stdscr.refresh()
        
        # Check for quit
        key = stdscr.getch()
        if key == ord('q'):
            break
        
        time.sleep(1)

if __name__ == "__main__":
    curses.wrapper(display_status)
```


## Phased Implementation Plan

### Phase 1: Bench Link Bring-Up

**Objective:** Validate basic AX.25 and IP connectivity between two nodes

**Components:**
- Two Linux hosts with TNCs
- KISS mode configuration
- AX.25 interface setup
- IP-over-AX.25 configuration

**Validation:**
- KISS frames transmitted and received
- AX.25 frames with correct callsigns
- IP ping successful between nodes
- Stable link operation

**Success Criteria:**
- Ping latency <1 second
- Packet loss <5%
- Sustained operation for 10 minutes

### Phase 2: Two-Node ION Test

**Objective:** Establish BP connectivity and transfer small bundles

**Components:**
- ION-DTN installed on both nodes
- Basic ION configuration (node IDs, convergence layer)
- Simple send/receive applications
- Contact plan with continuous contact

**Validation:**
- ION nodes start successfully
- Bundle created and sent
- Bundle received and delivered
- Payload integrity verified

**Success Criteria:**
- Bundle transfer completes in <10 seconds
- Payload matches original data
- No ION errors in logs

### Phase 3: Delayed Delivery Test

**Objective:** Demonstrate queuing and later delivery

**Components:**
- Contact plan with scheduled outages
- Queue monitoring tools
- Manual link control

**Validation:**
- Bundle queued when link down
- Bundle persists in queue during outage
- Bundle transmitted when link restored
- Delivery timestamp shows delay

**Success Criteria:**
- Bundle survives 5-minute outage
- Automatic transmission on link restoration
- Queue depth correctly reported

### Phase 4: Three-Node Relay Test

**Objective:** Demonstrate store-and-forward via relay node

**Components:**
- Third node added as relay
- Routing configuration for multi-hop
- Non-overlapping contact windows
- Relay monitoring tools

**Validation:**
- Bundle received at relay from source
- Bundle stored at relay
- Bundle forwarded from relay to destination
- End-to-end delivery successful

**Success Criteria:**
- Bundle delivered through relay
- Relay queue shows store-and-forward
- No direct source-to-destination connectivity

### Phase 5: Scheduled Contact Demonstration

**Objective:** Automated repeated cycles with scheduled contacts

**Components:**
- Automated contact plan execution
- Demonstration scripts
- Status monitoring displays
- Multiple cycle execution

**Validation:**
- Contact windows execute on schedule
- Multiple bundles transferred across cycles
- Timing accuracy within 1 second
- Repeatable demonstration

**Success Criteria:**
- 3+ complete cycles without manual intervention
- Consistent behavior across cycles
- All four demonstration modes functional


## Demonstration Modes

### Mode 1: Point-to-Point Real-Time

**Configuration:**
- Two nodes (A and B, or B and C)
- Continuous contact window
- No scheduled outages

**Procedure:**
1. Verify link is up
2. Send bundle from source
3. Observe immediate delivery
4. Display creation and delivery timestamps

**Observable Behavior:**
- Bundle delivered within seconds
- Minimal delay between creation and delivery
- Direct transfer without queuing

**Success Metric:**
- Delivery time < 10 seconds

### Mode 2: Delayed Point-to-Point

**Configuration:**
- Two nodes (A and B)
- Initial link down
- Scheduled link restoration

**Procedure:**
1. Disable link
2. Send bundle from source
3. Observe bundle queued locally
4. Enable link after delay (e.g., 2 minutes)
5. Observe automatic transmission
6. Display creation time vs. delivery time

**Observable Behavior:**
- Bundle queued when link down
- Queue depth increases
- Automatic transmission when link restored
- Significant delay between creation and delivery

**Success Metric:**
- Bundle delivered after link restoration
- Delay accurately measured

### Mode 3: Relay Store-and-Forward

**Configuration:**
- Three nodes (A, B, C)
- A-to-B link up, B-to-C link down initially
- Non-overlapping contact windows

**Procedure:**
1. Verify A-to-B link up, B-to-C link down
2. Send bundle from A to C (via B)
3. Observe bundle arrive at B
4. Observe bundle queued at B
5. Enable B-to-C link
6. Observe B forward bundle to C
7. Observe delivery at C

**Observable Behavior:**
- Bundle received at relay (B)
- Bundle stored in relay queue
- Relay queue depth shows waiting bundle
- Bundle forwarded when downstream contact available
- End-to-end delivery without direct A-to-C connectivity

**Success Metric:**
- Bundle delivered through relay
- Store-and-forward behavior clearly visible

### Mode 4: Interrupted Forwarding

**Configuration:**
- Two or three nodes
- Active link during transmission
- Planned interruption mid-transfer

**Procedure:**
1. Begin bundle transmission
2. Interrupt link during transfer
3. Observe bundle retained in queue
4. Restore link
5. Observe retry and successful delivery

**Observable Behavior:**
- Transmission begins
- Link interruption detected
- Bundle remains in queue
- Automatic retry on link restoration
- Successful delivery on subsequent attempt

**Success Metric:**
- Bundle delivered after interruption and recovery
- System demonstrates resilience to mid-transfer failures


## Hardware Specifications

### Recommended Hardware

**Linux Host:**
- Raspberry Pi 4 (4GB RAM) or equivalent
- Ubuntu 20.04 LTS or Debian 11
- 32GB SD card or larger
- USB ports for TNC connection

**TNC (Terminal Node Controller):**
- **Mobilinkd TNC3** - Bluetooth/USB KISS TNC
- Firmware: Latest version with KISS mode support
- Connection: USB cable to Raspberry Pi
- Audio connection: 3.5mm cable to FT-817 data port
- KISS mode support (required)
- 1200 baud AFSK

**Transceiver:**
- **Yaesu FT-817ND** - HF/VHF/UHF all-mode portable transceiver
- Frequency: 430-440 MHz (UHF) or 144-146 MHz (VHF)
- Mode: FM (frequency modulation)
- Data port: 6-pin mini-DIN connector
- Power: 5W maximum (QRP transceiver)

**Cables and Accessories:**
- USB cable (Mobilinkd TNC3 to Raspberry Pi)
- Audio cable (3.5mm to 6-pin mini-DIN for FT-817 data port)
- Dummy loads (50Ω, 10W) for bench testing
- RF attenuators (20dB) for bench configuration
- Antennas (whip for bench, external for field)

### Hardware Configuration

**Node A (Source):**
- Linux host: Raspberry Pi 4
- TNC: Mobilinkd TNC3 on /dev/ttyUSB0 (or /dev/ttyACM0)
- Radio: Yaesu FT-817ND
- Callsign: G4DPZ-1
- IP: 192.168.25.10 (development) or AMPRNet address (production)
- Frequency: 433.500 MHz (or coordinated frequency)

**Node B (Relay):**
- Linux host: Raspberry Pi 4
- TNC: Mobilinkd TNC3 on /dev/ttyUSB0 (or /dev/ttyACM0)
- Radio: Yaesu FT-817ND
- Callsign: G4DPZ-2
- IP: 192.168.25.20 (development) or AMPRNet address (production)
- Frequency: 433.500 MHz (same as Node A)

**Node C (Destination):**
- Linux host: Raspberry Pi 4
- TNC: Mobilinkd TNC3 on /dev/ttyUSB0 (or /dev/ttyACM0)
- Radio: Yaesu FT-817ND
- Callsign: G4DPZ-3
- IP: 192.168.25.30 (development) or AMPRNet address (production)
- Frequency: 433.500 MHz (same as Nodes A & B)

### Bench Configuration Setup

**RF Isolation:**
- All FT-817 radios transmit into 50Ω dummy loads (10W rating)
- OR use very low power (0.5W-1W) with 20dB attenuators
- Maintain physical separation (1-2 meters) to prevent overload

**Power Supply:**
- FT-817: 13.8V DC (or internal battery pack for portable operation)
- Raspberry Pis: 5V USB power (3A each)
- Mobilinkd TNC3: Powered via USB from Raspberry Pi

**FT-817 Configuration:**
- Mode: FM (not USB/LSB)
- Power: Low (0.5W-1W for bench testing)
- Squelch: Adjust to minimize noise but allow signal reception
- Data port: Connected to Mobilinkd TNC3 via audio cable

**Interconnections:**
```
[Node A]                           [Node B]                           [Node C]
  RPi ---- USB ---- Mobilinkd       RPi ---- USB ---- Mobilinkd       RPi ---- USB ---- Mobilinkd
                      TNC3                            TNC3                            TNC3
                       |                               |                               |
                   Audio Cable                     Audio Cable                     Audio Cable
                       |                               |                               |
                    FT-817                          FT-817                          FT-817
                       |                               |                               |
                   Dummy Load                      Dummy Load                      Dummy Load
```

### Field Configuration Setup

**Geographic Separation:**
- Node A and B: 1-10 km separation
- Node B and C: 1-10 km separation
- Line-of-sight or near-line-of-sight preferred

**Antenna Systems:**
- Vertical omnidirectional antennas
- Mounted at adequate height (>5m)
- 50Ω coaxial feedline
- SWR <2:1 on operating frequency

**Frequency Coordination:**
- Use coordinated packet frequencies (e.g., 445.925 MHz)
- Comply with local amateur radio regulations
- Avoid interference with other users


## Software Dependencies

### Required Software Packages

**Linux Packages:**
```bash
# AX.25 utilities and kernel support
apt-get install ax25-tools ax25-apps libax25

# ION-DTN (build from source)
apt-get install build-essential automake libtool

# Python for demo applications
apt-get install python3 python3-pip

# Serial communication
apt-get install minicom screen setserial

# Monitoring tools
apt-get install net-tools tcpdump
```

**ION-DTN Installation:**
```bash
# Download ION source
wget https://sourceforge.net/projects/ion-dtn/files/ion-4.1.2.tar.gz
tar xzf ion-4.1.2.tar.gz
cd ion-4.1.2

# Configure and build
./configure --prefix=/usr/local
make
sudo make install

# Verify installation
ionadmin
```

**Python Dependencies:**
```bash
pip3 install hypothesis  # For property-based testing
```

### Software Versions

- Linux kernel: 5.4+ (for AX.25 support)
- ION-DTN: 4.1.2 or later
- Python: 3.8 or later
- AX.25 tools: 0.0.12 or later

### Directory Structure

```
/opt/ion-demo/
├── config/
│   ├── node_a/
│   │   ├── ionconfig
│   │   ├── ionrc
│   │   ├── bprc
│   │   └── ipnrc
│   ├── node_b/
│   │   └── (similar files)
│   └── node_c/
│       └── (similar files)
├── scripts/
│   ├── start_node.sh
│   ├── stop_node.sh
│   ├── bpsend.py
│   ├── bprecv.py
│   ├── queue_status.sh
│   └── monitor.py
├── contact_plans/
│   ├── continuous.plan
│   ├── delayed.plan
│   ├── relay.plan
│   └── interrupted.plan
├── logs/
│   ├── ion.log
│   ├── ax25.log
│   └── demo.log
└── data/
    └── bundles/
```

## Security Considerations

### Amateur Radio Compliance

**Regulatory Requirements:**
- All transmissions must comply with amateur radio regulations
- Station identification required per regulations
- Encryption prohibited on amateur frequencies
- Content restrictions apply

**Callsign Usage:**
- Valid amateur radio license required for each operator
- Callsigns must be properly assigned in AX.25 configuration
- Station identification in transmitted data

### System Security

**Access Control:**
- Limit shell access to authorized operators
- Protect ION configuration files (read-only where possible)
- Secure serial/USB device permissions

**Data Validation:**
- Validate bundle payloads before processing
- Sanitize JSON input to prevent injection
- Limit payload sizes to prevent resource exhaustion

**Network Security:**
- IP-over-AX.25 network is inherently broadcast
- No encryption available (amateur radio restriction)
- Assume all data is publicly observable
- Do not transmit sensitive information

### Operational Security

**Demonstration Content:**
- Use only non-sensitive demonstration data
- Avoid personal information in payloads
- Use generic identifiers and test messages

**System Monitoring:**
- Log all bundle transfers for audit
- Monitor for unexpected behavior
- Alert on error conditions

## Performance Characteristics

### Expected Performance Metrics

**Link Layer:**
- Data rate: 1200 baud (150 bytes/sec) or 9600 baud (1200 bytes/sec)
- Packet size: 256 bytes typical (AX.25 MTU)
- Latency: 1-5 seconds per hop (including protocol overhead)

**Bundle Protocol:**
- Bundle creation: <100ms
- Bundle storage: <500ms
- Bundle forwarding: <1 second (when contact available)
- Queue depth: 100+ bundles supported

**End-to-End:**
- Point-to-point delivery: 5-10 seconds
- Two-hop delivery: 10-30 seconds (depending on contact schedule)
- Storage capacity: 1000+ bundles (limited by disk space)

### Scalability Considerations

**Current Design:**
- Three nodes (fixed topology)
- Linear relay chain
- Single relay node

**Potential Extensions:**
- Additional relay nodes
- Mesh topology with multiple paths
- Dynamic routing based on contact opportunities
- Larger payload sizes with fragmentation

### Resource Requirements

**CPU:**
- Minimal CPU usage (<10% on Raspberry Pi 4)
- ION-DTN is lightweight
- Demo applications are simple

**Memory:**
- ION-DTN: ~50MB RAM
- Bundle storage: Disk-based, minimal RAM impact
- Total system: <500MB RAM

**Storage:**
- ION database: ~100MB
- Bundle storage: Depends on queue depth and payload sizes
- Logs: ~10MB per day
- Total: 1GB adequate for demonstration

**Network:**
- Bandwidth: Limited by RF data rate (1200-9600 baud)
- Latency: Dominated by RF transmission time
- No continuous connectivity required


## Operational Procedures

### System Startup Procedure

**1. Hardware Preparation:**
```bash
# Connect TNCs to radios
# Connect TNCs to Linux hosts via USB
# Power on radios
# Verify radio settings (frequency, power, squelch)
```

**2. TNC Initialization:**
```bash
# Configure TNC for KISS mode
# Set serial port parameters
stty -F /dev/ttyUSB0 9600 raw -echo

# Verify TNC connection
echo -e "\xC0\xC0" > /dev/ttyUSB0
```

**3. AX.25 Interface Setup:**
```bash
# Attach KISS interface
sudo kissattach /dev/ttyUSB0 radio 192.168.25.10

# Configure interface
sudo ifconfig ax0 192.168.25.10 netmask 255.255.255.0

# Add route
sudo route add -net 192.168.25.0 netmask 255.255.255.0 dev ax0

# Verify interface
ifconfig ax0
```

**4. ION-DTN Startup:**
```bash
# Set ION configuration directory
export ION_CONFIG_DIR=/opt/ion-demo/config/node_a

# Start ION
ionstart -I ionconfig

# Load configuration
ionadmin ionrc
bpadmin bprc
ipnadmin ipnrc

# Verify ION status
bpstats
```

**5. Start Demo Applications:**
```bash
# Start receive application (on destination node)
./bprecv.py ipn:30.1 &

# Monitor status
./monitor.py &
```

### System Shutdown Procedure

**1. Stop Demo Applications:**
```bash
# Kill receive application
pkill -f bprecv.py

# Stop monitor
pkill -f monitor.py
```

**2. Stop ION-DTN:**
```bash
# Graceful shutdown
ionstop

# Verify shutdown
ps aux | grep ion
```

**3. Detach AX.25 Interface:**
```bash
# Remove interface
sudo ifconfig ax0 down

# Detach KISS
sudo killall kissattach
```

**4. Power Down Hardware:**
```bash
# Power off radios
# Disconnect TNCs if needed
```

### Demonstration Execution Procedure

**Mode 3: Relay Store-and-Forward (Example)**

**Setup:**
```bash
# On all nodes: Complete startup procedure
# Verify Node A can reach Node B
ping -c 3 192.168.25.20

# Verify Node B can reach Node C
ping -c 3 192.168.25.30

# Configure contact plan for non-overlapping windows
# A-to-B: UP, B-to-C: DOWN initially
```

**Execution:**
```bash
# On Node A: Send bundle to Node C
./bpsend.py ipn:30.1 "Test message from Node A"

# On Node B: Monitor queue
./queue_status.sh
# Should show bundle in queue for destination ipn:30.*

# Wait 2 minutes (demonstrate storage)

# Enable B-to-C contact
# (Update ION contact plan or enable RF path)

# On Node B: Observe forwarding
./queue_status.sh
# Should show bundle count decreasing

# On Node C: Observe delivery
# bprecv.py should display received bundle with timestamps
```

**Verification:**
- Bundle received at Node B (check logs)
- Bundle stored at Node B (check queue status)
- Bundle forwarded from Node B (check logs)
- Bundle delivered at Node C (check receive application output)
- Timestamps show creation time and delivery time
- Delay demonstrates store-and-forward behavior

### Troubleshooting Procedures

**Problem: No AX.25 connectivity**
```bash
# Check TNC connection
ls -l /dev/ttyUSB*

# Check KISS interface
ifconfig ax0

# Test with beacon
beacon -c N0CALL-1 -d BEACON ax0 "Test"

# Monitor with tcpdump
sudo tcpdump -i ax0 -vv
```

**Problem: ION not starting**
```bash
# Check ION logs
cat /var/ion/ion.log

# Verify configuration files
ionadmin ionrc

# Check for port conflicts
netstat -tulpn | grep 4556

# Clean ION state
ionstop
rm -rf /var/ion/*
ionstart -I ionconfig
```

**Problem: Bundles not forwarding**
```bash
# Check contact plan
ionadmin
l contact

# Check routing
ipnadmin
l plan

# Check queue status
bpstats

# Verify convergence layer
netstat -an | grep 4556
```

**Problem: TNC not responding**
```bash
# Reset TNC
# Power cycle or send reset command

# Verify serial port settings
stty -F /dev/ttyUSB0 -a

# Test with minicom
minicom -D /dev/ttyUSB0 -b 9600
```

### Maintenance Procedures

**Daily:**
- Check disk space for bundle storage
- Review logs for errors
- Verify TNC connections

**Weekly:**
- Clean old log files
- Verify ION database integrity
- Test all demonstration modes

**Monthly:**
- Update software if needed
- Backup configuration files
- Document any issues or improvements


## Design Decisions and Rationale

### IP-over-AX.25 vs. Custom Convergence Layer

**Decision:** Use IP-over-AX.25 with standard ION UDP/TCP convergence layers

**Rationale:**
- Minimizes custom software development
- Leverages existing ION convergence layers
- Uses standard Linux AX.25 stack
- Simplifies testing and troubleshooting
- Allows use of standard networking tools

**Trade-offs:**
- Additional protocol overhead (IP + UDP/TCP headers)
- Slightly reduced efficiency compared to native AX.25 convergence layer
- Acceptable for demonstration purposes given simplicity benefits

### Three-Node Linear Topology

**Decision:** Use fixed three-node linear topology (A→B→C)

**Rationale:**
- Simplest topology to demonstrate store-and-forward
- Clear demonstration of relay behavior
- Minimal configuration complexity
- Adequate for proving DTN concepts
- Easy to explain to audiences

**Trade-offs:**
- No redundant paths or mesh networking
- Single point of failure at relay node
- Not representative of complex DTN networks
- Acceptable for demonstration scope

### Scheduled Contact Windows

**Decision:** Use pre-defined contact schedules rather than opportunistic contacts

**Rationale:**
- Predictable demonstration behavior
- Simulates satellite pass operations
- Allows repeatable demonstrations
- Easy to explain and visualize
- Demonstrates DTN queuing behavior

**Trade-offs:**
- Less realistic than truly opportunistic contacts
- Requires time synchronization
- Manual or scripted control needed
- Acceptable for controlled demonstration environment

### Small Payload Sizes

**Decision:** Limit demonstration payloads to 256-1024 bytes

**Rationale:**
- Avoids fragmentation complexity
- Fast transfer times for demonstrations
- Reduces RF channel occupancy
- Simplifies testing and validation
- Adequate for demonstrating DTN concepts

**Trade-offs:**
- Not representative of large file transfers
- Doesn't demonstrate fragmentation handling
- Limited to text and small data
- Acceptable for initial demonstration scope

### Raspberry Pi Platform

**Decision:** Use Raspberry Pi 4 as Linux host platform

**Rationale:**
- Low cost and widely available
- Adequate performance for ION-DTN
- Good USB and serial port support
- Portable for field demonstrations
- Large community and documentation

**Trade-offs:**
- Limited to ARM architecture
- SD card reliability concerns
- Power supply requirements
- Acceptable for demonstration purposes

### KISS Mode TNCs

**Decision:** Use TNCs in KISS mode rather than command mode

**Rationale:**
- Direct frame control from host
- Simpler protocol (no AT commands)
- Better integration with AX.25 stack
- Standard approach for packet radio
- Widely supported by TNCs

**Trade-offs:**
- Requires KISS-capable TNC
- Less user-friendly than command mode
- Requires proper configuration
- Acceptable given technical benefits

### Phased Implementation

**Decision:** Implement in five phases from basic links to full demonstration

**Rationale:**
- Incremental validation reduces risk
- Each phase builds on previous success
- Easier troubleshooting of issues
- Clear milestones for progress tracking
- Allows early detection of problems

**Trade-offs:**
- Longer overall timeline
- More testing overhead
- Requires discipline to complete each phase
- Acceptable given complexity of system

## Future Enhancements

### Potential Improvements

**1. Mesh Topology:**
- Add redundant paths between nodes
- Implement dynamic routing
- Demonstrate path selection and failover

**2. Larger Payloads:**
- Support multi-kilobyte payloads
- Demonstrate fragmentation and reassembly
- Test with image files or logs

**3. Custody Transfer:**
- Implement BP custody transfer
- Demonstrate reliable delivery guarantees
- Handle custody acknowledgments

**4. Multiple Services:**
- Run multiple applications per node
- Demonstrate service multiplexing
- Use different endpoint IDs

**5. Mobile Nodes:**
- Add mobile node with intermittent connectivity
- Demonstrate opportunistic contacts
- Use GPS for location tracking

**6. Web Interface:**
- Develop web-based monitoring dashboard
- Real-time visualization of bundle flows
- Interactive demonstration control

**7. Higher Data Rates:**
- Use 9600 baud or higher
- Explore modern digital modes (e.g., APRS, D-STAR)
- Improve throughput for larger payloads

**8. Automated Testing:**
- Develop comprehensive test suite
- Automated regression testing
- Continuous integration for configuration changes

**9. Performance Optimization:**
- Tune ION parameters for packet radio
- Optimize contact plan execution
- Reduce protocol overhead

**10. Documentation:**
- Video tutorials for setup and operation
- Interactive demonstration scripts
- Troubleshooting guide with common issues

### Research Opportunities

**DTN Protocol Research:**
- Evaluate BP version 7 features
- Compare convergence layer performance
- Study queuing strategies and policies

**Packet Radio Optimization:**
- Optimize AX.25 parameters for DTN
- Evaluate different modulation schemes
- Study RF propagation effects on DTN

**Application Development:**
- Develop realistic DTN applications
- Study user experience with disrupted connectivity
- Evaluate application-layer protocols over DTN

## Conclusion

This design document specifies a complete ION-DTN demonstration system operating over UHF amateur packet radio. The system demonstrates delay-tolerant networking capabilities including store-and-forward relay, delayed delivery, and operation without continuous end-to-end connectivity.

The design emphasizes:
- Minimal custom software development
- Use of standard protocols and tools
- Phased implementation approach
- Operational simplicity
- Clear demonstration of DTN concepts

The system provides a foundation for understanding DTN operations and can be extended for more advanced demonstrations and research.

