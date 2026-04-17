# Requirements Document

## Introduction

This feature adds APRS packet decoding and logging to the existing `kiss_interface` tool. When operating on a shared amateur radio frequency, the tool may receive AX.25 packets from other stations (APRS beacons, position reports, messages). Rather than silently discarding these packets, the tool decodes and logs them, providing situational awareness of nearby stations. The decoder integrates into the existing receive, LTP, and beacon event loops, distinguishing between AX.25/APRS packets and LTP segments based on the frame structure.

## Glossary

- **APRS_Decoder**: The new `aprs.h`/`aprs.c` compilation unit responsible for parsing APRS position reports and other APRS data types from AX.25 UI frame information fields.
- **APRS_Position**: A decoded APRS position containing latitude, longitude, symbol, and optional comment text.
- **Frame_Classifier**: Logic that examines a received KISS payload and determines whether it is an AX.25 UI frame (potential APRS) or an LTP segment, based on the frame structure.
- **Station_Log**: A timestamped log entry printed to stdout when an APRS packet from another station is received, showing the source callsign, destination, and decoded content.

## Requirements

### Requirement 1: Frame Classification

**User Story:** As a radio operator, I want the tool to distinguish between AX.25 packets and LTP segments received on the same frequency, so that each is processed correctly.

#### Acceptance Criteria

1. WHEN a complete KISS frame payload is received, THE Frame_Classifier SHALL attempt to parse it as an AX.25 UI frame by checking for minimum length (16 bytes), control byte 0x03, and PID byte 0xF0.
2. WHEN the payload matches AX.25 UI frame structure, THE Frame_Classifier SHALL pass it to the APRS_Decoder for logging and NOT pass it to the LTP engine.
3. WHEN the payload does NOT match AX.25 UI frame structure, THE Frame_Classifier SHALL pass it to the LTP engine for processing as an LTP segment.
4. THE Frame_Classifier SHALL be integrated into the LTP receive and send event loops (both beacon-enabled and non-beacon paths) and the standalone receive mode.

### Requirement 2: APRS Position Decoding

**User Story:** As a radio operator, I want received APRS position packets to be decoded and displayed, so that I can see nearby stations on the frequency.

#### Acceptance Criteria

1. WHEN an AX.25 UI frame with an information field starting with `!` (position without timestamp) is received, THE APRS_Decoder SHALL parse the uncompressed position format (DDMM.MMN/DDDMM.MMW) and extract latitude, longitude, symbol, and comment.
2. WHEN an AX.25 UI frame with an information field starting with `=` (position without timestamp, with messaging) is received, THE APRS_Decoder SHALL parse it using the same uncompressed position format.
3. WHEN an AX.25 UI frame with an information field starting with `/` or `@` (position with timestamp) is received, THE APRS_Decoder SHALL extract the timestamp and parse the position that follows.
4. WHEN an AX.25 UI frame contains an APRS data type that the decoder does not support, THE APRS_Decoder SHALL log the raw information field as a hex dump (in verbose mode) or as a printable string.
5. THE APRS_Decoder SHALL handle information fields that are too short or malformed by logging a warning and continuing without crashing.

### Requirement 3: Station Logging

**User Story:** As a radio operator, I want a clear log of received APRS stations, so that I can monitor activity on the frequency.

#### Acceptance Criteria

1. WHEN a valid APRS packet is received, THE Station_Log SHALL print a line to stdout containing the timestamp, source callsign, destination callsign, and decoded content (position or raw data).
2. WHEN a position is successfully decoded, THE Station_Log SHALL display the latitude and longitude in decimal degrees with 4 decimal places, followed by the comment text.
3. WHEN verbose mode is enabled, THE Station_Log SHALL additionally print a hex dump of the raw AX.25 frame.
4. THE Station_Log SHALL flush stdout after each log entry to ensure real-time display.

### Requirement 4: Integration with Existing Modes

**User Story:** As a radio operator, I want APRS decoding to work in all receive-capable modes, so that I always see nearby stations regardless of which mode I'm running.

#### Acceptance Criteria

1. WHEN operating in `receive` mode, THE tool SHALL decode and log all received AX.25 UI frames as APRS packets (this is the existing behaviour, enhanced with APRS position parsing).
2. WHEN operating in `ltp-recv` mode (with or without `--beacon`), THE Frame_Classifier SHALL decode AX.25 packets as APRS and pass non-AX.25 payloads to the LTP engine.
3. WHEN operating in `ltp-send` mode with `--beacon`, THE Frame_Classifier SHALL decode AX.25 packets as APRS and pass non-AX.25 payloads to the LTP engine.
4. WHEN operating in `echo` mode, THE tool SHALL continue to echo all received packets (no change to echo behaviour) but additionally log decoded APRS content when verbose mode is enabled.
5. THE APRS decoding SHALL NOT interfere with LTP segment processing — LTP segments must still be delivered to the LTP engine without modification.

### Requirement 5: APRS Position Format Parsing

**User Story:** As a developer, I want the APRS position parser to correctly handle the standard uncompressed format, so that positions from other stations are accurately displayed.

#### Acceptance Criteria

1. THE APRS_Decoder SHALL parse latitude in DDMM.MM format with N/S hemisphere indicator and convert to decimal degrees.
2. THE APRS_Decoder SHALL parse longitude in DDDMM.MM format with E/W hemisphere indicator and convert to decimal degrees.
3. THE APRS_Decoder SHALL extract the symbol table character and symbol code from their positions in the APRS string.
4. THE APRS_Decoder SHALL extract the comment text following the symbol code to the end of the information field.
5. FOR ALL valid APRS uncompressed position strings, THE APRS_Decoder parsing the output of `beacon_build_position` SHALL recover coordinates within 0.02 arcminutes of the original input (round-trip with the beacon encoder).

### Requirement 6: Implementation Constraints

**User Story:** As a developer, I want the APRS decoder to follow the same implementation patterns as the existing codebase.

#### Acceptance Criteria

1. THE APRS_Decoder SHALL be implemented as a new compilation unit (`aprs.h`/`aprs.c`) with no external dependencies beyond standard C and POSIX.
2. THE APRS_Decoder SHALL use static allocation only, consistent with the existing codebase.
3. THE APRS_Decoder SHALL reuse the existing `ax25_strip_frame` function for AX.25 header parsing.
4. THE APRS_Decoder SHALL compile and run correctly on both x86_64 (Ubuntu) and ARM (Raspberry Pi) targets.
5. THE existing `ax25.c`, `kiss.c`, `ltp.c`, `beacon.c`, `serial.c`, `ping.c`, and `sdnv.c` modules SHALL NOT be modified.
