# Requirements Document

## Introduction

A standalone command-line tool that connects to a Mobilinkd TNC3 via USB serial, uses the KISS protocol to frame AX.25 UI packets, and sends/receives packets over the air via an attached Yaesu FT-817 radio. The tool provides an echo mode that receives a packet and retransmits it, useful for link testing and diagnostics. Unlike the existing `ionserialcla` (which is tightly coupled to ION-DTN's LTP engine), this tool operates independently with no ION dependencies.

## Glossary

- **KISS_Interface**: The main command-line tool that manages USB serial communication with the TNC using the KISS protocol
- **KISS_Encoder**: The component that wraps raw data into KISS frames using FEND/FESC byte-stuffing
- **KISS_Decoder**: The component that extracts data payloads from received KISS frames, reversing byte-stuffing
- **AX25_Framer**: The component that builds and parses AX.25 UI (Unnumbered Information) frames with source/destination callsign headers
- **Serial_Port**: The USB serial device file (e.g. /dev/ttyUSB0 or /dev/ttyACM0) representing the Mobilinkd TNC3
- **TNC3**: The Mobilinkd TNC3 hardware, a USB KISS TNC that modulates/demodulates 1200 baud AFSK audio
- **FT-817**: The Yaesu FT-817 transceiver connected to the TNC3 via audio cable
- **Echo_Mode**: An operating mode where every received packet is retransmitted after a configurable delay
- **FEND**: Frame End byte (0xC0) used as KISS frame delimiter
- **FESC**: Frame Escape byte (0xDB) used in KISS byte-stuffing
- **Callsign**: An amateur radio callsign with optional SSID suffix (e.g. G4DPZ-1), used in AX.25 addressing

## Requirements

### Requirement 1: Serial Port Connection

**User Story:** As a radio operator, I want to connect to my Mobilinkd TNC3 over USB, so that I can send and receive KISS frames through the radio.

#### Acceptance Criteria

1. WHEN a device path and baud rate are provided as command-line arguments, THE KISS_Interface SHALL open the Serial_Port in raw mode with 8N1 configuration and no hardware flow control
2. WHEN the Serial_Port is opened successfully, THE KISS_Interface SHALL configure KISS TNC parameters by sending TX-delay and TX-tail command frames to the TNC3
3. IF the specified Serial_Port device does not exist or cannot be opened, THEN THE KISS_Interface SHALL print a descriptive error message to stderr and exit with a non-zero status code
4. WHEN no baud rate is specified, THE KISS_Interface SHALL default to 9600 baud
5. THE KISS_Interface SHALL accept the device path in the format `<device>:<baud>` or `<device>` (defaulting to 9600 baud)

### Requirement 2: KISS Frame Encoding

**User Story:** As a radio operator, I want outgoing data to be properly KISS-encoded, so that the TNC3 can interpret and transmit the frames correctly.

#### Acceptance Criteria

1. WHEN encoding a data payload into a KISS frame, THE KISS_Encoder SHALL prepend a FEND byte and a command byte of 0x00 (data on port 0), append a trailing FEND byte, and apply byte-stuffing to the payload
2. WHEN the payload contains a FEND byte (0xC0), THE KISS_Encoder SHALL replace that byte with the two-byte sequence FESC (0xDB) followed by TFEND (0xDC)
3. WHEN the payload contains a FESC byte (0xDB), THE KISS_Encoder SHALL replace that byte with the two-byte sequence FESC (0xDB) followed by TFESC (0xDD)
4. FOR ALL byte sequences, encoding then decoding a payload SHALL produce the original payload (round-trip property)

### Requirement 3: KISS Frame Decoding

**User Story:** As a radio operator, I want incoming KISS frames to be correctly decoded, so that I can read the data received over the air.

#### Acceptance Criteria

1. WHEN a complete KISS frame delimited by FEND bytes is received from the Serial_Port, THE KISS_Decoder SHALL extract the data payload by removing the command byte and reversing byte-stuffing
2. WHEN the KISS_Decoder encounters a FESC-TFEND (0xDB 0xDC) sequence, THE KISS_Decoder SHALL decode the sequence as a single FEND byte (0xC0)
3. WHEN the KISS_Decoder encounters a FESC-TFESC (0xDB 0xDD) sequence, THE KISS_Decoder SHALL decode the sequence as a single FESC byte (0xDB)
4. WHEN a received KISS frame has a non-zero command nibble (not a data frame), THE KISS_Decoder SHALL discard the frame and log a warning
5. IF a KISS frame exceeds the maximum buffer size of 65535 bytes, THEN THE KISS_Decoder SHALL discard the frame and log an error

### Requirement 4: AX.25 UI Frame Construction

**User Story:** As a radio operator, I want packets wrapped in AX.25 UI frames with proper callsign addressing, so that the transmissions comply with amateur radio protocol requirements.

#### Acceptance Criteria

1. WHEN building an AX.25 frame, THE AX25_Framer SHALL encode the destination and source Callsigns into 7-byte address fields with characters left-shifted by one bit and padded with spaces
2. THE AX25_Framer SHALL set the control byte to 0x03 (UI frame) and the PID byte to 0xF0 (no layer 3 protocol)
3. WHEN a Callsign includes an SSID suffix (e.g. "G4DPZ-1"), THE AX25_Framer SHALL encode the SSID value (0-15) into bits 1-4 of the seventh address byte
4. THE AX25_Framer SHALL set the address-extension bit (bit 0 of the seventh byte) to 1 on the last address field only
5. WHEN stripping an AX.25 frame, THE AX25_Framer SHALL verify the control byte is 0x03 and PID byte is 0xF0 before extracting the information field
6. IF the received frame has fewer than 16 bytes (minimum AX.25 UI header size), THEN THE AX25_Framer SHALL reject the frame and return an error

### Requirement 5: Packet Transmission

**User Story:** As a radio operator, I want to send a text payload over the air, so that I can test the radio link with another station.

#### Acceptance Criteria

1. WHEN the KISS_Interface is started in send mode with a payload argument, THE KISS_Interface SHALL wrap the payload in an AX.25 UI frame using the configured source and destination Callsigns, KISS-encode the frame, and write the result to the Serial_Port
2. WHEN a packet is transmitted, THE KISS_Interface SHALL wait for the serial write to drain (using tcdrain) before exiting, to ensure the TNC3 has received the complete frame
3. THE KISS_Interface SHALL accept source and destination Callsigns as command-line arguments
4. WHEN the payload exceeds the maximum AX.25 information field size, THE KISS_Interface SHALL report an error and refuse to send

### Requirement 6: Packet Reception

**User Story:** As a radio operator, I want to receive and display packets from the air, so that I can verify the radio link is working.

#### Acceptance Criteria

1. WHEN the KISS_Interface is started in receive mode, THE KISS_Interface SHALL continuously read from the Serial_Port, decode KISS frames, strip AX.25 headers, and print the source Callsign and payload to stdout
2. WHEN a valid packet is received, THE KISS_Interface SHALL display the timestamp, source Callsign, destination Callsign, and payload data
3. WHEN the user sends SIGINT (Ctrl-C) or SIGTERM, THE KISS_Interface SHALL stop receiving, close the Serial_Port, and exit cleanly with status code 0
4. WHILE in receive mode, THE KISS_Interface SHALL print a hex dump of each received frame when the verbose flag is enabled

### Requirement 7: Echo Mode

**User Story:** As a radio operator, I want the tool to automatically retransmit received packets, so that I can test round-trip radio links without a second operator.

#### Acceptance Criteria

1. WHEN the KISS_Interface is started in echo mode, THE KISS_Interface SHALL receive packets, swap the source and destination Callsigns in the AX.25 header, and retransmit each packet
2. WHEN retransmitting an echoed packet, THE KISS_Interface SHALL wait a configurable delay (default 1000 milliseconds) before transmitting, to allow the remote station time to switch from transmit to receive
3. WHEN a packet is echoed, THE KISS_Interface SHALL log the original source Callsign, payload size, and echo timestamp to stdout
4. WHILE in echo mode, THE KISS_Interface SHALL continue operating until a SIGINT or SIGTERM signal is received

### Requirement 8: Command-Line Interface

**User Story:** As a radio operator, I want a clear command-line interface, so that I can easily select operating modes and configure parameters.

#### Acceptance Criteria

1. THE KISS_Interface SHALL support the following subcommands: `send`, `receive`, and `echo`
2. THE KISS_Interface SHALL accept the following common options: `--device` (Serial_Port path with optional baud rate), `--src` (source Callsign), `--dst` (destination Callsign), and `--verbose` (enable debug output)
3. THE KISS_Interface SHALL accept `--txdelay` (TX-delay in milliseconds, default 500) and `--txtail` (TX-tail in milliseconds, default 300) options for TNC configuration
4. WHEN the `echo` subcommand is used, THE KISS_Interface SHALL accept a `--delay` option to set the echo retransmit delay in milliseconds
5. WHEN invoked with no subcommand or with `--help`, THE KISS_Interface SHALL print usage information and exit with status code 0
6. IF a required argument (device path, callsigns for send/echo) is missing, THEN THE KISS_Interface SHALL print a specific error message identifying the missing argument and exit with a non-zero status code

### Requirement 9: KISS Encode/Decode Round-Trip Integrity

**User Story:** As a developer, I want to verify that encoding and decoding are exact inverses, so that no data is corrupted during KISS framing.

#### Acceptance Criteria

1. FOR ALL arbitrary byte sequences up to the maximum frame size, THE KISS_Decoder decoding the output of THE KISS_Encoder SHALL produce the original byte sequence
2. FOR ALL valid AX.25 UI frames, THE AX25_Framer stripping a frame built by THE AX25_Framer SHALL produce the original information field payload
3. FOR ALL Callsigns with valid characters (A-Z, 0-9) and SSID values (0-15), THE AX25_Framer encoding then decoding a Callsign SHALL produce the original Callsign and SSID
