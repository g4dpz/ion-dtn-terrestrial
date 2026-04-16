# Implementation Plan: APRS Beacon

## Overview

Implement periodic APRS position beacon transmission for the `kiss_interface` tool. The beacon module (`beacon.h`/`beacon.c`) handles APRS position formatting, beacon state management, and periodic transmission. CLI extensions in `main.c` add a standalone `beacon` subcommand and a `--beacon` flag for `ltp-recv`. All code is C11, POSIX-only, static allocation, targeting GCC on Linux (x86_64 and ARM).

## Tasks

- [x] 1. Create beacon module header and core formatting functions
  - [x] 1.1 Create `kiss-interface/beacon.h` with the interface defined in the design document
    - Define `beacon_state_t`, constants (`BEACON_TOCALL`, `BEACON_DEFAULT_COMMENT`, `BEACON_DEFAULT_INTERVAL`, `BEACON_MIN_INTERVAL`, `BEACON_MAX_INTERVAL`, `BEACON_MAX_COMMENT`, `BEACON_MAX_POSITION`), and function prototypes (`beacon_format_lat`, `beacon_format_lon`, `beacon_build_position`, `beacon_init`, `beacon_transmit`, `beacon_get_timeout_ms`, `beacon_is_due`)
    - _Requirements: 1.1, 1.2, 1.3, 7.1, 7.2, 7.3_

  - [x] 1.2 Implement `beacon_format_lat` and `beacon_format_lon` in `kiss-interface/beacon.c`
    - Convert decimal degrees to APRS DDMM.MM format with N/S and E/W hemisphere indicators
    - Validate latitude range [-90, +90] and longitude range [-180, +180], return -1 on error
    - Zero-pad degrees (DD for lat, DDD for lon) and minutes (MM.MM)
    - Use `snprintf` with `%02d%05.2f` (lat) and `%03d%05.2f` (lon) formatting
    - _Requirements: 1.3, 1.8, 8.1, 8.2, 8.5_

  - [x] 1.3 Implement `beacon_build_position` in `kiss-interface/beacon.c`
    - Format: `!DDMM.MMN/DDDMM.MMW-<comment>`
    - Use `!` as data type identifier, `/` as symbol table, `-` as symbol code (house/QTH)
    - Handle empty comment string (position only, no trailing text)
    - _Requirements: 1.2, 1.6, 8.3, 8.4_

  - [x]* 1.4 Write property test for coordinate conversion round-trip (Property 3)
    - **Property 3: Coordinate conversion round-trip**
    - Format lat/lon with `beacon_format_lat`/`beacon_format_lon`, parse back to decimal degrees, verify within 0.02 arcminutes of original
    - 1000 iterations with random lat in [-90, +90] and lon in [-180, +180]
    - **Validates: Requirements 1.3, 8.5**

  - [x]* 1.5 Write property test for position string structural invariants (Property 2)
    - **Property 2: Position string structural invariants**
    - Verify `beacon_build_position` output starts with `!`, has correct lat/lon field structure, `/` at byte 9, `-` at byte 19, ends with comment
    - 1000 iterations with random coordinates and comment strings
    - **Validates: Requirements 1.2, 8.1, 8.2, 8.3, 8.4**

  - [x]* 1.6 Write unit tests for coordinate formatting edge cases
    - Test: lat 52.467 → "5228.02N", lon -2.022 → "00201.32W"
    - Test: lat 0.0 → "0000.00N", lon 0.0 → "00000.00E" (equator/prime meridian)
    - Test: lat -90.0 → "9000.00S", lon 180.0 → "18000.00E" (poles/antimeridian)
    - Test: empty comment produces valid position-only string
    - Test: default comment produces expected string
    - _Requirements: 1.3, 1.6, 8.1, 8.2, 6.5_

- [x] 2. Implement beacon initialization and frame pre-building
  - [x] 2.1 Implement `beacon_init` in `kiss-interface/beacon.c`
    - Validate callsign (non-empty, ≤6 chars excluding SSID, alphanumeric only), return -1 on error
    - Validate lat/lon ranges, return -1 on error
    - Validate interval in [10, 3600], return -1 on error
    - Call `beacon_build_position` to format the APRS info field
    - Call `ax25_build_frame("APZ001", callsign, info, info_len, ...)` to build AX.25 UI frame with control=0x03, PID=0xF0
    - Call `kiss_encode` on the AX.25 frame and store the pre-built KISS frame in `beacon_state_t`
    - Set `initialized = 1` on success
    - _Requirements: 1.1, 1.4, 1.5, 1.7, 1.8, 3.4, 7.2, 7.5_

  - [x]* 2.2 Write property test for beacon frame construction round-trip (Property 1)
    - **Property 1: Beacon frame construction round-trip**
    - For random valid callsigns, lat, lon, comment: call `beacon_init`, then `ax25_strip_frame` on the pre-built frame, verify dst="APZ001-0", src matches input callsign, info starts with `!` and contains comment
    - 1000 iterations
    - **Validates: Requirements 1.1, 5.1**

  - [x]* 2.3 Write property test for invalid input rejection (Property 4)
    - **Property 4: Invalid input rejection**
    - Verify `beacon_init` returns -1 for empty callsign, callsign >6 chars, non-alphanumeric callsign, lat outside [-90,+90], lon outside [-180,+180]
    - 1000 iterations with random invalid inputs
    - **Validates: Requirements 1.7, 1.8**

  - [x]* 2.4 Write property test for beacon interval validation (Property 5)
    - **Property 5: Beacon interval validation**
    - Verify `beacon_init` succeeds for interval in [10, 3600] and fails outside that range, given otherwise valid inputs
    - 1000 iterations with random interval values
    - **Validates: Requirements 3.4**

  - [x]* 2.5 Write unit tests for beacon_init edge cases
    - Test: beacon_init with "APZ001" as TOCALL (verify dst in frame)
    - Test: beacon_init sets control=0x03, PID=0xF0 in frame
    - Test: beacon_init rejects empty callsign
    - Test: beacon_init rejects lat=91.0
    - Test: beacon_init rejects interval=5
    - Test: beacon_init accepts interval=120
    - _Requirements: 1.1, 1.4, 1.7, 1.8, 3.4_

- [x] 3. Implement beacon timing and transmission
  - [x] 3.1 Implement `beacon_transmit` in `kiss-interface/beacon.c`
    - Write pre-built KISS frame to fd using `write()` + `tcdrain()`
    - Record `last_tx` timestamp via `clock_gettime(CLOCK_MONOTONIC)`
    - Print log line to stdout with timestamp and callsign
    - Return -1 on write error or short write
    - _Requirements: 3.2, 3.6, 5.1, 5.2_

  - [x] 3.2 Implement `beacon_get_timeout_ms` and `beacon_is_due` in `kiss-interface/beacon.c`
    - `beacon_get_timeout_ms`: compute `max(0, (last_tx + interval) - now)` in milliseconds using `clock_gettime(CLOCK_MONOTONIC)`, return -1 if not initialized
    - `beacon_is_due`: return 1 if timeout is 0, 0 otherwise, -1 if not initialized
    - _Requirements: 3.3, 4.5_

  - [x]* 3.3 Write property test for beacon timeout calculation (Property 6)
    - **Property 6: Beacon timeout calculation correctness**
    - Initialize beacon, set `last_tx` to a known time, verify `beacon_get_timeout_ms` returns correct value relative to current CLOCK_MONOTONIC time
    - 1000 iterations with random intervals and elapsed times
    - **Validates: Requirements 4.5**

- [x] 4. Checkpoint - Verify beacon module compiles and tests pass
  - Ensure `beacon.c` compiles with `gcc -Wall -Wextra -std=c11`
  - Ensure all beacon tests pass via `test_beacon` target
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Extend CLI parsing in main.c for beacon options
  - [x] 5.1 Add `CMD_MODE_BEACON` to `cmd_mode_t` enum and extend `cli_args_t` with beacon fields
    - Add fields: `beacon_callsign` (const char*), `beacon_lat` (double), `beacon_lon` (double), `beacon_comment` (const char*), `beacon_interval` (int), `beacon_enabled` (int)
    - Set defaults in `parse_args`: `beacon_comment = NULL` (use BEACON_DEFAULT_COMMENT at dispatch), `beacon_interval = 120`, `beacon_enabled = 0`
    - _Requirements: 6.1, 6.5, 6.6_

  - [x] 5.2 Extend `parse_args` to handle `beacon` subcommand and new options
    - Add `beacon` subcommand parsing (sets `CMD_MODE_BEACON`)
    - Add `--beacon` flag (no argument, sets `beacon_enabled = 1`)
    - Add long options: `--callsign`, `--lat`, `--lon`, `--comment`, `--beacon-interval`
    - Parse `--lat` and `--lon` with `strtod`
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 5.3 Extend `validate_args` for beacon mode and ltp-recv --beacon
    - For `CMD_MODE_BEACON`: require `--device`, `--callsign`, `--lat`, `--lon`
    - For `CMD_MODE_LTP_RECV` with `beacon_enabled`: require `--callsign`, `--lat`, `--lon`
    - Print specific error messages to stderr and return -1 on missing options
    - _Requirements: 6.2, 6.3_

  - [x] 5.4 Update `print_usage` to include beacon subcommand and options
    - Add `beacon` to the commands list
    - Add `--callsign`, `--lat`, `--lon`, `--comment`, `--beacon-interval`, `--beacon` to options list
    - Add beacon usage example
    - _Requirements: 6.4_

  - [x]* 5.5 Write CLI unit tests for beacon parsing
    - Add tests to `kiss-interface/test_cli.c` (compiled with `-DTEST_CLI_MODE`)
    - Test: CLI parses `beacon` subcommand
    - Test: CLI parses `--callsign`, `--lat`, `--lon`
    - Test: CLI parses `--beacon` flag for `ltp-recv`
    - Test: CLI default comment is NULL (resolved to BEACON_DEFAULT_COMMENT at dispatch)
    - Test: CLI default beacon-interval is 120
    - Test: CLI rejects beacon mode without --callsign
    - Test: CLI rejects ltp-recv --beacon without --lat
    - Test: --help includes beacon options
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

- [x] 6. Implement standalone beacon mode (cmd_beacon)
  - [x] 6.1 Implement `cmd_beacon` function in `kiss-interface/main.c`
    - Call `beacon_init` with callsign, lat, lon, comment, interval
    - Transmit initial beacon immediately via `beacon_transmit`
    - Enter `poll()` loop: sleep for `beacon_get_timeout_ms()`, transmit on timeout
    - Handle SIGINT/SIGTERM via `g_running` flag for clean exit with code 0
    - _Requirements: 3.1, 3.2, 3.3, 3.5, 5.2, 7.6_

  - [x] 6.2 Wire `cmd_beacon` into `main()` dispatch switch
    - Add `CMD_MODE_BEACON` case to the dispatch switch in `main()`
    - Add `CMD_MODE_BEACON` to verbose mode printing
    - Include `#include "beacon.h"` at top of main.c
    - _Requirements: 6.1_

- [x] 7. Integrate beacon into LTP receive mode
  - [x] 7.1 Modify `cmd_ltp_recv` to accept beacon parameters and integrate beacon timing
    - Add beacon parameters to `cmd_ltp_recv` signature (or pass via struct)
    - When `beacon_enabled`: call `beacon_init`, transmit initial beacon, integrate `beacon_get_timeout_ms` into `poll()` timeout as `min(ltp_timeout, beacon_timeout)`
    - When beacon is due and LTP channel is idle (no active non-completed export sessions): call `beacon_transmit`
    - When beacon is due but LTP is busy: defer until channel becomes idle
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 5.2, 5.3_

  - [x] 7.2 Wire beacon parameters from `cli_args_t` to `cmd_ltp_recv` in `main()` dispatch
    - Pass beacon_callsign, beacon_lat, beacon_lon, beacon_comment, beacon_interval, beacon_enabled to cmd_ltp_recv
    - _Requirements: 4.1, 6.3_

- [x] 8. Update Makefile and build integration
  - [x] 8.1 Add `beacon.c` to `SRC` in `kiss-interface/Makefile`
    - Update: `SRC = main.c kiss.c ax25.c serial.c ping.c ltp.c sdnv.c beacon.c`
    - _Requirements: 7.3_

  - [x] 8.2 Add `test_beacon` target to `kiss-interface/Makefile`
    - `test_beacon: test_beacon.c beacon.c ax25.c kiss.c` with `$(THEFT_FLAGS)`
    - Add `test_beacon` to the `test:` target dependencies and execution list
    - Update `test_cli` target to include `beacon.c` in its source list
    - _Requirements: 7.3_

- [x] 9. Final checkpoint - Ensure full build and all tests pass
  - Run `make clean && make kiss_interface` to verify the full build compiles
  - Run `make test` to verify all tests pass (including test_beacon and updated test_cli)
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The test pattern follows the existing codebase: hand-rolled random loops with `rand()`/`srand(time(NULL))`, 1000 iterations, with optional `theft` PBT library support
- Digipeater path support (Requirement 2) is deferred per the design decision — the initial implementation uses direct simplex (no `--path` option)
