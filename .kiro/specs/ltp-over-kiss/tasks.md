# Implementation Plan: LTP over KISS

## Overview

Bottom-up implementation: SDNV module first, then LTP engine (segment encoding, session management, timers, event loop), then main.c CLI integration, and finally Makefile wiring. Each step builds on the previous, with property-based and unit tests placed close to the code they validate.

## Tasks

- [x] 1. Implement SDNV module
  - [x] 1.1 Create `kiss-interface/sdnv.h` and `kiss-interface/sdnv.c`
    - Implement `sdnv_encode()`: encode uint64_t to SDNV bytes (7 data bits per byte, MSB continuation bit, big-endian)
    - Implement `sdnv_decode()`: decode SDNV bytes back to uint64_t, return bytes consumed or -1 on error
    - Handle edge cases: value 0, max value 2^63-1, output buffer too small, truncated input
    - _Requirements: 1.3, 1.4, 13.1, 13.2, 13.4, 13.5_

  - [x]* 1.2 Write property tests for SDNV (in `kiss-interface/test_sdnv.c`)
    - **Property 1: SDNV encode/decode round-trip**
    - **Validates: Requirements 1.3, 13.1, 13.2, 13.3**

  - [x]* 1.3 Write property test for SDNV structural invariant
    - **Property 2: SDNV encoding structural invariant**
    - **Validates: Requirements 13.1, 13.4**

  - [x]* 1.4 Write unit tests for SDNV known values and error cases
    - Test encode 0 → `[0x00]`, 127 → `[0x7F]`, 128 → `[0x81, 0x00]`, 2^63-1 → 10 bytes
    - Test decode truncated buffer returns -1, encode with out_size=0 returns -1
    - _Requirements: 13.1, 13.5_

- [x] 2. Checkpoint — SDNV tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Implement LTP segment encoding and decoding
  - [x] 3.1 Create `kiss-interface/ltp.h`
    - Define all types from the design: `ltp_seg_type_t`, `ltp_segment_hdr_t`, `ltp_data_segment_t`, `ltp_claim_t`, `ltp_report_segment_t`, `ltp_report_ack_segment_t`, `ltp_cancel_segment_t`, `ltp_timer_t`, `ltp_recv_map_t`, `ltp_export_session_t`, `ltp_import_session_t`, `ltp_endpoint_t`, `ltp_config_t`, `ltp_engine_t`
    - Declare all function prototypes: `ltp_engine_init`, `ltp_send_block`, `ltp_process_segment`, `ltp_get_next_timeout_ms`, `ltp_fire_expired_timers`, `ltp_engine_run`, `ltp_cancel_session`, `ltp_eid_to_engine_id`, `ltp_register_endpoint`, `ltp_encode_data_segment`, `ltp_decode_segment`, `ltp_encode_report`, `ltp_encode_report_ack`, `ltp_encode_cancel`
    - Use compile-time constants from design: `LTP_MAX_EXPORT_SESSIONS=128`, `LTP_MAX_IMPORT_SESSIONS=128`, `LTP_MAX_BLOCK_SIZE=1024`, `LTP_DEFAULT_SEGMENT_MTU=64`, etc.
    - _Requirements: 1.1, 1.2, 2.1, 2.2, 3.6, 3.7, 6.4_

  - [x] 3.2 Implement segment encoding functions in `kiss-interface/ltp.c`
    - Implement `ltp_encode_data_segment()`: encode header (version/type byte, SDNV engine ID, SDNV session number, extension counts) + data content (client svc ID, offset, length, optional CP/RPT serials, payload)
    - Implement `ltp_encode_report()`: encode report segment with claims
    - Implement `ltp_encode_report_ack()`: encode report acknowledgment
    - Implement `ltp_encode_cancel()`: encode cancel segment with reason code
    - _Requirements: 1.1, 1.5, 2.1, 2.2_

  - [x] 3.3 Implement segment decoding in `kiss-interface/ltp.c`
    - Implement `ltp_decode_segment()`: parse header, extract type, dispatch to type-specific body parsing
    - Handle unrecognized segment types (5-7, 10-11) by returning error
    - _Requirements: 1.2, 1.6, 2.3_

  - [x]* 3.4 Write property test for LTP data segment round-trip
    - **Property 3: LTP data segment encode/decode round-trip**
    - **Validates: Requirements 1.1, 1.2, 1.7, 2.1**

  - [x]* 3.5 Write property test for LTP control segment round-trip
    - **Property 4: LTP control segment encode/decode round-trip**
    - **Validates: Requirements 1.7, 2.2**

  - [x]* 3.6 Write unit tests for segment encoding edge cases (in `kiss-interface/test_ltp.c`)
    - Test decode segment with invalid type (5) returns error
    - Test segment with offset+length > block size is rejected
    - _Requirements: 2.3, 6.5_

- [x] 4. Implement endpoint mapping and engine initialization
  - [x] 4.1 Implement `ltp_eid_to_engine_id()` using DJB2 hash of callsign portion
    - Strip "dtn://" prefix, hash remaining string
    - _Requirements: 8.1, 8.2_

  - [x] 4.2 Implement `ltp_register_endpoint()` and `ltp_engine_init()`
    - Zero-initialize engine state, set config, derive local engine ID, register local endpoint
    - _Requirements: 8.3, 8.4_

  - [x]* 4.3 Write property test for engine ID determinism
    - **Property 10: Engine ID hash determinism**
    - **Validates: Requirements 8.2**

- [x] 5. Checkpoint — Segment encoding/decoding and endpoint mapping tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Implement block segmentation and session management
  - [x] 6.1 Implement `ltp_send_block()` in `kiss-interface/ltp.c`
    - Allocate export session, copy block data, segment into data segments of configured MTU
    - Mark final segment as `LTP_SEG_RED_DATA_EORP_CP` with checkpoint serial
    - KISS-encode each segment via `kiss_encode()`, write to fd with `tcdrain()`
    - Enforce max export sessions limit, max block size limit
    - _Requirements: 3.1, 3.6, 3.8, 4.1, 4.6, 6.1, 6.4, 9.1_

  - [x] 6.2 Implement import session creation and data buffering
    - On first data segment for unknown session, create import session
    - Buffer received data at offset, update receive map (claims tracking)
    - _Requirements: 3.2, 6.2_

  - [x] 6.3 Implement block reassembly and delivery
    - When all byte ranges received, reassemble block, invoke `on_block_received` callback, close import session
    - _Requirements: 3.5, 6.3_

  - [x]* 6.4 Write property test for block segmentation completeness
    - **Property 5: Block segmentation completeness**
    - **Validates: Requirements 3.1, 4.1, 6.1**

  - [x]* 6.5 Write property test for block segmentation/reassembly round-trip
    - **Property 6: Block segmentation/reassembly round-trip**
    - **Validates: Requirements 3.5, 6.2, 6.3**

  - [x]* 6.6 Write unit tests for session limits and block size limits
    - Test max export sessions enforced (129th rejected)
    - Test max import sessions enforced (129th rejected)
    - Test 1024-byte block accepted, 1025-byte block rejected
    - _Requirements: 3.6, 3.7, 3.8, 6.4_

- [x] 7. Implement checkpoint/report exchange and retransmission
  - [x] 7.1 Implement checkpoint processing in `ltp_process_segment()`
    - On receiving checkpoint: generate reception report with claims from recv_map, KISS-encode and send report, start report retransmission timer
    - _Requirements: 4.2, 4.7_

  - [x] 7.2 Implement report processing in `ltp_process_segment()`
    - On receiving report: send report ack, check claims against block data
    - If all data acknowledged: close export session
    - If gaps: retransmit missing byte ranges as new data segments with new checkpoint
    - _Requirements: 4.3, 4.4, 3.4_

  - [x] 7.3 Implement report ack processing
    - On receiving report ack: cancel report retransmission timer
    - _Requirements: 4.5_

  - [x]* 7.4 Write property test for reception report claims accuracy
    - **Property 7: Reception report claims accuracy**
    - **Validates: Requirements 4.2**

  - [x]* 7.5 Write property test for retransmission targeting missing ranges
    - **Property 8: Retransmission targets exactly missing ranges**
    - **Validates: Requirements 4.4**

  - [x]* 7.6 Write property test for monotonically increasing serial numbers
    - **Property 9: Session serial numbers are monotonically increasing**
    - **Validates: Requirements 4.6, 4.7**

  - [x]* 7.7 Write unit tests for checkpoint/report exchange
    - Test report ack generated on report receipt
    - Test report ack cancels report timer
    - Test export session closed after full report
    - Test import session delivers block and closes
    - _Requirements: 3.4, 3.5, 4.3, 4.5_

- [x] 8. Implement timer management
  - [x] 8.1 Implement `ltp_get_next_timeout_ms()` and `ltp_fire_expired_timers()`
    - Linear scan of timer array, compute min expiry for poll() timeout
    - On expiry: retransmit checkpoint/report/cancel, increment retry count
    - On max retries exceeded: cancel session
    - Timer duration = 2 × OWLT + 200ms
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7, 12.3, 12.4_

  - [x]* 8.2 Write unit tests for timer management
    - Test timer duration = 2*OWLT + 200ms
    - Test session cancelled after max retries
    - _Requirements: 5.1, 5.5, 5.6_

- [x] 9. Implement session cancellation
  - [x] 9.1 Implement `ltp_cancel_session()` and cancel segment processing
    - Send cancel-by-sender/receiver segment, start cancel retransmission timer
    - On receiving cancel: send cancel ack, close session
    - On receiving cancel ack: close session
    - Handle cancel for unknown session (send ack, ignore)
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5_

  - [x]* 9.2 Write unit tests for cancellation
    - Test cancel-by-sender triggers cancel ack
    - Test cancel-by-receiver triggers cancel ack
    - Test segment with wrong engine ID discarded
    - _Requirements: 7.2, 7.4, 8.5_

- [x] 10. Implement LTP event loop
  - [x] 10.1 Implement `ltp_engine_run()` in `kiss-interface/ltp.c`
    - poll() on serial fd with timeout from `ltp_get_next_timeout_ms()`
    - On POLLIN: read bytes, feed to `kiss_decoder_feed()`, on complete frame call `ltp_process_segment()`
    - On timeout: call `ltp_fire_expired_timers()`
    - Check `g_running` each iteration, exit cleanly on signal
    - send_mode: exit when export session completes or is cancelled
    - recv_mode: loop until signal
    - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5_

  - [x]* 10.2 Write property test for KISS frame size limit
    - **Property 11: KISS frame size limit for LTP segments**
    - **Validates: Requirements 9.5**

- [x] 11. Checkpoint — LTP engine tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 12. Integrate LTP into main.c CLI
  - [x] 12.1 Extend `cmd_mode_t` enum and `cli_args_t` struct in `kiss-interface/main.c`
    - Add `CMD_MODE_LTP_SEND` and `CMD_MODE_LTP_RECV` to enum
    - Add fields: `local_eid`, `remote_eid`, `mtu`, `owlt_ms`, `retries`
    - Add `#include "ltp.h"` to main.c
    - _Requirements: 10.1, 10.2, 11.1, 11.2, 14.1_

  - [x] 12.2 Extend `parse_args()` and `validate_args()` in `kiss-interface/main.c`
    - Add "ltp-send" and "ltp-recv" subcommand parsing
    - Add `--local`, `--remote`, `--mtu`, `--owlt`, `--retries` option parsing
    - Set defaults: mtu=64, owlt=1500, retries=7
    - Validate: --device and --local required for both; --remote and payload required for ltp-send
    - _Requirements: 10.1, 10.2, 11.1, 11.2_

  - [x] 12.3 Implement `cmd_ltp_send()` in `kiss-interface/main.c`
    - Initialize LTP engine with local EID and config
    - Register remote endpoint, submit payload as red-part block via `ltp_send_block()`
    - Run `ltp_engine_run()` in send mode until session completes
    - Print success message with segment count and transfer time, or error on failure
    - _Requirements: 10.3, 10.4, 10.5, 10.6_

  - [x] 12.4 Implement `cmd_ltp_recv()` in `kiss-interface/main.c`
    - Initialize LTP engine with local EID and config
    - Set `on_block_received` callback to print block contents with timestamp and remote EID
    - Run `ltp_engine_run()` in receive mode until signal
    - Print summary on exit
    - _Requirements: 11.3, 11.4, 11.5, 11.6_

  - [x] 12.5 Extend `main()` dispatch and `print_usage()` in `kiss-interface/main.c`
    - Add `CMD_MODE_LTP_SEND` and `CMD_MODE_LTP_RECV` cases to switch
    - Update usage text with ltp-send and ltp-recv examples
    - _Requirements: 10.1, 11.1, 14.1_

  - [x]* 12.6 Extend CLI tests in `kiss-interface/test_cli.c`
    - Test CLI parses `ltp-send` subcommand with all args
    - Test CLI parses `ltp-recv` subcommand with all args
    - Test CLI defaults: mtu=64, owlt=1500, retries=7
    - Test missing --local for ltp-send prints error
    - Test existing send/receive/echo/ping commands still parse correctly
    - _Requirements: 10.1, 10.2, 11.1, 11.2, 14.1_

- [x] 13. Update Makefile
  - [x] 13.1 Update `kiss-interface/Makefile` with new sources and test targets
    - Add `sdnv.c` and `ltp.c` to SRC list
    - Add `test_sdnv` target: `test_sdnv.c sdnv.c` with `$(THEFT_FLAGS)`
    - Add `test_ltp` target: `test_ltp.c ltp.c sdnv.c kiss.c` with `$(THEFT_FLAGS)`
    - Update `test_cli` target to include `ltp.c sdnv.c`
    - Add `test_sdnv` and `test_ltp` to the `test` target and clean target
    - _Requirements: 14.2_

- [x] 14. Final checkpoint — Full build and all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Property tests use `theft` library if available, otherwise fall back to hand-rolled random loops with `rand()`/`srand(time(NULL))` (1000 iterations), matching existing test pattern
- Checkpoints ensure incremental validation at each layer boundary
- Implementation order is bottom-up: SDNV → segment codec → sessions → timers → event loop → CLI → Makefile
