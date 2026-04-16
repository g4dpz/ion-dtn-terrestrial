# Requirements Document

## Introduction

This feature adds periodic APRS position beacon transmission to the existing `kiss_interface` tool. The beacon serves two purposes: OFCOM callsign identification compliance (transmitting the operator's callsign at regular intervals) and APRS position reporting (advertising the station's location on the APRS network). The beacon integrates into the existing LTP event loop for use during LTP receive sessions, and also operates as a standalone beacon mode. All beacons are transmitted as standard AX.25 UI frames using the existing AX.25 and KISS modules unchanged.

## Glossary

- **Beacon_Module**: The new `beacon.h`/`beacon.c` compilation unit responsible for APRS position packet construction and periodic transmission scheduling.
- **CLI**: The command-line interface implemented in `main.c` that parses arguments and dispatches to mode functions.
- **APRS_Packet**: An AX.25 UI frame whose information field contains an APRS-formatted position report string, with the operator's callsign as the AX.25 source address and an APRS TOCALL as the destination address.
- **TOCALL**: The AX.25 destination address in an APRS packet, used to identify the originating software. This feature uses "APZ001" (experimental APRS software, per the APRS TOCALL registry).
- **Digipeater_Path**: A comma-separated list of AX.25 relay addresses (e.g. "WIDE1-1,WIDE2-1") used for multi-hop APRS routing. For direct simplex links, no path is needed.
- **Beacon_Interval**: The time in seconds between consecutive beacon transmissions (default 120 seconds).
- **Position_String**: An APRS-formatted position report in uncompressed format: `!DDMM.MMN/DDDMM.MMW-comment`, where '/' is the symbol table selector and '-' is the symbol code for house/QTH.
- **LTP_Engine**: The existing Licklider Transmission Protocol engine (`ltp.c`) with a `poll()`-based event loop.
- **Half_Duplex_Constraint**: The requirement that only one transmission occurs at a time on the RF channel, meaning beacons are deferred while LTP data segments or acknowledgments are in-flight.
- **SSID**: Secondary Station Identifier, a numeric suffix (0-15) appended to an amateur callsign (e.g. "G4DPZ-1").

## Requirements

### Requirement 1: APRS Position Packet Construction

**User Story:** As a radio operator, I want the tool to construct valid APRS position packets from my callsign and coordinates, so that my station is identifiable and locatable on the APRS network.

#### Acceptance Criteria

1. WHEN a callsign, latitude, longitude, and comment string are provided, THE Beacon_Module SHALL construct an APRS_Packet with the callsign-SSID as the AX.25 source address and "APZ001" as the AX.25 destination address.
2. THE Beacon_Module SHALL format the APRS position information field as `!DDMM.MMN/DDDMM.MMW-` followed by the comment string, where the latitude and longitude are in APRS uncompressed format with degrees and decimal minutes.
3. WHEN latitude is in the range -90.0 to +90.0 and longitude is in the range -180.0 to +180.0, THE Beacon_Module SHALL convert the decimal degree values to APRS DDMM.MM format with the correct N/S and E/W hemisphere indicators.
4. THE Beacon_Module SHALL set the AX.25 control byte to 0x03 (UI) and the PID byte to 0xF0 (no layer 3), consistent with APRS protocol requirements.
5. THE Beacon_Module SHALL use the existing `ax25_build_frame` function to construct the AX.25 UI frame and the existing `kiss_encode` function to produce the KISS frame, without modifying those modules.
6. WHEN the comment string is empty, THE Beacon_Module SHALL construct a valid APRS_Packet with only the position data and no trailing comment text.
7. IF the callsign string is invalid (empty, longer than 6 characters excluding SSID, or contains non-alphanumeric characters), THEN THE Beacon_Module SHALL return an error code.
8. IF the latitude is outside the range -90.0 to +90.0 or the longitude is outside the range -180.0 to +180.0, THEN THE Beacon_Module SHALL return an error code.

### Requirement 2: Digipeater Path Support

**User Story:** As a radio operator, I want to configure the digipeater path for my APRS beacons, so that I can choose between direct simplex transmission and multi-hop APRS routing.

#### Acceptance Criteria

1. WHERE the `--path` option is specified, THE Beacon_Module SHALL insert the specified digipeater addresses into the AX.25 frame header between the destination and source address fields, with the address extension bit set correctly on the final address.
2. WHEN no `--path` option is specified, THE Beacon_Module SHALL construct the AX.25 frame with only the destination and source addresses (no digipeater path), suitable for direct simplex links.
3. THE Beacon_Module SHALL support a digipeater path of up to 2 relay addresses (e.g. "WIDE1-1,WIDE2-1"), consistent with standard APRS practice.
4. IF a digipeater path address is invalid (empty, longer than 6 characters excluding SSID, or contains non-alphanumeric characters), THEN THE Beacon_Module SHALL print an error to stderr and exit with code 1.

### Requirement 3: Periodic Beacon Transmission (Standalone Mode)

**User Story:** As a radio operator, I want a standalone beacon mode that transmits periodic APRS position beacons, so that I can run an APRS beacon station without LTP.

#### Acceptance Criteria

1. WHEN the `beacon` subcommand is invoked with the required options, THE CLI SHALL transmit an APRS_Packet immediately upon startup and then repeat at the configured Beacon_Interval.
2. THE Beacon_Module SHALL use `clock_gettime(CLOCK_MONOTONIC)` to schedule beacon transmissions, avoiding drift from wall-clock adjustments.
3. WHILE the beacon mode is running, THE CLI SHALL use `poll()` with a timeout equal to the time remaining until the next beacon, so that the process sleeps efficiently between transmissions.
4. WHEN the Beacon_Interval is set to a value in the range 10 to 3600 seconds, THE Beacon_Module SHALL accept the interval. IF the interval is outside this range, THEN THE Beacon_Module SHALL print an error to stderr and exit with code 1.
5. WHEN SIGINT or SIGTERM is received, THE CLI SHALL stop the beacon loop and exit cleanly with code 0.
6. THE CLI SHALL print a log line to stdout for each beacon transmitted, including a timestamp and the callsign.

### Requirement 4: Beacon Integration with LTP Receive Mode

**User Story:** As a radio operator, I want to enable periodic APRS beacons during LTP receive sessions, so that my station remains identifiable per OFCOM requirements while waiting for LTP data.

#### Acceptance Criteria

1. WHEN the `--beacon` flag is specified with the `ltp-recv` subcommand along with the required beacon options (`--callsign`, `--lat`, `--lon`), THE CLI SHALL enable periodic beacon transmission during the LTP receive event loop.
2. WHILE the LTP_Engine has no export or import sessions with pending transmissions (no segments or acknowledgments queued for transmission), THE Beacon_Module SHALL transmit a beacon when the Beacon_Interval has elapsed.
3. WHILE the LTP_Engine has segments or acknowledgments queued for transmission, THE Beacon_Module SHALL defer the beacon until the LTP transmission completes, respecting the Half_Duplex_Constraint.
4. WHEN a deferred beacon's interval has elapsed and the LTP channel becomes idle, THE Beacon_Module SHALL transmit the beacon at the next opportunity.
5. THE Beacon_Module SHALL integrate with the existing `poll()`-based event loop by contributing its next-beacon timeout to the `poll()` timeout calculation, using `min(ltp_timeout, beacon_timeout)` as the effective timeout.

### Requirement 5: OFCOM Callsign Identification

**User Story:** As a licensed radio operator, I want every beacon to contain my callsign, so that my station satisfies OFCOM identification requirements at all times.

#### Acceptance Criteria

1. THE Beacon_Module SHALL include the operator's callsign-SSID as the AX.25 source address in every transmitted APRS_Packet.
2. THE Beacon_Module SHALL transmit beacons at intervals no greater than the configured Beacon_Interval (default 120 seconds), ensuring the station is identifiable as frequently as practicable per OFCOM licence conditions.
3. WHEN operating in LTP receive mode with beaconing enabled, THE Beacon_Module SHALL transmit a beacon within the configured Beacon_Interval plus a tolerance of no more than 10 seconds (to account for Half_Duplex_Constraint deferral).

### Requirement 6: CLI Options for Beacon Configuration

**User Story:** As a radio operator, I want to configure beacon parameters from the command line, so that I can set my callsign, position, comment, and interval without recompiling.

#### Acceptance Criteria

1. THE CLI SHALL accept the following options for beacon configuration: `--callsign` (source callsign-SSID), `--lat` (latitude in decimal degrees), `--lon` (longitude in decimal degrees), `--comment` (beacon comment text, default "DTN/LTP station"), `--beacon-interval` (seconds, default 120), and `--path` (digipeater path, optional).
2. WHEN the `beacon` subcommand is used, THE CLI SHALL require `--device`, `--callsign`, `--lat`, and `--lon` as mandatory options. IF any mandatory option is missing, THEN THE CLI SHALL print a specific error message to stderr and exit with code 1.
3. WHEN the `--beacon` flag is used with `ltp-recv`, THE CLI SHALL require `--callsign`, `--lat`, and `--lon` in addition to the existing LTP options. IF any required beacon option is missing, THEN THE CLI SHALL print a specific error message to stderr and exit with code 1.
4. THE CLI SHALL display beacon-related options in the `--help` output, including the `beacon` subcommand and the `--beacon` flag for `ltp-recv`.
5. WHEN `--comment` is not specified, THE CLI SHALL use "github.com/g4dpz/ion-dtn-terrestrial" as the default comment text.
6. WHEN `--beacon-interval` is not specified, THE CLI SHALL use 120 seconds as the default interval.

### Requirement 7: Implementation Constraints

**User Story:** As a developer, I want the beacon feature to follow the same implementation patterns as the existing codebase, so that the tool remains consistent, portable, and dependency-free.

#### Acceptance Criteria

1. THE Beacon_Module SHALL be implemented in C using only POSIX APIs and the C standard library, with no external dependencies.
2. THE Beacon_Module SHALL use static allocation only (no `malloc`/`free`), consistent with the existing codebase.
3. THE Beacon_Module SHALL be implemented as a new compilation unit (`beacon.h`/`beacon.c`) added to the existing Makefile.
4. THE Beacon_Module SHALL compile and run correctly on both x86_64 (Ubuntu) and ARM (Raspberry Pi) targets using GCC with `-Wall -Wextra -std=c11`.
5. THE Beacon_Module SHALL reuse the existing `ax25_build_frame`, `kiss_encode`, `serial_open`, `serial_close`, and `serial_configure_tnc` functions without modification.
6. THE Beacon_Module SHALL reuse the existing `g_running` signal flag and `sigaction()` setup from `main.c` for clean shutdown.

### Requirement 8: APRS Position Format Correctness

**User Story:** As a radio operator, I want the APRS position format to be correct and parseable by standard APRS software (e.g. APRS-IS, direwolf, Xastir), so that my beacon is useful on the APRS network.

#### Acceptance Criteria

1. THE Beacon_Module SHALL format latitude as `DDMM.MM` followed by `N` for positive values or `S` for negative values, where DD is degrees (00-90) and MM.MM is minutes (00.00-59.99), zero-padded.
2. THE Beacon_Module SHALL format longitude as `DDDMM.MM` followed by `E` for positive values or `W` for negative values, where DDD is degrees (000-180) and MM.MM is minutes (00.00-59.99), zero-padded.
3. THE Beacon_Module SHALL use `!` as the APRS data type identifier (position without timestamp, no messaging).
4. THE Beacon_Module SHALL use `/` as the symbol table identifier and `-` as the symbol code (house/QTH) in the position string.
5. THE Beacon_Module SHALL produce a position string that, when parsed by a standards-compliant APRS decoder, yields coordinates within 0.02 minutes (approximately 37 metres) of the original decimal degree input.
