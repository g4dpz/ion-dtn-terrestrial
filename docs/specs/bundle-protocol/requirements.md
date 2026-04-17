# Requirements Document

## Introduction

This feature adds a minimal Bundle Protocol version 7 (BPv7, RFC 9171) implementation to the existing `kiss_interface` tool, enabling DTN bundle exchange over the proven LTP-over-KISS stack. The implementation is point-to-point only (no routing, no custody transfer, no fragmentation beyond what LTP provides), targeting short messages and sensor data over the constrained 1200 baud VHF amateur radio link.

Bundles are CBOR-encoded (RFC 8949) and submitted to the existing LTP engine as the client service payload. The LTP engine handles reliable delivery, segmentation, and retransmission. The bundle layer adds DTN endpoint addressing, creation timestamps, bundle lifetime/expiry, and the standard BPv7 wire format for interoperability with other DTN implementations.

## Glossary

- **Bundle**: A BPv7 protocol data unit consisting of a primary block and one or more extension blocks (at minimum a payload block), CBOR-encoded as an indefinite-length array.
- **Primary_Block**: The first block in a bundle, containing source and destination endpoint IDs, creation timestamp, lifetime, and bundle processing flags.
- **Payload_Block**: A canonical block (block type 1) containing the application data.
- **EID**: Endpoint Identifier in `dtn://` URI scheme (e.g. `dtn://g4dpz-1/msg`), used for bundle addressing.
- **Creation_Timestamp**: A pair of (DTN time in milliseconds since 2000-01-01, sequence number) that uniquely identifies a bundle from a given source.
- **Bundle_Lifetime**: The time in milliseconds after creation after which a bundle is no longer useful and may be discarded.
- **CBOR**: Concise Binary Object Representation (RFC 8949), the encoding format for BPv7 bundles.
- **CRC**: Cyclic Redundancy Check — BPv7 supports CRC-16 and CRC-32 on blocks for integrity verification.
- **Bundle_Agent**: The module (`bp.h`/`bp.c`) that creates, encodes, decodes, and dispatches bundles.
- **LTP_Engine**: The existing Licklider Transmission Protocol engine that provides reliable delivery of bundles as LTP client service data.

## Requirements

### Requirement 1: CBOR Encoding and Decoding

**User Story:** As a developer, I want to encode and decode CBOR data items, so that bundles can be serialized in the standard BPv7 wire format.

#### Acceptance Criteria

1. THE Bundle_Agent SHALL encode CBOR unsigned integers (major type 0) in the shortest possible form (1, 2, 3, 5, or 9 bytes depending on value).
2. THE Bundle_Agent SHALL encode CBOR byte strings (major type 2) with a length prefix followed by raw bytes.
3. THE Bundle_Agent SHALL encode CBOR text strings (major type 3) with a length prefix followed by UTF-8 bytes.
4. THE Bundle_Agent SHALL encode CBOR arrays (major type 4) with a definite length prefix.
5. THE Bundle_Agent SHALL decode CBOR unsigned integers, byte strings, text strings, and arrays from a byte buffer, returning the number of bytes consumed.
6. FOR ALL supported CBOR data items, encoding then decoding SHALL produce the original value (round-trip property).
7. WHEN a CBOR item in the buffer is truncated or malformed, THE decoder SHALL return an error code without crashing.

### Requirement 2: Bundle Primary Block

**User Story:** As a developer, I want to construct and parse BPv7 primary blocks, so that bundles carry the required addressing and lifetime metadata.

#### Acceptance Criteria

1. THE Bundle_Agent SHALL encode the primary block as a CBOR array containing: version (7), bundle processing control flags, CRC type, destination EID, source EID, report-to EID, creation timestamp (array of [time, sequence]), and lifetime in milliseconds.
2. THE Bundle_Agent SHALL use BPv7 version number 7 in the primary block.
3. THE Bundle_Agent SHALL encode endpoint identifiers using the `dtn` scheme (scheme code 1) with the SSP as a CBOR text string (e.g. `//g4dpz-1/msg`).
4. THE Bundle_Agent SHALL set the creation timestamp time to the current DTN time (milliseconds since 2000-01-01 00:00:00 UTC).
5. THE Bundle_Agent SHALL accept a configurable bundle lifetime in milliseconds (default 3600000, i.e. 1 hour).
6. THE Bundle_Agent SHALL support CRC-16-CCITT on the primary block for integrity verification.
7. WHEN decoding a primary block, THE Bundle_Agent SHALL verify the version is 7 and the CRC (if present) is correct, returning an error if either check fails.

### Requirement 3: Payload Block

**User Story:** As a developer, I want to construct and parse BPv7 payload blocks, so that application data can be carried in bundles.

#### Acceptance Criteria

1. THE Bundle_Agent SHALL encode the payload block as a CBOR array containing: block type (1), block number, block processing control flags, CRC type, and the block-type-specific data (the payload as a CBOR byte string).
2. THE Bundle_Agent SHALL assign block number 1 to the payload block (block number 0 is reserved for the primary block per BPv7).
3. THE Bundle_Agent SHALL support payloads up to 65535 bytes (fragmented across multiple LTP blocks if needed).
4. WHEN decoding a payload block, THE Bundle_Agent SHALL extract the payload byte string and verify the CRC if present.

### Requirement 4: Bundle Encoding and Decoding

**User Story:** As a developer, I want to encode complete bundles (primary block + payload block) into a byte buffer and decode them back, so that bundles can be submitted to LTP for transmission.

#### Acceptance Criteria

1. THE Bundle_Agent SHALL encode a complete bundle as a CBOR indefinite-length array containing the primary block followed by the payload block, terminated by a CBOR break code (0xFF).
2. THE Bundle_Agent SHALL decode a complete bundle from a byte buffer, extracting the primary block and payload block.
3. WHEN the encoded bundle fits within 1024 bytes (the LTP max block size), THE Bundle_Agent SHALL submit it as a single LTP block without fragmentation.
4. WHEN the encoded bundle exceeds 1024 bytes, THE Bundle_Agent SHALL fragment the bundle per BPv7 fragmentation rules before submitting each fragment as a separate LTP block.
5. FOR ALL valid bundles, encoding then decoding SHALL produce an equivalent bundle with identical source EID, destination EID, creation timestamp, lifetime, and payload data (round-trip property).

### Requirement 10: Bundle Fragmentation

**User Story:** As a developer, I want the bundle agent to fragment large bundles into smaller fragment bundles, so that payloads larger than the LTP max block size can be transmitted reliably.

#### Acceptance Criteria

1. WHEN a bundle's encoded size exceeds the configured fragment size (default 900 bytes, leaving room for fragment primary block overhead within the 1024-byte LTP max block), THE Bundle_Agent SHALL split the payload into fragments and create a separate fragment bundle for each.
2. EACH fragment bundle SHALL contain a primary block with the "bundle is a fragment" flag set (bit 0 of the bundle processing control flags), the fragment offset field, and the total application data unit length field, per BPv7 Section 4.3.1.
3. EACH fragment bundle SHALL contain a payload block carrying the fragment's portion of the original payload data.
4. THE Bundle_Agent SHALL assign the same source EID, destination EID, creation timestamp, and lifetime to all fragment bundles from the same original bundle, so that the receiver can identify and reassemble them.
5. THE Bundle_Agent SHALL transmit fragment bundles sequentially, each as a separate LTP block, waiting for each LTP session to complete before sending the next fragment.

### Requirement 11: Bundle Reassembly

**User Story:** As a developer, I want the bundle agent to reassemble fragment bundles into the original complete bundle, so that the receiver can recover the full payload.

#### Acceptance Criteria

1. WHEN a received bundle has the "bundle is a fragment" flag set, THE Bundle_Agent SHALL buffer the fragment and track which byte ranges of the original payload have been received, using the fragment offset and payload length.
2. WHEN all fragments of a bundle have been received (the union of fragment ranges covers [0, total ADU length)), THE Bundle_Agent SHALL reassemble the complete payload in offset order and deliver the bundle to the application.
3. THE Bundle_Agent SHALL identify fragments belonging to the same original bundle by matching source EID, creation timestamp (time and sequence number), and total ADU length.
4. THE Bundle_Agent SHALL support reassembly of bundles with up to 64 fragments.
5. IF a fragment is received for a bundle whose lifetime has expired, THEN THE Bundle_Agent SHALL discard the fragment and log a warning.
6. FOR ALL payloads of size 1 to 65535 bytes, fragmenting and then reassembling SHALL produce the original payload (round-trip property).

### Requirement 5: CLI Integration — Bundle Send

**User Story:** As a radio operator, I want a `bp-send` command that sends a bundle over the air using BPv7 over LTP, so that I can exchange DTN messages with another station.

#### Acceptance Criteria

1. WHEN the `bp-send` subcommand is invoked, THE CLI SHALL accept required arguments: `--device`, `--local` (source EID), `--remote` (destination EID), and either a payload string as a positional argument OR `--file` specifying a file path to read as the payload.
2. WHEN the `bp-send` subcommand is invoked, THE CLI SHALL accept optional arguments: `--lifetime` (bundle lifetime in seconds, default 3600), `--mtu`, `--owlt`, `--retries`, `--txdelay`, `--txtail`, `--beacon` (with beacon options), `--file` (read payload from file), and `--verbose`.
3. WHEN `--file` is specified, THE CLI SHALL read the entire file contents into memory and use them as the bundle payload. IF the file cannot be opened or read, THEN THE CLI SHALL print an error to stderr and exit with code 1.
4. WHEN both a positional payload string and `--file` are specified, THE CLI SHALL use `--file` and ignore the positional argument.
5. WHEN all required arguments are provided, THE CLI SHALL create a bundle with the specified source and destination EIDs and payload, encode it in BPv7 CBOR format, fragment if necessary, submit each fragment to the LTP engine as a block, and run the LTP event loop until delivery completes.
6. WHEN the bundle is delivered, THE CLI SHALL print a success message with the bundle size, number of fragments (if fragmented), and transfer time, then exit with code 0.
7. IF the LTP session is cancelled, THEN THE CLI SHALL print an error and exit with code 1.

### Requirement 6: CLI Integration — Bundle Receive

**User Story:** As a radio operator, I want a `bp-recv` command that receives bundles over the air and displays their contents, so that I can receive DTN messages from another station.

#### Acceptance Criteria

1. WHEN the `bp-recv` subcommand is invoked, THE CLI SHALL accept required arguments: `--device` and `--local` (local EID).
2. WHEN the `bp-recv` subcommand is invoked, THE CLI SHALL accept optional arguments: `--owlt`, `--mtu`, `--retries`, `--txdelay`, `--txtail`, `--beacon` (with beacon options), `--outdir` (directory to write received files, default stdout), and `--verbose`.
3. WHEN a complete bundle is received via LTP block delivery, THE CLI SHALL decode the BPv7 bundle, verify the primary block, and print the source EID, destination EID, creation timestamp, and payload data to stdout.
4. WHEN `--outdir` is specified, THE CLI SHALL write each received bundle's payload to a file in the specified directory, named `<timestamp>_<source_eid>.bin`, and print the filename to stdout instead of the payload data.
5. THE CLI SHALL continue listening for bundles until interrupted by SIGINT or SIGTERM.
5. WHEN interrupted, THE CLI SHALL print a summary of bundles received and exit with code 0.
6. WHEN a received bundle has an expired lifetime (creation time + lifetime < current time), THE CLI SHALL discard the bundle and log a warning.
7. WHEN verbose mode is enabled, THE CLI SHALL print the full CBOR hex dump of each received bundle.

### Requirement 7: DTN Time

**User Story:** As a developer, I want to convert between Unix time and DTN time, so that bundle creation timestamps are correct per the BPv7 specification.

#### Acceptance Criteria

1. THE Bundle_Agent SHALL define DTN epoch as 2000-01-01 00:00:00 UTC (Unix timestamp 946684800).
2. THE Bundle_Agent SHALL convert current system time to DTN time in milliseconds by subtracting the DTN epoch from the Unix time and multiplying by 1000.
3. THE Bundle_Agent SHALL convert DTN time back to Unix time for display purposes.
4. FOR ALL Unix timestamps after 2000-01-01, converting to DTN time and back SHALL produce the original Unix timestamp (round-trip property).

### Requirement 8: Backward Compatibility

**User Story:** As a developer, I want all existing commands to continue working unchanged after adding bundle protocol support.

#### Acceptance Criteria

1. THE CLI SHALL continue to support all existing subcommands (`send`, `receive`, `echo`, `ping`, `ltp-send`, `ltp-recv`, `beacon`) with identical behavior.
2. THE Bundle_Agent SHALL be implemented as new compilation units (`cbor.h`/`cbor.c`, `bp.h`/`bp.c`) that do not modify any existing source files except `main.c` (for CLI integration) and `Makefile`.
3. THE `bp-send` and `bp-recv` subcommands SHALL reuse the existing LTP engine, KISS encoding, serial port handling, beacon, and APRS decode modules without modification.

### Requirement 9: CBOR Round-Trip Integrity

**User Story:** As a developer, I want to verify that CBOR encoding and decoding are exact inverses, so that bundle data is not corrupted during serialization.

#### Acceptance Criteria

1. FOR ALL unsigned integer values in the range 0 to 2^64-1, CBOR encoding then decoding SHALL produce the original value.
2. FOR ALL byte strings of length 0 to 900, CBOR encoding then decoding SHALL produce the original byte string.
3. FOR ALL text strings of length 0 to 256, CBOR encoding then decoding SHALL produce the original text string.
4. FOR ALL valid BPv7 bundles (with valid EIDs, timestamps, and payloads up to 900 bytes), encoding then decoding SHALL produce an equivalent bundle.
