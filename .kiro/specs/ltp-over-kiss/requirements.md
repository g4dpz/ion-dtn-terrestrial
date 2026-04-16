# Requirements Document

## Introduction

LTP-over-KISS replaces the AX.25 framing layer in the existing `kiss_interface` tool with the Licklider Transmission Protocol (LTP). LTP provides reliable data transfer over links with long or variable delays, making it suitable for 1200 baud amateur radio links where round-trip times are measured in seconds. LTP segments are framed directly inside KISS frames (bypassing AX.25), using DTN-style endpoint addressing. This feature is a stepping stone toward full Bundle Protocol support over terrestrial amateur radio.

The implementation targets the existing Mobilinkd TNC3 + Yaesu FT-817 hardware at 1200 baud, running on Ubuntu and Raspberry Pi. It reuses the proven KISS encoding/decoding and serial port handling from the existing codebase, and leverages ping-derived RTT measurements for retransmission timer estimation.

## Glossary

- **LTP_Engine**: The core LTP protocol state machine that manages sessions, segments, timers, and acknowledgments
- **LTP_Segment**: A protocol data unit containing an LTP header and optional payload, transmitted inside a single KISS frame
- **LTP_Session**: A unidirectional data transfer context identified by a session originator engine ID and session number
- **LTP_Block**: A contiguous application data unit (e.g. a bundle or user message) that LTP segments for transmission
- **Checkpoint**: A data segment marked by the sender to request a reception report from the receiver
- **Reception_Report**: A segment sent by the receiver indicating which byte ranges of a block have been successfully received
- **Report_Acknowledgment**: A segment sent by the sender confirming receipt of a reception report
- **Engine_ID**: A numeric identifier for an LTP engine instance, derived from a DTN endpoint identifier (e.g. dtn://g4dpz-1 maps to engine ID 1)
- **KISS_Frame**: A FEND-delimited, byte-stuffed frame carrying an LTP segment as its payload (command byte 0x00)
- **Convergence_Layer**: The interface between LTP and the underlying link — in this system, KISS over serial
- **Red_Part**: The portion of an LTP block that requires reliable (acknowledged) delivery
- **Green_Part**: The portion of an LTP block that uses best-effort (unacknowledged) delivery
- **Retransmission_Timer**: A timer set by the sender after transmitting a checkpoint, based on estimated one-way light time; expiry triggers retransmission
- **Cancel_Segment**: A segment sent by either sender or receiver to abort an LTP session
- **OWLT**: One-Way Light Time — estimated one-way propagation delay, derived from RTT measurements
- **Segment_MTU**: Maximum LTP segment size that fits within a single KISS frame, accounting for KISS and LTP header overhead
- **CLI_Dispatcher**: The command-line interface module in main.c that parses arguments and dispatches to mode functions

## Requirements

### Requirement 1: LTP Segment Encoding and Decoding

**User Story:** As a developer, I want to encode and decode LTP segments into byte buffers, so that LTP protocol data units can be transmitted over the KISS link.

#### Acceptance Criteria

1. THE LTP_Engine SHALL encode LTP segments using the CCSDS LTP segment format: 1-byte version/flags, SDNV-encoded engine IDs, SDNV-encoded session number, SDNV-encoded extensions count, and segment-type-specific content fields
2. WHEN a byte buffer containing a valid LTP segment is provided, THE LTP_Engine SHALL decode the segment header and extract the segment type, session ID, engine IDs, and payload data
3. THE LTP_Engine SHALL support encoding and decoding of Self-Delimiting Numeric Values (SDNVs) for variable-length integer fields
4. WHEN an SDNV value exceeds 2^63 - 1, THE LTP_Engine SHALL reject the value and return an error code
5. THE LTP_Segment_Encoder SHALL format LTP segments into byte buffers suitable for direct encapsulation in KISS frames without AX.25 framing
6. THE LTP_Segment_Decoder SHALL parse LTP segments from byte buffers received as KISS frame payloads
7. FOR ALL valid LTP segments, encoding a segment to bytes and then decoding the bytes SHALL produce an equivalent segment structure (round-trip property)

### Requirement 2: LTP Segment Types

**User Story:** As a developer, I want the LTP engine to handle all required segment types, so that reliable and best-effort data transfer can be performed.

#### Acceptance Criteria

1. THE LTP_Engine SHALL support data segment types: red data (type 0), red data with checkpoint (type 1), red data with end-of-red-part and checkpoint (type 2), green data (type 3), and green data with end-of-block (type 4)
2. THE LTP_Engine SHALL support control segment types: reception report (type 8), report acknowledgment (type 9), cancel by sender (type 12), cancel acknowledgment to sender (type 13), cancel by receiver (type 14), and cancel acknowledgment to receiver (type 15)
3. WHEN a segment with an unrecognized type value is received, THE LTP_Engine SHALL discard the segment and log a warning

### Requirement 3: LTP Session Management

**User Story:** As a developer, I want the LTP engine to manage transmission and reception sessions, so that data blocks can be tracked and acknowledged reliably.

#### Acceptance Criteria

1. WHEN a new data block is submitted for transmission, THE LTP_Engine SHALL create a new export session with a unique session number and segment the block into data segments no larger than the configured Segment_MTU
2. WHEN the first data segment of an unknown session is received, THE LTP_Engine SHALL create a new import session to track received data
3. THE LTP_Engine SHALL maintain separate export session and import session tables, each keyed by the combination of session originator engine ID and session number
4. WHEN all data in a block has been acknowledged via reception reports, THE LTP_Engine SHALL close the export session and release associated resources
5. WHEN a complete block has been received and acknowledged, THE LTP_Engine SHALL deliver the reassembled block to the application layer and close the import session
6. THE LTP_Engine SHALL support a configurable maximum number of concurrent export sessions (default 128)
7. THE LTP_Engine SHALL support a configurable maximum number of concurrent import sessions (default 128)
8. WHEN the maximum number of concurrent export sessions is reached, THE LTP_Engine SHALL reject new block transmission requests and return an error code

### Requirement 4: Checkpoint and Report Acknowledgment

**User Story:** As a developer, I want the LTP engine to use checkpoint/report exchanges, so that the receiver can confirm which data has been received and the sender can retransmit missing segments.

#### Acceptance Criteria

1. WHEN transmitting the final segment of the red part of a block, THE LTP_Engine SHALL mark the segment as a checkpoint with the end-of-red-part flag and assign a unique checkpoint serial number
2. WHEN a checkpoint segment is received, THE LTP_Engine SHALL generate a reception report listing the contiguous byte ranges (claims) that have been successfully received within the red part
3. WHEN a reception report is received, THE LTP_Engine SHALL send a report acknowledgment segment to the receiver
4. WHEN a reception report indicates missing data, THE LTP_Engine SHALL retransmit only the missing byte ranges as new data segments, with the final retransmitted segment marked as a new checkpoint
5. WHEN a report acknowledgment is received, THE LTP_Engine SHALL stop retransmitting the corresponding reception report
6. THE LTP_Engine SHALL assign monotonically increasing checkpoint serial numbers within each export session
7. THE LTP_Engine SHALL assign monotonically increasing report serial numbers within each import session

### Requirement 5: Retransmission Timers

**User Story:** As a developer, I want the LTP engine to use retransmission timers based on estimated link delay, so that lost segments are retransmitted after an appropriate interval.

#### Acceptance Criteria

1. WHEN a checkpoint segment is transmitted, THE LTP_Engine SHALL start a retransmission timer with a duration of 2 × OWLT plus a configurable processing margin
2. WHEN a retransmission timer expires without receiving a corresponding reception report, THE LTP_Engine SHALL retransmit the checkpoint segment
3. THE LTP_Engine SHALL accept an initial OWLT estimate as a configuration parameter (default 1500 milliseconds, based on measured 1200 baud RF link RTT of approximately 2500 ms)
4. WHEN a reception report is received before the retransmission timer expires, THE LTP_Engine SHALL cancel the corresponding retransmission timer
5. THE LTP_Engine SHALL support a configurable maximum number of retransmission attempts per checkpoint (default 7)
6. WHEN the maximum number of retransmission attempts is exceeded, THE LTP_Engine SHALL cancel the session and notify the application layer
7. WHEN a reception report is transmitted, THE LTP_Engine SHALL start a retransmission timer for the report, with the same duration as the checkpoint timer

### Requirement 6: Block Segmentation and Reassembly

**User Story:** As a developer, I want the LTP engine to segment large data blocks and reassemble them on the receiving side, so that data larger than a single KISS frame can be transferred reliably.

#### Acceptance Criteria

1. THE LTP_Engine SHALL segment data blocks into LTP data segments with payload sizes no larger than the configured Segment_MTU (default 64 bytes, to fit within KISS frame constraints at 1200 baud)
2. WHEN data segments are received, THE LTP_Engine SHALL buffer received data and track which byte ranges have been received using offset and length fields from each segment
3. WHEN all byte ranges of a block's red part have been received, THE LTP_Engine SHALL reassemble the complete block in offset order and deliver the block to the application layer
4. THE LTP_Engine SHALL support blocks up to a configurable maximum size (default 1024 bytes)
5. IF a received segment's offset plus length exceeds the expected block size, THEN THE LTP_Engine SHALL discard the segment and log a warning

### Requirement 7: Session Cancellation

**User Story:** As a developer, I want either side to cancel an LTP session, so that resources can be freed when a transfer cannot complete.

#### Acceptance Criteria

1. WHEN the application requests cancellation of an export session, THE LTP_Engine SHALL transmit a cancel-by-sender segment and release session resources after receiving a cancel acknowledgment
2. WHEN a cancel-by-sender segment is received, THE LTP_Engine SHALL discard the import session data, transmit a cancel-acknowledgment-to-sender segment, and close the import session
3. WHEN the application requests cancellation of an import session, THE LTP_Engine SHALL transmit a cancel-by-receiver segment and release session resources after receiving a cancel acknowledgment
4. WHEN a cancel-by-receiver segment is received, THE LTP_Engine SHALL discard the export session data, transmit a cancel-acknowledgment-to-receiver segment, and close the export session
5. WHEN a cancel segment is transmitted, THE LTP_Engine SHALL start a retransmission timer and retransmit the cancel segment if no acknowledgment is received before timer expiry

### Requirement 8: DTN Endpoint Addressing

**User Story:** As a developer, I want to use DTN-style endpoint identifiers for addressing, so that the system follows DTN conventions and is compatible with future Bundle Protocol integration.

#### Acceptance Criteria

1. THE LTP_Engine SHALL accept endpoint identifiers in the format "dtn://callsign" (e.g. "dtn://g4dpz-1") for local and remote engine identification
2. WHEN a DTN endpoint identifier is provided, THE LTP_Engine SHALL derive a numeric Engine_ID by hashing or mapping the callsign portion to a unique integer value
3. THE LTP_Engine SHALL include the numeric Engine_ID in all transmitted LTP segment headers as the sender or receiver engine ID
4. THE LTP_Engine SHALL maintain a mapping table between DTN endpoint identifiers and numeric Engine_IDs
5. WHEN a segment is received with an Engine_ID not matching the local engine, THE LTP_Engine SHALL discard the segment and log a warning

### Requirement 9: KISS Convergence Layer Integration

**User Story:** As a developer, I want LTP segments to be transmitted directly inside KISS frames, so that the radio link carries LTP without AX.25 overhead.

#### Acceptance Criteria

1. THE Convergence_Layer SHALL encapsulate each LTP segment as the payload of a KISS data frame (command byte 0x00), using the existing KISS encoding with FEND delimiters and byte-stuffing
2. WHEN a complete KISS data frame is received, THE Convergence_Layer SHALL extract the payload and pass the raw bytes to the LTP_Segment_Decoder for processing
3. THE Convergence_Layer SHALL reuse the existing kiss_encode and kiss_decoder_feed functions from kiss.c without modification
4. THE Convergence_Layer SHALL reuse the existing serial_open, serial_close, and serial_configure_tnc functions from serial.c without modification
5. THE Convergence_Layer SHALL limit the maximum LTP segment size such that the KISS-encoded frame (including byte-stuffing overhead) does not exceed 512 bytes, to remain within TNC buffer limits at 1200 baud

### Requirement 10: CLI Integration — Send Mode

**User Story:** As a developer, I want an "ltp-send" command that transmits a data block reliably using LTP, so that I can send messages over the radio link with acknowledgment.

#### Acceptance Criteria

1. WHEN the "ltp-send" subcommand is invoked, THE CLI_Dispatcher SHALL accept required arguments: --device, --local (local DTN endpoint), --remote (remote DTN endpoint), and a payload string
2. WHEN the "ltp-send" subcommand is invoked, THE CLI_Dispatcher SHALL accept optional arguments: --mtu (segment MTU, default 64), --owlt (one-way light time in ms, default 1500), --retries (max retransmission attempts, default 7), --txdelay, --txtail, and --verbose
3. WHEN all required arguments are provided, THE CLI_Dispatcher SHALL initialize the LTP_Engine, submit the payload as a red-part block, and run the transmission session to completion
4. WHEN the block is fully acknowledged by the receiver, THE CLI_Dispatcher SHALL print a success message including the number of segments transmitted and total transfer time, then exit with code 0
5. IF the session is cancelled due to retransmission exhaustion, THEN THE CLI_Dispatcher SHALL print an error message to stderr and exit with code 1
6. WHEN --verbose is specified, THE CLI_Dispatcher SHALL print each transmitted and received segment with type, session ID, offset, and length

### Requirement 11: CLI Integration — Receive Mode

**User Story:** As a developer, I want an "ltp-recv" command that receives data blocks reliably using LTP, so that I can receive messages sent via ltp-send.

#### Acceptance Criteria

1. WHEN the "ltp-recv" subcommand is invoked, THE CLI_Dispatcher SHALL accept required arguments: --device and --local (local DTN endpoint)
2. WHEN the "ltp-recv" subcommand is invoked, THE CLI_Dispatcher SHALL accept optional arguments: --owlt (one-way light time in ms, default 1500), --txdelay, --txtail, and --verbose
3. WHEN a complete block is reassembled from received LTP segments, THE CLI_Dispatcher SHALL print the block contents to stdout with a timestamp and the remote engine's endpoint identifier
4. THE CLI_Dispatcher SHALL continue listening for new LTP sessions until interrupted by SIGINT or SIGTERM
5. WHEN interrupted, THE CLI_Dispatcher SHALL print a summary of sessions received and blocks delivered, then exit with code 0
6. WHEN --verbose is specified, THE CLI_Dispatcher SHALL print each received and transmitted segment with type, session ID, offset, and length

### Requirement 12: Event Loop and Timer Management

**User Story:** As a developer, I want the LTP engine to use a poll-based event loop with timer management, so that retransmission timers and serial I/O are handled in a single thread without blocking.

#### Acceptance Criteria

1. THE LTP_Engine SHALL use poll() on the serial file descriptor to multiplex between incoming data and timer expiry events
2. WHEN poll() returns with data available, THE LTP_Engine SHALL read bytes from the serial port, feed them to the KISS decoder, and process any complete LTP segments
3. WHEN poll() returns due to timeout, THE LTP_Engine SHALL check all active retransmission timers and fire any that have expired
4. THE LTP_Engine SHALL compute the poll() timeout as the minimum time until the next timer expiry, or -1 if no timers are active (for receive mode)
5. WHEN SIGINT or SIGTERM is received, THE LTP_Engine SHALL set the global running flag to 0 and exit the event loop cleanly

### Requirement 13: SDNV Encoding and Decoding

**User Story:** As a developer, I want to encode and decode Self-Delimiting Numeric Values, so that LTP header fields use the compact variable-length integer format specified by the LTP standard.

#### Acceptance Criteria

1. THE SDNV_Encoder SHALL encode non-negative integer values into the SDNV format where each byte uses 7 data bits and 1 continuation bit, with the most significant byte first
2. THE SDNV_Decoder SHALL decode SDNV-encoded bytes back to the original non-negative integer value
3. FOR ALL non-negative integer values in the range 0 to 2^63 - 1, encoding with the SDNV_Encoder and then decoding with the SDNV_Decoder SHALL produce the original value (round-trip property)
4. WHEN an SDNV encoding would require more than 10 bytes, THE SDNV_Encoder SHALL return an error code
5. WHEN an SDNV-encoded sequence in a buffer is incomplete (truncated), THE SDNV_Decoder SHALL return an error code indicating insufficient data

### Requirement 14: Backward Compatibility

**User Story:** As a developer, I want the existing send, receive, echo, and ping commands to continue working unchanged, so that the AX.25-based functionality is preserved alongside the new LTP modes.

#### Acceptance Criteria

1. THE CLI_Dispatcher SHALL continue to support the existing "send", "receive", "echo", and "ping" subcommands with identical behavior and argument parsing
2. THE LTP_Engine modules SHALL be compiled as separate compilation units (ltp.c, sdnv.c) that do not modify any existing source files (kiss.c, ax25.c, serial.c, ping.c)
3. WHEN the "ltp-send" or "ltp-recv" subcommand is invoked, THE CLI_Dispatcher SHALL use the KISS and serial modules directly, bypassing the AX.25 module entirely
