# Implementation Plan: KISS USB Interface

## Overview

Build a standalone C command-line tool (`kiss_interface`) that communicates with a Mobilinkd TNC3 over USB serial using the KISS protocol, wrapping payloads in AX.25 UI frames. The tool supports send, receive, and echo modes. Implementation is split into four compilation units (`kiss.c`, `ax25.c`, `serial.c`, `main.c`) with a Makefile build. All buffers are statically allocated; only POSIX APIs are used.

## Tasks

- [x] 1. Create project structure, headers, and Makefile
  - [x] 1.1 Create `kiss.h` with KISS constants, `kiss_encode`, `kiss_decoder_t`, `kiss_decoder_init`, `kiss_decoder_feed`, and `kiss_build_cmd` declarations as specified in the design
    - _Requirements: 2.1, 2.2, 2.3, 3.1, 3.2, 3.3, 3.4, 3.5_
  - [x] 1.2 Create `ax25.h` with AX.25 constants, `ax25_encode_addr`, `ax25_decode_addr`, `ax25_build_frame`, and `ax25_strip_frame` declarations as specified in the design
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6_
  - [x] 1.3 Create `serial.h` with `serial_open`, `serial_close`, `serial_configure_tnc`, and `serial_parse_device` declarations as specified in the design
    - _Requirements: 1.1, 1.2, 1.4, 1.5_
  - [x] 1.4 Create the `Makefile` with targets for `kiss_interface`, `test_kiss`, `test_ax25`, `test_serial`, `test` (run all), and `clean` as specified in the design
    - _Requirements: (build infrastructure)_

- [x] 2. Implement KISS encoding and decoding (`kiss.c`)
  - [x] 2.1 Implement `kiss_encode` — prepend FEND + 0x00 command byte, apply byte-stuffing (FEND→FESC+TFEND, FESC→FESC+TFESC), append trailing FEND
    - _Requirements: 2.1, 2.2, 2.3_
  - [x] 2.2 Implement `kiss_decoder_init` and `kiss_decoder_feed` — byte-at-a-time state machine (IDLE→IN_FRAME→ESCAPE→FRAME_COMPLETE), reverse byte-stuffing, discard non-data commands (cmd nibble != 0), discard frames exceeding KISS_MAX_PAYLOAD
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_
  - [x] 2.3 Implement `kiss_build_cmd` — build KISS command frames for TNC parameters (TX-delay cmd 0x01, TX-tail cmd 0x04)
    - _Requirements: 1.2_
  - [x]* 2.4 Write property test for KISS encode/decode round-trip (`test_kiss.c`)
    - **Property 1: KISS encode/decode round-trip**
    - For any arbitrary byte sequence of length 0–65535, encoding then decoding produces the original sequence
    - **Validates: Requirements 2.4, 9.1, 3.1**
  - [x]* 2.5 Write property test for KISS frame structure invariant (`test_kiss.c`)
    - **Property 4: KISS frame structure invariant**
    - Output of `kiss_encode` starts with FEND+0x00, ends with FEND, no unescaped FEND between delimiters
    - **Validates: Requirements 2.1, 2.2, 2.3**
  - [x]* 2.6 Write unit tests for KISS edge cases (`test_kiss.c`)
    - Test: decoder discards non-data command frames (Req 3.4)
    - Test: decoder discards oversized frames (Req 3.5)
    - Test: TNC parameter command frames have correct bytes (Req 1.2)
    - _Requirements: 3.4, 3.5, 1.2_

- [x] 3. Implement AX.25 framing (`ax25.c`)
  - [x] 3.1 Implement `ax25_encode_addr` — encode callsign into 7-byte address field: left-shift characters by 1, space-pad to 6 chars, encode SSID in bits 1–4 of byte 6, set reserved bits 5–6, set extension bit on last field
    - _Requirements: 4.1, 4.3, 4.4_
  - [x] 3.2 Implement `ax25_decode_addr` — reverse the encoding: right-shift characters, trim spaces, extract SSID, format as "CALL-SSID" string
    - _Requirements: 4.1, 4.3_
  - [x] 3.3 Implement `ax25_build_frame` — assemble destination addr + source addr + control (0x03) + PID (0xF0) + info field
    - _Requirements: 4.1, 4.2, 4.4_
  - [x] 3.4 Implement `ax25_strip_frame` — validate minimum 16-byte length, verify control=0x03 and PID=0xF0, decode source/destination callsigns, return pointer to info field
    - _Requirements: 4.5, 4.6_
  - [x]* 3.5 Write property test for AX.25 build/strip round-trip (`test_ax25.c`)
    - **Property 2: AX.25 build/strip round-trip**
    - For any valid callsigns and info payload, building then stripping produces original data
    - **Validates: Requirements 9.2, 4.1, 4.5**
  - [x]* 3.6 Write property test for callsign encode/decode round-trip (`test_ax25.c`)
    - **Property 3: Callsign encode/decode round-trip**
    - For any callsign of 1–6 uppercase alphanumeric chars with SSID 0–15, encoding then decoding produces the original
    - **Validates: Requirements 9.3, 4.3**
  - [x]* 3.7 Write property test for AX.25 frame structural invariants (`test_ax25.c`)
    - **Property 5: AX.25 frame structural invariants**
    - Byte 14 = 0x03, byte 15 = 0xF0, extension bits correct on dst (0) and src (1)
    - **Validates: Requirements 4.2, 4.4**
  - [x]* 3.8 Write unit test: AX.25 rejects frames < 16 bytes (`test_ax25.c`)
    - _Requirements: 4.6_

- [x] 4. Checkpoint — Ensure KISS and AX.25 modules compile and all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement serial port handling (`serial.c`)
  - [x] 5.1 Implement `serial_parse_device` — parse "device:baud" or "device" (default 9600), validate baud rate against supported set {1200, 9600, 19200, 38400, 57600, 115200}
    - _Requirements: 1.4, 1.5_
  - [x] 5.2 Implement `serial_open` — open device O_RDWR|O_NOCTTY, configure termios for raw 8N1 (no parity, 1 stop bit, no flow control), set baud rate via cfsetispeed/cfsetospeed, apply with tcsetattr
    - _Requirements: 1.1_
  - [x] 5.3 Implement `serial_close` — close file descriptor
    - _Requirements: 1.1_
  - [x] 5.4 Implement `serial_configure_tnc` — convert txdelay/txtail from milliseconds to 10ms units, build KISS command frames via `kiss_build_cmd`, write to serial fd
    - _Requirements: 1.2_
  - [x]* 5.5 Write property test for device string parse round-trip (`test_serial.c`)
    - **Property 7: Device string parse round-trip**
    - For any device path (no colon) and baud from {1200,9600,19200,38400,57600,115200}, "device:baud" parses correctly; device alone yields 9600
    - **Validates: Requirements 1.5, 1.4**
  - [x]* 5.6 Write unit tests for serial module (`test_serial.c`)
    - Test: parse "device:baud" extracts both parts correctly
    - Test: parse "device" alone defaults to 9600
    - Test: invalid baud defaults to 9600 with warning
    - _Requirements: 1.4, 1.5_

- [x] 6. Implement CLI parsing and mode dispatch (`main.c`)
  - [x] 6.1 Define `cli_args_t` struct and implement CLI argument parsing — support `send`, `receive`, `echo` subcommands with `--device`, `--src`, `--dst`, `--verbose`, `--txdelay`, `--txtail`, `--delay`, `--help` options
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_
  - [x] 6.2 Implement argument validation — check required args per mode (device always required; src/dst required for send/echo; payload required for send), print specific error for missing args, exit non-zero
    - _Requirements: 8.6, 5.3_
  - [x] 6.3 Install signal handler for SIGINT/SIGTERM using `sigaction()` — set `volatile sig_atomic_t g_running = 0`, clear SA_RESTART so blocking reads return EINTR
    - _Requirements: 6.3, 7.4_

- [x] 7. Implement send, receive, and echo mode functions (`main.c`)
  - [x] 7.1 Implement `cmd_send` — build AX.25 frame, KISS-encode, write to serial, tcdrain, check payload size limit
    - _Requirements: 5.1, 5.2, 5.4_
  - [x] 7.2 Implement `cmd_receive` — blocking read loop checking `g_running`, feed bytes to KISS decoder, strip AX.25 headers, print timestamp + source + destination + payload; hex dump in verbose mode
    - _Requirements: 6.1, 6.2, 6.3, 6.4_
  - [x] 7.3 Implement `cmd_echo` — receive loop like `cmd_receive`, but on each decoded packet swap source/destination callsigns, rebuild AX.25 frame, KISS-encode, sleep configurable delay, retransmit; log echo info to stdout
    - _Requirements: 7.1, 7.2, 7.3, 7.4_
  - [x]* 7.4 Write property test for echo callsign swap (`test_ax25.c` or separate test file)
    - **Property 6: Echo mode preserves payload and swaps callsigns**
    - For any valid AX.25 frame with src S, dst D, payload P: strip → swap → rebuild produces src=D, dst=S, payload=P
    - **Validates: Requirements 7.1**

- [x] 8. Wire main() together and finalize
  - [x] 8.1 Implement `main()` — parse CLI args, open serial port, configure TNC, dispatch to cmd_send/cmd_receive/cmd_echo, close serial port on exit, handle all error paths with descriptive stderr messages
    - _Requirements: 1.1, 1.2, 1.3, 8.1, 8.5_
  - [x]* 8.2 Write unit tests for CLI parsing (`test_serial.c` or separate `test_cli.c`)
    - Test: parses send/receive/echo subcommands (Req 8.1)
    - Test: parses --device, --src, --dst, --verbose (Req 8.2)
    - Test: parses --txdelay, --txtail (Req 8.3)
    - Test: parses echo --delay (Req 8.4)
    - Test: --help prints usage and exits 0 (Req 8.5)
    - Test: missing required arg prints error and exits 1 (Req 8.6)
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_

- [x] 9. Final checkpoint — Ensure all tests pass and tool builds cleanly
  - Run `make clean && make && make test`
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Property tests use the `theft` library if available, or a hand-rolled random loop with `rand()`/`srand()` as fallback
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Integration testing with actual TNC3/FT-817 hardware is manual and not included in automated tasks
