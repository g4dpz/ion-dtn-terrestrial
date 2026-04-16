# Requirements Document

## Introduction

A new `ping` subcommand for the existing `kiss_interface` tool that sends a packet over the air via the KISS/AX.25 stack, switches to receive mode on the same serial file descriptor, waits for an echo reply from a remote station (running `echo` mode), and measures round-trip time. The feature supports multiple sequential pings with configurable count, interval, and timeout, and prints summary statistics on completion. This exercises single-fd bidirectional TX/RX patterns needed for LTP handshake logic and provides round-trip timing data useful for estimating one-way light time for LTP retransmission timers.

## Glossary

- **KISS_Interface**: The main command-line tool that manages USB serial communication with the TNC using the KISS protocol
- **Ping_Engine**: The component within KISS_Interface that orchestrates the send-then-receive cycle for each ping sequence number, measures round-trip time, and accumulates statistics
- **RTT**: Round-Trip Time — the elapsed wall-clock time between transmitting a ping packet and receiving the corresponding echo reply
- **Sequence_Number**: A monotonically increasing 16-bit unsigned integer embedded in each ping payload, used to match echo replies to their originating ping request
- **Ping_Payload**: The data embedded in the AX.25 information field of a ping packet, containing a magic identifier, the Sequence_Number, and a transmit timestamp
- **Echo_Station**: A remote station running `kiss_interface echo` mode that receives packets and retransmits them with swapped callsigns
- **KISS_Encoder**: The component that wraps raw data into KISS frames using FEND/FESC byte-stuffing
- **KISS_Decoder**: The component that extracts data payloads from received KISS frames, reversing byte-stuffing
- **AX25_Framer**: The component that builds and parses AX.25 UI frames with source/destination callsign headers
- **Serial_Port**: The USB serial device file representing the Mobilinkd TNC3
- **Summary_Statistics**: The aggregate results printed after a ping session: packets sent, packets received, packet loss percentage, and minimum/average/maximum RTT values

## Requirements

### Requirement 1: Ping Subcommand CLI Integration

**User Story:** As a radio operator, I want a `ping` subcommand in the existing tool, so that I can test round-trip connectivity and measure link timing from the command line.

#### Acceptance Criteria

1. THE KISS_Interface SHALL accept `ping` as a subcommand in addition to the existing `send`, `receive`, and `echo` subcommands
2. WHEN the `ping` subcommand is used, THE KISS_Interface SHALL require `--device`, `--src`, and `--dst` options
3. WHEN the `ping` subcommand is used, THE KISS_Interface SHALL accept a `--count` option specifying the number of ping packets to send (default: 4)
4. WHEN the `ping` subcommand is used, THE KISS_Interface SHALL accept a `--timeout` option specifying the maximum time in milliseconds to wait for each echo reply (default: 5000)
5. WHEN the `ping` subcommand is used, THE KISS_Interface SHALL accept an `--interval` option specifying the delay in milliseconds between consecutive ping transmissions (default: 1000)
6. IF a required argument for ping mode is missing, THEN THE KISS_Interface SHALL print a specific error message identifying the missing argument and exit with a non-zero status code
7. WHEN invoked with `--help`, THE KISS_Interface SHALL include the `ping` subcommand and its options in the usage output

### Requirement 2: Ping Packet Construction

**User Story:** As a radio operator, I want each ping packet to carry a sequence number and timestamp, so that replies can be matched to requests and round-trip time can be calculated.

#### Acceptance Criteria

1. WHEN constructing a ping packet, THE Ping_Engine SHALL build a Ping_Payload containing a 4-byte magic identifier (ASCII "PING"), a 16-bit Sequence_Number in network byte order, and an 8-byte transmit timestamp in microseconds (using `clock_gettime(CLOCK_MONOTONIC)`)
2. WHEN constructing a ping packet, THE Ping_Engine SHALL wrap the Ping_Payload in an AX.25 UI frame using the configured source and destination callsigns, then KISS-encode the frame
3. THE Ping_Engine SHALL increment the Sequence_Number by one for each successive ping packet, starting from 1

### Requirement 3: Ping Transmission and Reception Cycle

**User Story:** As a radio operator, I want the tool to send a ping and then listen for the reply on the same serial connection, so that I can verify bidirectional communication over a single link.

#### Acceptance Criteria

1. WHEN a ping packet is transmitted, THE Ping_Engine SHALL write the KISS-encoded frame to the Serial_Port and call `tcdrain` to ensure the TNC has received the complete frame
2. WHEN a ping packet has been transmitted, THE Ping_Engine SHALL immediately switch to receive mode on the same Serial_Port file descriptor and listen for an echo reply
3. WHILE waiting for an echo reply, THE Ping_Engine SHALL use `poll()` or `select()` with the configured timeout to avoid blocking indefinitely on the serial read
4. WHEN a KISS frame is received during the reply wait period, THE Ping_Engine SHALL decode the KISS frame, strip the AX.25 header, and examine the Ping_Payload for a matching magic identifier and Sequence_Number
5. WHEN a received echo reply contains a Sequence_Number that does not match the expected value, THE Ping_Engine SHALL discard the reply and continue waiting until the timeout expires
6. WHEN a valid echo reply is received, THE Ping_Engine SHALL compute the RTT by subtracting the transmit timestamp (from the Ping_Payload) from the current monotonic clock time

### Requirement 4: Timeout Handling

**User Story:** As a radio operator, I want missed replies to be reported after a timeout, so that I can identify packet loss on the link.

#### Acceptance Criteria

1. IF no valid echo reply is received within the configured timeout period, THEN THE Ping_Engine SHALL print a timeout message to stdout identifying the Sequence_Number of the lost packet
2. WHEN a timeout occurs, THE Ping_Engine SHALL increment the lost-packet counter and proceed to the next ping sequence
3. WHILE the timeout period has not expired and no matching reply has been received, THE Ping_Engine SHALL continue reading from the Serial_Port

### Requirement 5: Per-Ping Output

**User Story:** As a radio operator, I want to see the result of each ping as it completes, so that I can monitor link quality in real time.

#### Acceptance Criteria

1. WHEN a valid echo reply is received, THE Ping_Engine SHALL print a line to stdout containing the payload size in bytes, the source callsign of the reply, the Sequence_Number, and the RTT in milliseconds with fractional precision
2. WHEN the verbose flag is enabled, THE Ping_Engine SHALL print a hex dump of the received echo frame
3. WHEN a ping session begins, THE Ping_Engine SHALL print a header line identifying the destination callsign and the configured count

### Requirement 6: Summary Statistics

**User Story:** As a radio operator, I want summary statistics after all pings complete, so that I can assess overall link quality and timing characteristics.

#### Acceptance Criteria

1. WHEN all ping sequences have completed or the session is interrupted by SIGINT, THE Ping_Engine SHALL print a summary line containing the number of packets transmitted, the number of packets received, and the packet loss percentage
2. WHEN at least one echo reply was received, THE Ping_Engine SHALL print the minimum, average, and maximum RTT values in milliseconds
3. THE Ping_Engine SHALL compute the average RTT as the arithmetic mean of all successful RTT measurements

### Requirement 7: Signal Handling in Ping Mode

**User Story:** As a radio operator, I want to interrupt a ping session with Ctrl-C and still see the summary, so that I can stop early without losing collected data.

#### Acceptance Criteria

1. WHEN the user sends SIGINT or SIGTERM during a ping session, THE Ping_Engine SHALL stop transmitting further ping packets and exit the send/receive loop
2. WHEN a ping session is interrupted by a signal, THE Ping_Engine SHALL print the Summary_Statistics for all pings completed before the interruption
3. WHEN a ping session is interrupted by a signal, THE KISS_Interface SHALL close the Serial_Port and exit with status code 0

### Requirement 8: Ping Payload Round-Trip Integrity

**User Story:** As a developer, I want to verify that ping payload construction and parsing are exact inverses, so that sequence numbers and timestamps survive the echo round-trip without corruption.

#### Acceptance Criteria

1. FOR ALL Sequence_Number values (0-65535) and valid monotonic timestamp values, THE Ping_Engine constructing a Ping_Payload and then parsing the same byte sequence SHALL recover the original Sequence_Number and timestamp
2. FOR ALL valid ping payloads, THE Ping_Engine SHALL identify a Ping_Payload by the presence of the 4-byte magic identifier "PING" at offset 0
3. IF a received payload does not begin with the magic identifier "PING", THEN THE Ping_Engine SHALL treat the payload as a non-ping packet and discard the payload without error
