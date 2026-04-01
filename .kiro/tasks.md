# Implementation Plan: ION-DTN UHF Demonstration System

## Overview

This implementation plan breaks down the ION-DTN UHF demonstration system into discrete coding tasks following a phased approach. Each phase builds on the previous one, starting with basic link validation and progressing to full multi-hop store-and-forward demonstrations with scheduled contact windows.

The system demonstrates delay-tolerant networking over UHF amateur packet radio using a three-node topology (Source → Relay → Destination) with LTP (Licklider Transmission Protocol) directly over serial KISS interfaces.

## Tasks

- [x] 1. Create project directory structure and configuration templates
  - Create `/opt/ion-demo/` directory structure with subdirectories for config, scripts, contact_plans, logs, and data
  - Create configuration template files for three nodes (node_a, node_b, node_c)
  - Create placeholder files for ION configuration (ionconfig, ionrc, bprc, ipnrc) for each node
  - _Requirements: 45.1, 45.3_

- [ ] 2. Phase 1: Bench Link Bring-Up - TNC and AX.25 Configuration
  - [x] 2.1 Create TNC initialization script
    - Write `scripts/init_tnc.sh` to configure serial port parameters and enter KISS mode
    - Include serial port device configuration (stty settings for 9600 baud, raw mode)
    - Add TNC connection verification checks
    - _Requirements: 4.1, 4.2, 18.4_

  - [ ] 2.2 Create LTP interface configuration script
    - Write `scripts/setup_ltp.sh` to configure LTP over serial KISS interface
    - Configure ION ltpcli/ltpclo for serial device
    - Set appropriate LTP parameters for 1200 baud packet radio
    - **NOTE:** LTP operates directly over serial, no IP layer needed
    - _Requirements: 6.1, 6.2, 6.3, 33.1, 33.2, 46.1_

  - [ ]* 2.3 Write property test for KISS frame transmission
    - **Property 10: KISS Frame Transmission**
    - **Validates: Requirements 4.3**
    - Generate random valid KISS frames and verify TNC transmits corresponding AX.25 packets

  - [ ]* 2.4 Write property test for AX.25 callsign validation
    - **Property 12: AX.25 Frame Contains Correct Callsign**
    - **Validates: Requirements 5.5**
    - For any transmission, verify AX.25 frame contains assigned callsign and SSID

  - [ ] 2.5 Create link validation test script
    - Write `scripts/test_link.sh` to verify IP connectivity between two nodes
    - Include ping tests with latency and packet loss measurement
    - Add AX.25 interface status checks
    - Log results for validation
    - _Requirements: 19.4, 26.1, 26.3, 26.4_

- [ ] 3. Checkpoint - Verify Phase 1 completion
  - Ensure TNC initialization works on all nodes
  - Ensure AX.25 interfaces are up with correct IP addresses
  - Ensure ping succeeds between connected nodes with <1s latency and <5% loss
  - Ask the user if questions arise


- [ ] 4. Phase 2: Two-Node ION Test - ION Installation and Configuration
  - [ ] 4.1 Create ION configuration files for Node A (Source)
    - Write `config/node_a/ionconfig` with ION memory and path settings
    - Write `config/node_a/ionrc` with node identity (dtn://g4dpz-1/), contact plan, and range definitions
    - Write `config/node_a/bprc` with scheme, endpoints, protocol, and convergence layer configuration
    - Write `config/node_a/dtn2rc` with routing plans for destinations
    - **NOTE:** IP addresses in examples use 192.168.25.x - replace with AMPRNet addresses (44.x.y.z) from https://portal.ampr.org/ for production
    - _Requirements: 2.1, 2.4, 2.5, 33.1, 33.4, 42.1, 46.1, 46.2, 46.5_

  - [ ] 4.2 Create ION configuration files for Node B (Relay)
    - Write `config/node_b/ionconfig` with ION memory and path settings
    - Write `config/node_b/ionrc` with node identity (ipn:20.1), contact plan, and range definitions
    - Write `config/node_b/bprc` with scheme, endpoints, protocol, and convergence layer configuration
    - Write `config/node_b/ipnrc` with routing plans for both upstream and downstream
    - _Requirements: 2.2, 2.4, 2.6, 33.1, 33.4, 42.2, 42.3_

  - [ ] 4.3 Create ION configuration files for Node C (Destination)
    - Write `config/node_c/ionconfig` with ION memory and path settings
    - Write `config/node_c/ionrc` with node identity (ipn:30.1), contact plan, and range definitions
    - Write `config/node_c/bprc` with scheme, endpoints, protocol, and convergence layer configuration
    - Write `config/node_c/ipnrc` with routing plans for sources
    - _Requirements: 2.3, 2.4, 2.7, 33.1, 33.4, 42.4_

  - [ ] 4.4 Create ION startup script
    - Write `scripts/start_ion.sh` to start ION with configuration loading
    - Include ionstart command with configuration directory parameter
    - Add ionadmin, bpadmin, and ipnadmin commands to load configurations
    - Include ION status verification (bpstats)
    - _Requirements: 2.4, 44.1_

  - [ ] 4.5 Create ION shutdown script
    - Write `scripts/stop_ion.sh` for graceful ION shutdown
    - Include ionstop command and verification
    - Add cleanup of stale processes if needed
    - _Requirements: 44.1_

  - [ ]* 4.6 Write property test for bundle persistent storage
    - **Property 2: Bundle Persistent Storage Round-Trip**
    - **Validates: Requirements 8.2, 8.3, 28.1, 28.2, 28.3**
    - For any bundle stored, verify it's recovered after node restart with all metadata intact


- [ ] 5. Phase 2: Demo Applications - Send and Receive
  - [ ] 5.1 Create bundle send application
    - Write `scripts/bpsend.py` to create and send bundles via ION
    - Implement JSON payload creation with origin, created timestamp (ISO 8601), type, sequence, and value fields
    - Use bpsendfile command to transmit bundles
    - Add command-line argument parsing for destination EID and message
    - Include error handling and status reporting
    - _Requirements: 7.1, 7.2, 7.5, 22.1, 22.3, 23.1, 23.2, 23.3, 23.4, 30.1, 30.2, 30.3, 30.4_

  - [ ]* 5.2 Write property test for bundle creation metadata
    - **Property 1: Bundle Creation Includes Required Metadata**
    - **Validates: Requirements 7.4, 7.5**
    - For any data provided, verify created bundle includes creation timestamp, source EID, and destination EID

  - [ ]* 5.3 Write property test for JSON payload structure
    - **Property 27: JSON Payload Structure**
    - **Validates: Requirements 30.1, 30.2, 30.3, 30.4**
    - For any JSON payload, verify it includes "origin", "created", "type", and "value" fields

  - [ ]* 5.4 Write property test for ISO 8601 timestamp format
    - **Property 26: ISO 8601 Timestamp Format**
    - **Validates: Requirements 23.2**
    - For any creation timestamp, verify it uses valid ISO 8601 format

  - [ ] 5.5 Create bundle receive application
    - Write `scripts/bprecv.py` to receive and display bundles
    - Use bprecvfile command in loop to receive bundles
    - Parse JSON payloads and extract fields
    - Display payload, creation timestamp, and delivery timestamp
    - Log received bundles with all metadata
    - _Requirements: 10.1, 10.3, 22.2, 22.4, 30.5_

  - [ ]* 5.6 Write property test for end-to-end payload integrity
    - **Property 3: End-to-End Payload Integrity**
    - **Validates: Requirements 10.2**
    - For any data sent by source application, verify delivered payload matches exactly

  - [ ]* 5.7 Write property test for JSON payload round-trip
    - **Property 28: JSON Payload Round-Trip**
    - **Validates: Requirements 30.5**
    - For any valid JSON payload, verify demo application successfully parses and displays it

  - [ ] 5.8 Create sequence number management
    - Implement sequence number tracking in send application
    - Store last sequence number in file for persistence
    - Increment on each send
    - _Requirements: 23.4_

  - [ ] 5.9 Create two-node bundle transfer test
    - Write test script to send bundle from Node A to Node B
    - Verify bundle delivery within 10 seconds
    - Validate payload integrity
    - Check ION logs for successful transfer
    - _Requirements: 12.1, 12.2, 12.3_

- [ ] 6. Checkpoint - Verify Phase 2 completion
  - Ensure ION starts successfully on all nodes
  - Ensure bundles can be sent and received between two nodes
  - Ensure payload integrity is maintained
  - Ensure delivery completes within 10 seconds
  - Ask the user if questions arise


- [ ] 7. Phase 3: Delayed Delivery Test - Contact Plan and Queue Management
  - [ ] 7.1 Create contact plan configuration files
    - Write `contact_plans/continuous.plan` for continuous contact (Phase 2 testing)
    - Write `contact_plans/delayed.plan` for delayed delivery with scheduled outage and restoration
    - Write `contact_plans/relay.plan` for non-overlapping windows (A-B up, B-C down, then switch)
    - Write `contact_plans/interrupted.plan` for mid-transfer interruption scenarios
    - Use ION contact plan format with start times, durations, and node pairs
    - _Requirements: 11.1, 11.2, 11.3, 31.1, 31.2, 31.3, 31.4, 31.5_

  - [ ] 7.2 Create contact plan loader script
    - Write `scripts/load_contact_plan.sh` to load contact plan into ION
    - Parse contact plan file and generate ionadmin commands
    - Apply contact plan to running ION instance
    - Verify contact plan loaded correctly
    - _Requirements: 31.4_

  - [ ] 7.3 Create manual link control script
    - Write `scripts/control_link.sh` to manually enable/disable RF paths
    - Implement enable and disable functions using ION contact plan updates
    - Add status display showing current link state
    - _Requirements: 39.1, 39.2, 39.5_

  - [ ]* 7.4 Write property test for contact window RF path control
    - **Property 7: Contact Window Enables RF Path**
    - **Property 8: Contact Window Disables RF Path**
    - **Validates: Requirements 11.2, 11.3, 21.2, 21.3**
    - For any scheduled contact window, verify RF path is enabled/disabled at scheduled time

  - [ ]* 7.5 Write property test for disabled path prevents transmission
    - **Property 9: Disabled Path Prevents Transmission**
    - **Validates: Requirements 39.5**
    - For any disabled RF path, verify bundle transmission is prevented until re-enabled

  - [ ] 7.6 Create queue status monitoring script
    - Write `scripts/queue_status.sh` to display ION queue depths and link status
    - Query bpstats for bundle counts and sizes
    - Display outbound and inbound queue information
    - Show link status (up/down) and next contact time
    - _Requirements: 17.1, 17.2, 17.3, 17.4, 40.1, 40.2, 40.3, 40.4, 40.5, 44.4_

  - [ ]* 7.7 Write property test for bundle queue retention during outage
    - **Property 4: Bundle Queue Retention During Outage**
    - **Validates: Requirements 13.1, 13.2, 8.4**
    - For any bundle sent when RF path is disabled, verify bundle remains in queue until restoration

  - [ ]* 7.8 Write property test for bundle transmission after restoration
    - **Property 5: Bundle Transmission After Path Restoration**
    - **Validates: Requirements 13.3, 15.3**
    - For any queued bundle, verify transmission occurs when RF path is restored

  - [ ] 7.9 Create delayed delivery test script
    - Write test to send bundle with link down
    - Verify bundle queued locally
    - Restore link after delay (2-5 minutes)
    - Verify automatic transmission and delivery
    - Measure and display delay between creation and delivery
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5_

- [ ] 8. Checkpoint - Verify Phase 3 completion
  - Ensure contact plans load correctly
  - Ensure bundles queue when link is down
  - Ensure bundles transmit automatically when link is restored
  - Ensure queue status monitoring works correctly
  - Ask the user if questions arise


- [ ] 9. Phase 4: Three-Node Relay Test - Store-and-Forward Implementation
  - [ ] 9.1 Update ION configurations for three-node topology
    - Modify Node A configuration to route to Node C via Node B
    - Modify Node B configuration to accept from Node A and forward to Node C
    - Modify Node C configuration to accept from Node B
    - Update contact plans for non-overlapping windows
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 42.1, 42.2, 42.3, 42.4, 42.5_

  - [ ] 9.2 Create relay node monitoring display
    - Write `scripts/relay_monitor.py` to display relay node status
    - Show bundles received from upstream
    - Show bundles queued for downstream
    - Show bundles successfully forwarded
    - Display link status for both upstream and downstream
    - _Requirements: 17.1, 17.2, 17.3, 17.4, 17.5_

  - [ ]* 9.3 Write property test for bundle reception and storage
    - **Property 18: Bundle Reception and Storage**
    - **Validates: Requirements 8.1, 8.2**
    - For any bundle arriving at a node, verify it's received and stored persistently

  - [ ]* 9.4 Write property test for successful forwarding removes bundle
    - **Property 6: Successful Forwarding Removes Bundle from Queue**
    - **Validates: Requirements 9.4, 10.4, 28.4**
    - For any successfully forwarded or delivered bundle, verify it's removed from storage

  - [ ] 9.5 Create three-node relay test script
    - Write test to send bundle from Node A to Node C
    - Verify bundle arrives at Node B during first contact window
    - Verify bundle queued at Node B when B-C link is down
    - Enable B-C link
    - Verify bundle forwarded from Node B to Node C
    - Verify delivery at Node C
    - Confirm no direct A-C connectivity
    - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5, 9.1, 9.2, 9.3, 9.4, 9.5_

  - [ ]* 9.6 Write property test for multi-hop bundle delivery
    - **Property 21: Multi-Hop Bundle Delivery**
    - **Validates: Requirements 14.4**
    - For any bundle sent from Source to Destination via Relay, verify eventual successful delivery

  - [ ] 9.7 Create interrupted forwarding test script
    - Write test to begin bundle transmission and interrupt mid-transfer
    - Verify bundle retained in queue after interruption
    - Restore link
    - Verify retry and successful delivery
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5_

  - [ ]* 9.8 Write property test for bundle delivery after interruption
    - **Property 22: Bundle Delivery After Interruption**
    - **Validates: Requirements 15.4**
    - For any interrupted transmission, verify successful delivery on subsequent contact

- [ ] 10. Checkpoint - Verify Phase 4 completion
  - Ensure three-node topology is configured correctly
  - Ensure bundles relay through Node B with store-and-forward
  - Ensure relay monitoring displays correct status
  - Ensure interrupted transfers recover successfully
  - Ask the user if questions arise


- [ ] 11. Phase 5: Scheduled Contact Demo - Automated Contact Plan Execution
  - [ ] 11.1 Create automated contact plan executor
    - Write `scripts/contact_executor.py` to execute contact plans automatically
    - Parse contact plan schedule with start times, durations, and link IDs
    - Implement timing loop to enable/disable links at scheduled times
    - Add enable_link() and disable_link() functions using ION contact plan updates
    - Include timing accuracy validation (within 1 second)
    - Support multiple cycle execution
    - _Requirements: 21.1, 21.2, 21.3, 21.4, 21.5_

  - [ ]* 11.2 Write property test for automated contact plan execution
    - **Property 7: Contact Window Enables RF Path**
    - **Property 8: Contact Window Disables RF Path**
    - **Validates: Requirements 11.2, 11.3, 21.2, 21.3**
    - For automated execution, verify contact windows transition correctly at scheduled times

  - [ ] 11.3 Create demonstration orchestration script
    - Write `scripts/run_demo.sh` to orchestrate complete demonstration
    - Start all nodes with appropriate configurations
    - Load contact plan
    - Start contact executor
    - Send demonstration bundles at appropriate times
    - Display status and results
    - Support all four demonstration modes
    - _Requirements: 44.1, 44.2, 44.3, 44.4, 44.5_

  - [ ] 11.4 Create demonstration reset script
    - Write `scripts/reset_demo.sh` to clear demonstration state
    - Stop ION on all nodes
    - Clear bundle storage
    - Reset sequence numbers
    - Prepare for next demonstration run
    - _Requirements: 43.2, 43.3, 43.5_

  - [ ]* 11.5 Write property test for demonstration repeatability
    - **Property 35: Demonstration Repeatability**
    - **Validates: Requirements 43.1**
    - For any demonstration sequence, verify consistent results when reset and repeated

  - [ ] 11.6 Create multi-cycle test script
    - Write test to execute 3+ complete demonstration cycles
    - Verify consistent behavior across cycles
    - Measure timing accuracy
    - Validate no manual intervention required
    - _Requirements: 21.4, 43.1, 43.2, 43.4_

- [ ] 12. Checkpoint - Verify Phase 5 completion
  - Ensure automated contact plan execution works correctly
  - Ensure multiple cycles run without manual intervention
  - Ensure timing accuracy is within 1 second
  - Ensure demonstration is repeatable
  - Ask the user if questions arise


- [ ] 13. Monitoring and Logging Tools
  - [ ] 13.1 Create real-time status monitor
    - Write `scripts/monitor.py` using curses for real-time display
    - Display node identity and current time
    - Show outbound and inbound queue depths (bundle count and bytes)
    - Show link status for all connections
    - Update display every second
    - Support keyboard quit command
    - _Requirements: 17.1, 17.2, 17.3, 17.4, 17.5, 40.1, 40.2, 40.3_

  - [ ] 13.2 Create event logging system
    - Write `scripts/log_events.py` to log significant ION events
    - Log bundle reception with timestamp and source
    - Log bundle storage with bundle ID
    - Log bundle forwarding with destination
    - Log bundle delivery with timestamps
    - Write logs to `/opt/ion-demo/logs/` directory
    - _Requirements: 24.5_

  - [ ] 13.3 Create diagnostic tools script
    - Write `scripts/diagnostics.sh` to check system status
    - Check TNC connection and serial port status
    - Check AX.25 interface status and IP connectivity
    - Check ION process status
    - Display ION routing tables and contact schedules
    - Report any detected issues
    - _Requirements: 24.1, 24.2, 24.3, 24.4_

  - [ ] 13.4 Create error detection and reporting
    - Implement TNC connection failure detection in monitoring tools
    - Add error logging for bundle storage failures
    - Add error logging for bundle forwarding failures
    - Display error counts in status monitor
    - _Requirements: 35.1, 35.2, 35.3_

  - [ ]* 13.5 Write property test for TNC connection failure detection
    - **Property 29: TNC Connection Failure Detection**
    - **Validates: Requirements 35.1**
    - For any TNC connection loss, verify node detects failure and logs error

  - [ ]* 13.6 Write property test for queue depth monitoring
    - **Property 32: Queue Depth Monitoring**
    - **Validates: Requirements 40.1, 40.2**
    - For any node, verify it provides current count of bundles in outbound and inbound queues


- [ ] 14. Demonstration Mode Scripts
  - [ ] 14.1 Create Mode 1 demonstration script (Point-to-Point Real-Time)
    - Write `scripts/demo_mode1.sh` for real-time two-node transfer
    - Configure continuous contact window
    - Send bundle and verify immediate delivery (<10 seconds)
    - Display creation and delivery timestamps
    - _Requirements: 12.1, 12.2, 12.3, 12.4_

  - [ ] 14.2 Create Mode 2 demonstration script (Delayed Point-to-Point)
    - Write `scripts/demo_mode2.sh` for delayed delivery
    - Disable link initially
    - Send bundle and show queuing
    - Wait 2 minutes to demonstrate storage
    - Enable link and show automatic transmission
    - Display delay between creation and delivery
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5_

  - [ ] 14.3 Create Mode 3 demonstration script (Relay Store-and-Forward)
    - Write `scripts/demo_mode3.sh` for three-node relay
    - Configure A-B link up, B-C link down
    - Send bundle from A to C
    - Show bundle arrival and queuing at B
    - Enable B-C link
    - Show forwarding from B to C
    - Display end-to-end delivery with timestamps
    - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5_

  - [ ] 14.4 Create Mode 4 demonstration script (Interrupted Forwarding)
    - Write `scripts/demo_mode4.sh` for interrupted transfer
    - Begin bundle transmission
    - Interrupt link mid-transfer
    - Show bundle retention
    - Restore link
    - Show retry and successful delivery
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5_

  - [ ] 14.5 Create public demonstration script
    - Write `scripts/public_demo.sh` following demonstration script requirements
    - Display all three nodes and callsigns at start
    - Show initial link states (A-B up, B-C down)
    - Send message from A to C
    - Show arrival at B only
    - Show message queued on B
    - Enable B-C link
    - Show forwarding from B
    - Show delivery at C with both timestamps
    - _Requirements: 34.1, 34.2, 34.3, 34.4, 34.5, 34.6, 34.7, 34.8_


- [ ] 15. Testing and Validation
  - [ ] 15.1 Create unit test suite for demo applications
    - Write unit tests for JSON payload parsing and generation
    - Test timestamp format validation
    - Test message sequence numbering
    - Test error handling for invalid payloads
    - _Requirements: 23.1, 23.2, 23.3, 23.4, 30.5_

  - [ ]* 15.2 Write property test for payload size support
    - **Property 23: Payload Size Support**
    - **Validates: Requirements 16.5**
    - For any payload up to 1 kilobyte, verify successful transfer

  - [ ]* 15.3 Write property test for message payload structure
    - **Property 25: Message Payload Structure**
    - **Validates: Requirements 23.1, 23.3, 23.4**
    - For any message payload, verify it includes creation timestamp, origin node ID, and sequence number

  - [ ] 15.4 Create integration test suite
    - Write integration tests for two-node bundle transfer
    - Write integration tests for three-node relay
    - Write integration tests for contact window scheduling
    - Write integration tests for interrupted transfer recovery
    - _Requirements: 25.1, 25.2, 25.3, 25.4, 25.5_

  - [ ] 15.5 Create validation test for all demonstration modes
    - Test Mode 1 (real-time) meets success criteria
    - Test Mode 2 (delayed) meets success criteria
    - Test Mode 3 (relay) meets success criteria
    - Test Mode 4 (interrupted) meets success criteria
    - Verify all four modes complete successfully
    - _Requirements: 29.1, 29.2, 29.3, 29.4, 29.5_

  - [ ]* 15.6 Write property test for IP packet encapsulation
    - **Property 13: IP Packet Encapsulation**
    - **Property 14: IP Packet De-encapsulation**
    - **Validates: Requirements 6.2, 6.3**
    - For any IP packet, verify correct encapsulation in AX.25 frames and extraction

  - [ ]* 15.7 Write property test for bundle queue maintenance
    - **Property 19: Bundle Queue Maintenance**
    - **Validates: Requirements 8.5**
    - For any node, verify it maintains a queue of stored bundles awaiting transmission


- [ ] 16. Documentation and User Guides
  - [ ] 16.1 Create hardware setup documentation
    - Write `docs/hardware_setup.md` documenting hardware configuration for each node
    - Document TNC models, radio models, and cable requirements
    - Document bench configuration with dummy loads
    - Document field configuration with antenna systems
    - Include wiring diagrams and connection details
    - _Requirements: 38.1, 38.2, 38.3, 38.4, 38.5, 45.1_

  - [ ] 16.2 Create software installation guide
    - Write `docs/installation.md` with step-by-step installation instructions
    - Document Linux package installation (AX.25 tools, build tools)
    - Document ION-DTN compilation and installation
    - Document Python dependencies
    - Document directory structure creation
    - _Requirements: 45.2_

  - [ ] 16.3 Create configuration guide
    - Write `docs/configuration.md` explaining ION configuration files
    - Document ionconfig, ionrc, bprc, and ipnrc for each node
    - Explain contact plan format and examples
    - Document AX.25 configuration (axports, kissattach)
    - Provide configuration templates and examples
    - _Requirements: 45.3, 45.4_

  - [ ] 16.4 Create operation manual
    - Write `docs/operation.md` with operational procedures
    - Document system startup procedure (hardware, TNC, AX.25, ION)
    - Document system shutdown procedure
    - Document demonstration execution for all four modes
    - Document troubleshooting procedures for common issues
    - _Requirements: 45.5_

  - [ ] 16.5 Create quick start guide
    - Write `docs/quickstart.md` with minimal steps to run first demonstration
    - Provide simple commands for each step
    - Include verification checks
    - Reference detailed documentation for more information
    - _Requirements: 44.1, 44.2, 44.3, 44.4, 44.5_

  - [ ] 16.6 Create README file
    - Write `README.md` with project overview and purpose
    - Include system requirements and prerequisites
    - Provide links to detailed documentation
    - Include quick start instructions
    - Document demonstration modes and success criteria
    - _Requirements: 45.1, 45.2, 45.3, 45.4, 45.5_


- [ ] 17. Error Handling and Recovery
  - [ ] 17.1 Implement TNC error handling
    - Add TNC connection monitoring to initialization script
    - Implement automatic reconnection on connection loss
    - Add error logging for TNC failures
    - Display TNC status in monitoring tools
    - _Requirements: 35.1, 35.2_

  - [ ] 17.2 Implement bundle error handling
    - Add bundle validation in send application
    - Implement error handling for storage failures
    - Add retry logic for forwarding failures
    - Log bundle errors with details
    - _Requirements: 35.3, 35.4_

  - [ ] 17.3 Implement link quality monitoring
    - Add packet loss measurement to link validation
    - Monitor AX.25 retransmission rates
    - Display link quality metrics in status monitor
    - Log link quality degradation warnings
    - _Requirements: 26.3, 35.4_

  - [ ] 17.4 Create error recovery procedures
    - Document recovery steps for common failures
    - Implement automatic recovery where possible
    - Add manual recovery commands to scripts
    - Test recovery from TNC disconnection, storage failures, and link interruptions
    - _Requirements: 35.1, 35.2, 35.3, 35.4, 35.5_

- [ ] 18. Final Integration and System Testing
  - [ ] 18.1 Perform end-to-end system test
    - Test complete system with all three nodes
    - Execute all four demonstration modes
    - Verify all monitoring and logging tools work
    - Test error handling and recovery
    - Validate against all success criteria
    - _Requirements: 29.1, 29.2, 29.3, 29.4, 29.5_

  - [ ] 18.2 Perform repeatability testing
    - Execute demonstration multiple times
    - Verify consistent results across runs
    - Test reset and restart procedures
    - Validate timing accuracy across multiple cycles
    - _Requirements: 43.1, 43.2, 43.3, 43.4, 43.5_

  - [ ] 18.3 Perform stress testing
    - Test with maximum payload sizes (1KB)
    - Test with multiple bundles in queue
    - Test with extended outage periods
    - Test with rapid contact window transitions
    - Verify system stability under load
    - _Requirements: 16.5, 26.1, 26.3_

  - [ ] 18.4 Create final validation checklist
    - Document all requirements and their validation status
    - Verify all demonstration modes meet success criteria
    - Confirm all documentation is complete
    - Validate all scripts and tools function correctly
    - Prepare system for demonstration use
    - _Requirements: 29.1, 29.2, 29.3, 29.4, 29.5_

- [ ] 19. Final checkpoint - System ready for demonstration
  - Ensure all phases complete successfully
  - Ensure all demonstration modes work correctly
  - Ensure all documentation is complete and accurate
  - Ensure system is stable and repeatable
  - Ask the user if questions arise


## Notes

- Tasks marked with `*` are optional property-based tests and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation at the end of each phase
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The phased approach allows incremental build-up and validation
- All scripts should include error handling and status reporting
- Configuration files should be well-commented for maintainability
- Documentation should be clear and complete for reproducibility

## Implementation Language

- Shell scripts (Bash): System initialization, configuration, and control scripts
- Python 3: Demo applications, monitoring tools, contact plan executor, and property-based tests
- Configuration files: ION configuration format, contact plan format
- Documentation: Markdown format

## Success Criteria

- **Minimum Success**: Transfer at least one bundle point-to-point over UHF packet link (Phase 2)
- **Good Success**: Queue bundle during outage and deliver after restoration (Phase 3)
- **Strong Success**: Deliver bundle from Source to Destination through Relay with store-and-forward (Phase 4)
- **Excellent Success**: Execute repeated automated contact-plan operations over multiple cycles (Phase 5)

## Testing Approach

- **Unit Tests**: Verify specific examples and edge cases for individual components
- **Property-Based Tests**: Verify universal properties across all inputs (minimum 100 iterations per test)
- **Integration Tests**: Verify component interactions and end-to-end flows
- **System Tests**: Verify complete demonstration modes and success criteria
- **Repeatability Tests**: Verify consistent behavior across multiple runs

## Phased Validation

Each phase must be completed and validated before proceeding to the next:

1. **Phase 1**: Basic link connectivity (TNC, AX.25, IP)
2. **Phase 2**: ION installation and two-node bundle transfer
3. **Phase 3**: Delayed delivery with queue management
4. **Phase 4**: Three-node relay with store-and-forward
5. **Phase 5**: Automated scheduled contact demonstrations

