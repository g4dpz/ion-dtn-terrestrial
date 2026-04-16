# Implementation Plan: KISS Ping

## Overview

Add a `ping` subcommand to the existing `kiss_interface` tool. Implementation proceeds bottom-up: first the standalone `ping.c` module (payload build/parse), then CLI integration in `main.c` (argument parsing, validation, `cmd_ping` orchestration), then Makefile wiring, and finally tests. All code is C11, POSIX-only, targeting gcc on Linux.

## Tasks

- [x] 1. Create ping payload module (`ping.h` / `ping.c`)
  - [x] 1.1 Create `kiss-interface/ping.h` with the public interface
    - Define `PING_MAGIC`, `PING_MAGIC_LEN`, `PING_PAYLOAD_LEN` constants
    - Declare `ping_build_payload(uint16_t seq, int64_t tx_us, uint8_t *out, size_t out_size)`
    - Declare `ping_parse_payload(const uint8_t *data, size_t len, uint16_t *seq, int64_t *tx_us)`
    - Declare `ping_now_us(void)`
    - _Requirements: 2.1, 8.1, 8.2, 8.3_

  - [x] 1.2 Implement `kiss-interface/ping.c`
    - `ping_build_payload`: write 4-byte magic "PING", 2-byte seq in network byte order (`htons`), 8-byte timestamp in network byte order into `out`; return -1 if `out_size < PING_PAYLOAD_LEN`
    - `ping_parse_payload`: check `len >= PING_PAYLOAD_LEN`, verify magic bytes match "PING", extract seq (`ntohs`) and timestamp from network byte order; return -1 on mismatch or short buffer
    - `ping_now_us`: call `clock_gettime(CLOCK_MONOTONIC)`, convert to microseconds as `int64_t`
    - _Requirements: 2.1, 2.3, 8.1, 8.2, 8.3_

  - [x]* 1.3 Write property test: payload round-trip (Property 1)
    - **Property 1: Ping payload round-trip**
    - For random `uint16_t seq` and non-negative `int64_t tx_us`, `ping_build_payload` then `ping_parse_payload` recovers original values
    - Use `theft` if available, else hand-rolled random loop with 1000 iterations
    - **Validates: Requirements 8.1**

  - [x]* 1.4 Write property test: payload structural invariant (Property 2)
    - **Property 2: Ping payload structural invariant**
    - For random seq and timestamp, output is exactly 14 bytes with bytes 0–3 == "PING", bytes 4–5 == `htons(seq)`, bytes 6–13 == network-order timestamp
    - **Validates: Requirements 2.1**

  - [x]* 1.5 Write property test: non-ping payload rejection (Property 3)
    - **Property 3: Non-ping payload rejection**
    - For random buffers whose first 4 bytes != "PING", `ping_parse_payload` returns -1
    - **Validates: Requirements 8.3**

  - [x]* 1.6 Write unit tests for ping payload edge cases
    - `ping_build_payload` with `out_size < 14` returns -1
    - `ping_parse_payload` with buffer < 14 bytes returns -1
    - `ping_parse_payload` with exactly 14 valid bytes succeeds and returns correct seq/timestamp
    - `ping_build_payload` with seq=1 and known timestamp produces expected byte sequence
    - _Requirements: 2.1, 8.1, 8.3_

- [x] 2. Checkpoint — Verify ping module compiles and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Extend CLI parsing in `main.c` for ping subcommand
  - [x] 3.1 Add `CMD_MODE_PING` to `cmd_mode_t` enum and extend `cli_args_t`
    - Add `CMD_MODE_PING` after `CMD_MODE_ECHO` in the enum
    - Add `int count`, `int timeout_ms`, `int interval_ms` fields to `cli_args_t`
    - Set defaults in `parse_args`: count=4, timeout_ms=5000, interval_ms=1000
    - _Requirements: 1.1, 1.3, 1.4, 1.5_

  - [x] 3.2 Extend `parse_args` to handle `ping` subcommand and new options
    - Recognize `"ping"` as a subcommand, set `args->mode = CMD_MODE_PING`
    - Add `--count`, `--timeout`, `--interval` to `long_opts` array and switch cases
    - _Requirements: 1.1, 1.3, 1.4, 1.5_

  - [x] 3.3 Extend `validate_args` for ping mode
    - Require `--device`, `--src`, `--dst` for ping mode (same as send/echo)
    - Validate `count > 0`, `timeout_ms > 0`
    - Print specific error message identifying the missing/invalid argument
    - _Requirements: 1.2, 1.6_

  - [x] 3.4 Update `print_usage` to include ping subcommand and its options
    - Add `ping` to the Commands section
    - Add `--count`, `--timeout`, `--interval` to the Options section
    - Add a ping example to the Examples section
    - _Requirements: 1.7_

  - [x] 3.5 Add `#include "ping.h"` to `main.c`
    - _Requirements: 1.1_

- [x] 4. Implement `cmd_ping` in `main.c`
  - [x] 4.1 Implement the `cmd_ping` function
    - Signature: `int cmd_ping(int fd, const char *src, const char *dst, int count, int timeout_ms, int interval_ms, int verbose)`
    - Print header line: "PING <dst> from <src>: <count> packets"
    - Initialize `ping_stats_t` accumulator (sent, received, rtt_min, rtt_max, rtt_sum)
    - _Requirements: 5.3_

  - [x] 4.2 Implement the per-ping send/receive loop inside `cmd_ping`
    - For seq = 1 to count (while `g_running`):
      - Call `ping_build_payload(seq, ping_now_us(), ...)`
      - Build AX.25 frame with `ax25_build_frame`, KISS-encode with `kiss_encode`
      - `write()` + `tcdrain()` to serial fd
      - Increment `stats.sent`
    - _Requirements: 2.1, 2.2, 2.3, 3.1_

  - [x] 4.3 Implement the reply wait logic with `poll()` and timeout handling
    - Use `poll(fd, remaining_ms)` in a loop
    - On `POLLIN`: `read()`, feed to `kiss_decoder_feed`, on complete frame: `ax25_strip_frame` + `ping_parse_payload`
    - If magic and seq match: compute RTT = `(ping_now_us() - tx_us) / 1000.0` ms, print result line (payload size, source callsign, seq, RTT with fractional ms), update stats
    - If seq mismatch: discard, adjust remaining timeout, continue polling
    - On timeout: print timeout message with seq number, increment loss counter
    - If verbose: print hex dump of received echo frame
    - Sleep `interval_ms` between pings (using `usleep`)
    - _Requirements: 3.2, 3.3, 3.4, 3.5, 3.6, 4.1, 4.2, 4.3, 5.1, 5.2_

  - [x] 4.4 Implement summary statistics output
    - After loop (or on signal exit): print packets transmitted, received, loss percentage
    - If received > 0: print min/avg/max RTT (avg = rtt_sum / received)
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 4.5 Wire `cmd_ping` into the `main()` dispatch switch
    - Add `CMD_MODE_PING` case in the dispatch switch, calling `cmd_ping` with the parsed args
    - Add `"ping"` to the verbose mode_str mapping
    - Print ping-specific verbose config (count, timeout, interval)
    - _Requirements: 1.1, 7.1, 7.2, 7.3_

- [x] 5. Checkpoint — Verify main.c compiles with ping support
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Update Makefile and add tests
  - [x] 6.1 Update `kiss-interface/Makefile`
    - Add `ping.c` to `SRC` list
    - Add `test_ping` target: `test_ping: test_ping.c ping.c` with `$(THEFT_FLAGS)`
    - Update `test_cli` target to include `ping.c` in its source list
    - Add `test_ping` to the `test` target and its `./test_ping` invocation
    - Add `test_ping` to the `clean` target
    - _Requirements: 1.1_

  - [x] 6.2 Create `kiss-interface/test_ping.c` with property and unit tests
    - Include property tests from tasks 1.3, 1.4, 1.5 and unit tests from task 1.6
    - Use the same test infrastructure pattern as `test_cli.c` (TEST macro, tests_run/tests_passed counters)
    - Conditionally use `theft` via `#ifdef HAVE_THEFT`, fall back to `rand()`/`srand()` loop with 1000 iterations
    - _Requirements: 2.1, 8.1, 8.2, 8.3_

  - [x] 6.3 Extend `kiss-interface/test_cli.c` with ping CLI tests
    - Update the `cmd_mode_t` enum and `cli_args_t` struct declarations to include `CMD_MODE_PING` and the new fields (count, timeout_ms, interval_ms)
    - Add test: `parse_ping_subcommand` — parses `ping` and sets `CMD_MODE_PING`
    - Add test: `parse_ping_options` — parses `--count 10 --timeout 3000 --interval 500` with correct values
    - Add test: `parse_ping_defaults` — verifies count=4, timeout_ms=5000, interval_ms=1000
    - Add test: `validate_missing_device_ping` — missing `--device` for ping returns error
    - Add test: `validate_missing_src_ping` — missing `--src` for ping returns error
    - Add test: `validate_missing_dst_ping` — missing `--dst` for ping returns error
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

- [x] 7. Final checkpoint — Ensure all tests pass
  - Run `make test` in `kiss-interface/`. Ensure `test_ping`, `test_cli`, and all existing tests pass. Ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Property tests validate the correctness properties defined in the design document
- All code is C11, compiled with `gcc -Wall -Wextra -O2 -std=c11`, POSIX APIs only
- The `theft` PBT library is auto-detected by the Makefile; tests fall back to hand-rolled random loops if unavailable
- No dynamic allocation — all buffers are stack-allocated, consistent with the existing codebase
