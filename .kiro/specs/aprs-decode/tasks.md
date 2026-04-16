# Implementation Plan: APRS Decode

## Overview

Add APRS packet decoding and frame classification to the `kiss_interface` tool. A new `aprs.h`/`aprs.c` module handles frame classification and APRS position parsing. Integration into `main.c` event loops routes AX.25 frames to the APRS decoder and non-AX.25 payloads to the LTP engine.

## Tasks

- [-] 1. Create APRS decoder module
  - [x] 1.1 Create `kiss-interface/aprs.h` with the interface from the design
    - Define `aprs_position_t` struct, `aprs_is_ax25_frame`, `aprs_decode_position`, `aprs_log_packet`
    - _Requirements: 1.1, 2.1, 3.1, 6.1_

  - [x] 1.2 Implement `aprs_is_ax25_frame` in `kiss-interface/aprs.c`
    - Check len >= 16, byte[14] == 0x03, byte[15] == 0xF0
    - _Requirements: 1.1, 1.2, 1.3_

  - [x] 1.3 Implement `aprs_decode_position` in `kiss-interface/aprs.c`
    - Handle data types '!' and '=' (position without timestamp): parse DDMM.MMN/DDDMM.MMW starting at byte 1
    - Handle data types '/' and '@' (position with timestamp): skip 7-byte timestamp, then parse position
    - Convert DDMM.MM to decimal degrees
    - Extract symbol table, symbol code, and comment text
    - Return -1 for unsupported types or malformed input
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 5.1, 5.2, 5.3, 5.4_

  - [x] 1.4 Implement `aprs_log_packet` in `kiss-interface/aprs.c`
    - Call `ax25_strip_frame` to get src/dst callsigns and info field
    - Attempt `aprs_decode_position` on the info field
    - Print timestamp, src, dst, decoded position (lat/lon in decimal degrees) or raw info text
    - Hex dump in verbose mode
    - Flush stdout
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 6.3_

  - [x]* 1.5 Write property test for frame classification (Property 1)
    - **Property 1: Frame classification correctness**
    - For random buffers, verify `aprs_is_ax25_frame` returns 1 iff len >= 16 AND byte[14] == 0x03 AND byte[15] == 0xF0
    - 1000 iterations
    - **Validates: Requirements 1.1, 1.2, 1.3**

  - [x]* 1.6 Write property test for position decode round-trip (Property 2)
    - **Property 2: Position decode round-trip with beacon encoder**
    - For random lat/lon/comment, build with `beacon_build_position`, parse with `aprs_decode_position`, verify within 0.02 arcminutes
    - 1000 iterations
    - **Validates: Requirements 5.5**

  - [x]* 1.7 Write property test for malformed input resilience (Property 3)
    - **Property 3: Malformed input resilience**
    - For random byte sequences 0-256 bytes, `aprs_decode_position` does not crash and returns -1 for invalid data
    - 1000 iterations
    - **Validates: Requirements 2.5**

  - [x]* 1.8 Write unit tests for APRS decoding
    - Test: classify valid AX.25 frame returns 1
    - Test: classify LTP segment returns 0
    - Test: classify short buffer returns 0
    - Test: decode "!5228.02N/00201.32W-comment" → lat≈52.467, lon≈-2.022
    - Test: decode "=" position type
    - Test: decode "/" position with timestamp
    - Test: decode unknown type returns -1
    - Test: empty info field returns -1
    - _Requirements: 1.1, 1.3, 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 2. Checkpoint — APRS decoder tests pass
  - Compile and run test_aprs. Ensure all tests pass.

- [ ] 3. Integrate frame classifier into main.c event loops
  - [x] 3.1 Add `#include "aprs.h"` to `kiss-interface/main.c`
    - _Requirements: 4.1_

  - [x] 3.2 Integrate classifier into LTP recv with beacon event loop
    - In the custom event loop for `CMD_MODE_LTP_RECV` with `beacon_enabled`: after `kiss_decoder_feed` returns 1, call `aprs_is_ax25_frame`. If AX.25, call `aprs_log_packet`. Otherwise call `ltp_process_segment`.
    - _Requirements: 4.2_

  - [x] 3.3 Integrate classifier into LTP send with beacon event loop
    - Same pattern as 3.2 for the `CMD_MODE_LTP_SEND` with `beacon_enabled` custom event loop.
    - _Requirements: 4.3_

  - [x] 3.4 Integrate classifier into `ltp_engine_run` path
    - For non-beacon LTP recv/send that uses `ltp_engine_run`: add a frame callback or modify `ltp_process_segment` to check for AX.25 first. Simplest: add a wrapper function `classify_and_process` that checks `aprs_is_ax25_frame` before calling `ltp_process_segment`, and use it in `ltp_engine_run`.
    - Alternative: modify `ltp_process_segment` to return a specific code for "not an LTP segment" so the caller can try APRS decode. This is cleaner since `ltp_process_segment` already returns -1 for unrecognized types.
    - _Requirements: 4.2, 4.5_

  - [x] 3.5 Enhance `cmd_receive` to decode APRS positions
    - In the existing AX.25 receive mode, after stripping the AX.25 frame, attempt `aprs_decode_position` on the info field and display decoded position if available.
    - _Requirements: 4.1_

- [ ] 4. Update Makefile
  - [x] 4.1 Add `aprs.c` to SRC, add `test_aprs` target, update test/clean targets
    - _Requirements: 6.1_

- [x] 5. Final checkpoint — Full build and all tests pass
  - Run `make clean && make && make test`. Ensure all tests pass.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- The frame classifier is a simple 3-line check — the main work is the APRS position parser
- `aprs_log_packet` reuses `ax25_strip_frame` unchanged
- The `beacon_build_position` function from beacon.c is used in the round-trip property test
- No existing source files are modified except main.c (for integration) and Makefile
