# Requirements Document

## Introduction

This feature implements a GNU Radio-based APRS beacon transmitter using an Ettus B200 mini SDR, replacing the Mobilinkd TNC3 hardware for beacon transmission. The system constructs AX.25 UI frames with APRS position reports, applies Bell 202 AFSK modulation (1200 baud, 1200 Hz mark / 2200 Hz space), NRZI-encodes the bitstream, FM-modulates the signal, and transmits on 144.850 MHz via the B200 mini's UHD sink. This is the first step toward a fully software-defined radio pipeline for the existing DTN over amateur radio project. The GNU Radio components reside in a new `gnuradio/` directory, separate from the existing C codebase in `kiss-interface/`.

## Glossary

- **Flowgraph**: A GNU Radio signal processing pipeline composed of interconnected blocks that generate, process, and sink signal data.
- **AFSK_Modulator**: The GNU Radio block or block chain responsible for converting a digital bitstream into Bell 202 Audio Frequency Shift Keying audio tones (1200 Hz mark, 2200 Hz space) at 1200 baud.
- **NRZI_Encoder**: The module responsible for Non-Return-to-Zero Inverted encoding, where a zero bit causes a transition and a one bit causes no transition, as required by the AX.25 specification for AFSK transmission.
- **AX25_Framer**: The module responsible for constructing complete AX.25 UI frames including address fields, control/PID bytes, information field, bit stuffing, FCS (CRC-CCITT), and flag sequences.
- **FM_Modulator**: The GNU Radio block that applies narrowband FM modulation to the AFSK audio baseband signal for VHF transmission.
- **UHD_Sink**: The GNU Radio UHD USRP Sink block that transmits IQ samples to the Ettus B200 mini SDR hardware via USB3.
- **Beacon_Transmitter**: The top-level Python application that orchestrates AX.25 frame construction, AFSK/NRZI encoding, FM modulation, and SDR transmission as a periodic APRS beacon.
- **Bit_Stuffing**: The AX.25 requirement to insert a zero bit after every sequence of five consecutive one bits in the frame data (excluding flags), to prevent false flag detection.
- **FCS**: Frame Check Sequence, a 16-bit CRC-CCITT (polynomial 0x8408, reflected) appended to the AX.25 frame before bit stuffing, transmitted LSB first.
- **Flag_Sequence**: The AX.25 frame delimiter byte 0x7E, transmitted as preamble (multiple flags) before the frame and as a closing flag after the frame.
- **B200_Mini**: The Ettus Research USRP B200 mini Software Defined Radio, connected via USB3, using the UHD (USRP Hardware Driver) library.
- **APRS_Position_Report**: An APRS-formatted position string in uncompressed format: `!DDMM.MMN/DDDMM.MMW-comment`, using `/` as symbol table and `-` as symbol code (house/QTH).

## Requirements

### Requirement 1: AX.25 UI Frame Construction

**User Story:** As a radio operator, I want the GNU Radio beacon to construct valid AX.25 UI frames with APRS position reports, so that my transmissions are decodable by standard APRS receivers and infrastructure.

#### Acceptance Criteria

1. WHEN a callsign, latitude, longitude, and comment string are provided, THE AX25_Framer SHALL construct an AX.25 UI frame with the callsign-SSID as the source address, "APZ001" as the destination address, control byte 0x03 (UI), and PID byte 0xF0 (no layer 3).
2. THE AX25_Framer SHALL encode each address field as 7 bytes: 6 characters of the callsign left-shifted by one bit and space-padded, followed by an SSID byte with the extension bit set on the final address field.
3. THE AX25_Framer SHALL format the APRS information field as `!DDMM.MMN/DDDMM.MMW-` followed by the comment string, where latitude uses 2-digit degrees and minutes with N/S hemisphere, and longitude uses 3-digit degrees and minutes with E/W hemisphere.
4. THE AX25_Framer SHALL compute a 16-bit FCS using CRC-CCITT (polynomial 0x8408, initial value 0xFFFF) over the address, control, PID, and information fields, and append the FCS in little-endian byte order.
5. THE AX25_Framer SHALL apply Bit_Stuffing to the frame content between the opening and closing flags, inserting a zero bit after every five consecutive one bits.
6. THE AX25_Framer SHALL prepend a preamble of at least 25 Flag_Sequence bytes (0x7E) before the frame and append one Flag_Sequence byte after the frame, to allow receiver clock synchronisation.
7. IF the callsign is invalid (empty, base call longer than 6 characters, non-alphanumeric characters, or SSID outside 0-15), THEN THE AX25_Framer SHALL raise a ValueError with a descriptive message.
8. IF the latitude is outside the range -90.0 to +90.0 or the longitude is outside the range -180.0 to +180.0, THEN THE AX25_Framer SHALL raise a ValueError with a descriptive message.

### Requirement 2: NRZI Encoding

**User Story:** As a radio operator, I want the bitstream to be NRZI-encoded before AFSK modulation, so that the transmitted signal conforms to the AX.25 specification and is decodable by standard TNCs and software decoders.

#### Acceptance Criteria

1. THE NRZI_Encoder SHALL encode the bit-stuffed AX.25 bitstream using Non-Return-to-Zero Inverted encoding, where a zero bit produces a tone transition and a one bit produces no transition.
2. THE NRZI_Encoder SHALL initialise its state to the mark tone (logical high) at the start of each frame transmission.
3. FOR ALL valid AX.25 bitstreams, encoding with the NRZI_Encoder followed by decoding with a compliant NRZI decoder SHALL reproduce the original bitstream (round-trip property).

### Requirement 3: Bell 202 AFSK Modulation

**User Story:** As a radio operator, I want the beacon to generate Bell 202 AFSK audio at 1200 baud, so that the signal is compatible with the worldwide 1200 baud APRS network on VHF.

#### Acceptance Criteria

1. THE AFSK_Modulator SHALL generate a 1200 Hz sine tone for mark (logical one) and a 2200 Hz sine tone for space (logical zero), at a symbol rate of 1200 baud.
2. THE AFSK_Modulator SHALL maintain continuous phase between tone transitions, producing no discontinuities in the audio waveform at bit boundaries.
3. THE AFSK_Modulator SHALL generate audio samples at a sample rate that is an integer multiple of 1200 Hz and compatible with the FM_Modulator input requirements (minimum 48000 samples per second).
4. WHEN the NRZI-encoded bitstream is provided, THE AFSK_Modulator SHALL produce an audio waveform where each bit period contains exactly (sample_rate / 1200) samples at the corresponding tone frequency.

### Requirement 4: FM Modulation and SDR Transmission

**User Story:** As a radio operator, I want the AFSK audio to be FM-modulated and transmitted via the B200 mini SDR on 144.800 MHz, so that my APRS beacon is receivable by nearby stations on the standard UK APRS frequency.

#### Acceptance Criteria

1. THE FM_Modulator SHALL apply narrowband FM modulation to the AFSK audio baseband signal with a peak deviation of approximately 3.5 kHz, consistent with amateur VHF FM voice channel standards.
2. THE UHD_Sink SHALL transmit the FM-modulated IQ signal via the B200_Mini at a centre frequency of 144.850 MHz (configurable via command-line option).
3. THE UHD_Sink SHALL use a sample rate of at least 480000 samples per second to provide adequate bandwidth for the FM signal and meet the B200 mini minimum sample rate requirements (AD9364 RFIC constraint).
4. THE Beacon_Transmitter SHALL set the B200_Mini transmit gain to a configurable value (default 50, range 0-89), allowing the operator to control output power.
5. WHEN the Flowgraph is started, THE UHD_Sink SHALL verify that the B200_Mini hardware is connected and accessible. IF the B200_Mini is not detected, THEN THE Beacon_Transmitter SHALL print an error message to stderr and exit with code 1.
6. THE FM_Modulator SHALL produce an output signal bandwidth that fits within a 12.5 kHz channel, consistent with amateur VHF FM channelisation.

### Requirement 5: Periodic Beacon Scheduling

**User Story:** As a radio operator, I want the beacon to transmit at configurable intervals, so that my station is periodically identifiable on the APRS network and compliant with OFCOM licence conditions.

#### Acceptance Criteria

1. WHEN the Beacon_Transmitter is started, THE Beacon_Transmitter SHALL transmit one APRS beacon immediately and then repeat at the configured beacon interval (default 120 seconds).
2. WHEN the beacon interval is set to a value in the range 10 to 3600 seconds, THE Beacon_Transmitter SHALL accept the interval. IF the interval is outside this range, THEN THE Beacon_Transmitter SHALL print an error to stderr and exit with code 1.
3. THE Beacon_Transmitter SHALL use monotonic time (time.monotonic) for scheduling, avoiding drift from wall-clock adjustments.
4. WHEN SIGINT is received (Ctrl+C), THE Beacon_Transmitter SHALL stop the GNU Radio Flowgraph, release the B200_Mini hardware, and exit cleanly with code 0.
5. THE Beacon_Transmitter SHALL print a log line to stdout for each beacon transmitted, including an ISO 8601 timestamp and the source callsign.

### Requirement 6: Command-Line Interface

**User Story:** As a radio operator, I want to configure the beacon parameters from the command line, so that I can set my callsign, position, frequency, gain, and interval without editing source code.

#### Acceptance Criteria

1. THE Beacon_Transmitter SHALL accept the following command-line options: `--callsign` (source callsign-SSID, required), `--lat` (latitude in decimal degrees, required), `--lon` (longitude in decimal degrees, required), `--comment` (beacon comment text, default "github.com/g4dpz/ion-dtn-terrestrial"), `--interval` (beacon interval in seconds, default 120), `--freq` (transmit frequency in MHz, default 144.850), `--gain` (transmit gain 0-89, default 50), and `--sample-rate` (SDR sample rate, default 480000).
2. IF `--callsign`, `--lat`, or `--lon` is not provided, THEN THE Beacon_Transmitter SHALL print a usage message to stderr and exit with code 1.
3. THE Beacon_Transmitter SHALL validate all numeric parameters and print a descriptive error to stderr if a value is out of range or not a valid number.
4. WHEN `--help` is specified, THE Beacon_Transmitter SHALL print a help message describing all options and exit with code 0.

### Requirement 7: AX.25 Frame Serialisation Round-Trip

**User Story:** As a developer, I want to verify that AX.25 frames constructed by the GNU Radio beacon are correctly formed, so that interoperability with existing APRS infrastructure is assured.

#### Acceptance Criteria

1. FOR ALL valid combinations of callsign, latitude, longitude, and comment, THE AX25_Framer SHALL produce frames where the FCS validates correctly when recomputed over the frame content (FCS integrity property).
2. FOR ALL valid AX.25 frames produced by the AX25_Framer, removing bit stuffing and extracting the address, control, PID, and information fields SHALL yield the original input values (round-trip property).
3. THE AX25_Framer SHALL produce frames that, when decoded by the existing `ax25_strip_frame` function in `kiss-interface/ax25.c` (after removing flags, bit stuffing, and FCS), yield the correct source callsign, destination callsign, and information field content.

### Requirement 8: Signal Quality and Regulatory Compliance

**User Story:** As a licensed radio operator, I want the transmitted signal to meet amateur radio standards and OFCOM licence conditions, so that my transmissions are legal and do not cause interference.

#### Acceptance Criteria

1. THE Beacon_Transmitter SHALL include the operator's callsign-SSID as the AX.25 source address in every transmitted APRS beacon, satisfying OFCOM identification requirements.
2. THE Beacon_Transmitter SHALL transmit only on frequencies within the amateur 2-metre band (144.000-146.000 MHz). IF the configured frequency is outside this range, THEN THE Beacon_Transmitter SHALL print an error to stderr and exit with code 1.
3. THE AFSK_Modulator SHALL produce mark and space tones within 1% of the nominal 1200 Hz and 2200 Hz frequencies respectively, ensuring reliable demodulation by receiving stations.
4. THE Beacon_Transmitter SHALL cease transmission within 1 second of receiving a SIGINT signal, to allow the operator to comply with OFCOM requirements to stop transmitting on demand.

### Requirement 9: Project Structure and Dependencies

**User Story:** As a developer, I want the GNU Radio beacon to be organised in a separate directory with clear dependencies, so that it does not interfere with the existing C codebase and is straightforward to build and run.

#### Acceptance Criteria

1. THE Beacon_Transmitter SHALL be implemented as a Python application in the `gnuradio/` directory at the repository root, separate from the existing `kiss-interface/` C codebase.
2. THE Beacon_Transmitter SHALL depend only on Python 3.8 or later, GNU Radio 3.10 or later (with gr-uhd), and the Python standard library.
3. THE Beacon_Transmitter SHALL include a `requirements.txt` or equivalent dependency specification listing the required Python packages.
4. THE Beacon_Transmitter SHALL be executable as `python3 gnuradio/aprs_beacon.py` with no additional installation steps beyond installing GNU Radio and UHD.
5. THE Beacon_Transmitter SHALL include a README file in the `gnuradio/` directory documenting hardware setup, dependencies, and usage examples for the B200 mini.
