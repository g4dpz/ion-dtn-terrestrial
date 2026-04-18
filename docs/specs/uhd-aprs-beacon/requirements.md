# Requirements Document

## Introduction

This feature implements a pure C APRS beacon transmitter using the UHD C API directly, replacing the Python/GNU Radio implementation with a native version that integrates into the existing `kiss-interface` codebase. The system reuses the existing `beacon.c` for APRS position formatting and `ax25.c` for AX.25 frame construction, and adds new modules for CRC-CCITT FCS computation, bit stuffing, NRZI encoding, Bell 202 AFSK audio generation, direct FM modulation, and UHD-based SDR transmission via the Ettus B200 mini.

Critical design parameters are derived from field-tested lessons learned during the GNU Radio prototype (documented in `gnuradio/LESSONS-LEARNED.md`): NRZI initial state of 0 (space), inverted AFSK tone mapping through FM (NRZI 1 → 2200 Hz, NRZI 0 → 1200 Hz), direct FM modulation via frequency modulator sensitivity (not NBFM with pre-emphasis), 3000 Hz FM deviation, 480 kHz SDR sample rate, 48 kHz audio sample rate, 80 preamble flag bytes, and 3 closing flag bytes.

The implementation targets C11 (gcc), POSIX-only, and runs on both Ubuntu (x86_64) and Raspberry Pi (ARM). The only external dependency beyond the C standard library and POSIX is libuhd (UHD C API).

## Glossary

- **NRZI_Encoder**: The module (`nrzi.c/nrzi.h`) responsible for Non-Return-to-Zero Inverted encoding of bit-stuffed AX.25 bitstreams, where a zero bit causes a state transition and a one bit causes no transition.
- **AFSK_Modulator**: The module (`afsk.c/afsk.h`) responsible for generating Bell 202 AFSK audio samples (1200 Hz mark, 2200 Hz space at 1200 baud) from NRZI-encoded bitstreams using a continuous-phase accumulator.
- **UHD_Transmitter**: The module (`uhd_tx.c/uhd_tx.h`) responsible for configuring the Ettus B200 mini via the UHD C API, applying direct FM modulation to AFSK audio, rational resampling from audio rate to SDR rate, and streaming IQ samples to the hardware.
- **SDR_Beacon**: The CLI subcommand (`sdr-beacon`) added to `main.c` that orchestrates the full transmission pipeline: AX.25 frame construction → FCS → bit stuffing → flag framing → NRZI encoding → AFSK modulation → FM modulation → UHD transmission.
- **FCS_Computer**: The component within the frame pipeline that computes CRC-CCITT (polynomial 0x8408, initial 0xFFFF) over AX.25 frame content and appends the 16-bit result in little-endian byte order.
- **Bit_Stuffer**: The component that inserts a zero bit after every five consecutive one bits in the AX.25 frame content between flags, preventing false flag detection by receivers.
- **Flag_Framer**: The component that prepends preamble flag bytes (0x7E) and appends closing flag bytes to the bit-stuffed frame content.
- **FM_Modulator**: The component within the UHD_Transmitter that converts AFSK audio samples to complex IQ samples using direct frequency modulation with sensitivity = 2π × deviation / sample_rate.
- **Rational_Resampler**: The component within the UHD_Transmitter that interpolates complex IQ samples from the audio sample rate (48 kHz) to the SDR sample rate (480 kHz) using a 10:1 integer ratio.
- **B200_Mini**: The Ettus Research USRP B200 mini Software Defined Radio, connected via USB3, controlled through the UHD C API (libuhd).
- **Silence_Padding**: Blocks of zero-valued audio samples prepended and appended to the AFSK audio burst to allow the B200 mini TX chain to stabilise before and after the signal.

## Requirements

### Requirement 1: AX.25 Bitstream Frame Construction

**User Story:** As a radio operator, I want the C beacon to construct complete AX.25 bitstreams with FCS, bit stuffing, and flag framing from the existing AX.25 byte-level frames, so that the bitstream is ready for NRZI encoding and AFSK modulation.

#### Acceptance Criteria

1. WHEN an AX.25 UI frame (produced by the existing `ax25_build_frame` function) is provided, THE FCS_Computer SHALL compute a 16-bit CRC-CCITT (polynomial 0x8408, initial value 0xFFFF, final XOR 0xFFFF) over the frame bytes and append the FCS in little-endian byte order.
2. THE Bit_Stuffer SHALL convert the frame bytes (including FCS) to a bitstream in LSB-first order and insert a zero bit after every five consecutive one bits.
3. THE Flag_Framer SHALL prepend 80 flag bytes (0x7E, each represented as the bit pattern 01111110) before the bit-stuffed content and append 3 flag bytes after the bit-stuffed content.
4. FOR ALL valid AX.25 frame byte sequences, converting to bits, applying bit stuffing, and then removing bit stuffing SHALL reproduce the original bitstream (bit stuffing round-trip property).
5. FOR ALL valid AX.25 frame byte sequences, the bit-stuffed output SHALL contain no run of six or more consecutive one bits within the frame content region (between flags).
6. FOR ALL valid combinations of callsign, latitude, longitude, and comment, the FCS appended by the FCS_Computer SHALL validate correctly when recomputed over the frame content excluding the FCS bytes (FCS integrity property).

### Requirement 2: NRZI Encoding

**User Story:** As a radio operator, I want the bitstream to be NRZI-encoded before AFSK modulation, so that the transmitted signal conforms to the AX.25 specification and is decodable by standard TNCs and software decoders.

#### Acceptance Criteria

1. THE NRZI_Encoder SHALL encode the flag-framed bitstream using Non-Return-to-Zero Inverted encoding, where a zero bit produces a state transition (toggle) and a one bit produces no transition (hold).
2. THE NRZI_Encoder SHALL initialise its state to 0 (space) at the start of each frame encoding, so that the preamble flag bytes (0x7E = 01111110 LSB-first) produce predominantly mark tone (1200 Hz) during the six consecutive one-bits.
3. FOR ALL valid bitstreams, encoding with the NRZI_Encoder followed by decoding with a compliant NRZI decoder (where a transition maps to 0 and no transition maps to 1) SHALL reproduce the original bitstream (round-trip property).

### Requirement 3: Bell 202 AFSK Modulation

**User Story:** As a radio operator, I want the beacon to generate Bell 202 AFSK audio at 1200 baud with inverted tone mapping, so that the signal is correctly demodulated after FM transmission and reception.

#### Acceptance Criteria

1. THE AFSK_Modulator SHALL generate audio samples at 48000 Hz sample rate, producing exactly 40 samples per bit period (48000 / 1200 baud).
2. THE AFSK_Modulator SHALL use inverted tone mapping as required for correct demodulation through the FM transmission chain: NRZI state 1 maps to 2200 Hz and NRZI state 0 maps to 1200 Hz.
3. THE AFSK_Modulator SHALL maintain continuous phase between tone transitions using a phase accumulator, producing no discontinuities in the audio waveform at bit boundaries.
4. WHEN the NRZI-encoded bitstream is provided, THE AFSK_Modulator SHALL produce a float array of audio samples where the total length equals the number of NRZI bits multiplied by 40 (samples per bit).
5. THE AFSK_Modulator SHALL produce mark and space tones within 1% of the nominal frequencies (1200 Hz and 2200 Hz respectively), ensuring reliable demodulation by receiving stations.

### Requirement 4: Direct FM Modulation

**User Story:** As a radio operator, I want the AFSK audio to be FM-modulated using direct frequency modulation (not NBFM with pre-emphasis), so that the data signal is transmitted cleanly without voice-oriented filtering that corrupts packet data.

#### Acceptance Criteria

1. THE FM_Modulator SHALL convert AFSK audio samples to complex IQ samples using direct frequency modulation with sensitivity calculated as 2π × max_deviation / audio_sample_rate.
2. THE FM_Modulator SHALL use a default FM deviation of 3000 Hz, configurable via a command-line option.
3. IF the FM deviation is set to a value outside the range 1000 to 5000 Hz, THEN THE SDR_Beacon SHALL print an error to stderr and exit with code 1.
4. THE FM_Modulator SHALL produce unit-magnitude complex samples (magnitude 1.0) suitable for direct transmission by the UHD_Transmitter.

### Requirement 5: Rational Resampling

**User Story:** As a radio operator, I want the FM-modulated IQ signal to be resampled from the audio rate to the SDR rate, so that the B200 mini receives samples at a rate within its hardware constraints.

#### Acceptance Criteria

1. THE Rational_Resampler SHALL interpolate complex IQ samples from the audio sample rate (48000 Hz) to the SDR sample rate (default 480000 Hz) using an integer interpolation ratio.
2. THE Rational_Resampler SHALL use zero-order hold or linear interpolation for the resampling, producing a continuous IQ stream at the SDR sample rate.
3. WHEN the SDR sample rate is not an integer multiple of the audio sample rate, THE SDR_Beacon SHALL print an error to stderr and exit with code 1.

### Requirement 6: UHD SDR Transmission

**User Story:** As a radio operator, I want the resampled IQ signal to be transmitted via the B200 mini using the UHD C API, so that my APRS beacon is broadcast on the configured VHF frequency.

#### Acceptance Criteria

1. THE UHD_Transmitter SHALL configure the B200_Mini via the UHD C API with the specified centre frequency, transmit gain, and sample rate.
2. THE UHD_Transmitter SHALL transmit the FM-modulated, resampled IQ signal as a single burst for each beacon frame, including 1 second of silence padding before and after the AFSK audio to allow the B200 mini TX chain to stabilise.
3. THE UHD_Transmitter SHALL set the B200_Mini transmit gain to a configurable value (default 50, range 0-89).
4. IF the B200_Mini hardware is not detected or the UHD C API returns an error during initialisation, THEN THE UHD_Transmitter SHALL print a descriptive error message to stderr and return an error code.
5. THE UHD_Transmitter SHALL use a default SDR sample rate of 480000 Hz, meeting the B200 mini minimum sample rate constraint imposed by the AD9364 RFIC.
6. THE UHD_Transmitter SHALL transmit only on frequencies within the amateur 2-metre band (144.000-146.000 MHz). IF the configured frequency is outside this range, THEN THE SDR_Beacon SHALL print an error to stderr and exit with code 1.

### Requirement 7: Silence Padding for TX Ramp-Up

**User Story:** As a radio operator, I want silence padding around the AFSK audio burst, so that the B200 mini TX chain has time to stabilise and the preamble flags are not corrupted.

#### Acceptance Criteria

1. THE UHD_Transmitter SHALL prepend 1 second of zero-valued audio samples (48000 samples at audio rate) before the AFSK audio burst, to allow the B200 mini TX chain to stabilise.
2. THE UHD_Transmitter SHALL append 1 second of zero-valued audio samples after the AFSK audio burst, to allow the TX chain to ramp down cleanly.
3. THE Flag_Framer SHALL use 80 preamble flag bytes (~533 ms of AFSK tones at 1200 baud), providing sufficient receiver synchronisation time after the B200 mini TX ramp-up period.

### Requirement 8: CLI Subcommand Integration

**User Story:** As a radio operator, I want an `sdr-beacon` subcommand in the existing `kiss_interface` CLI, so that I can transmit APRS beacons via the B200 mini using the same tool I use for TNC-based operations.

#### Acceptance Criteria

1. THE SDR_Beacon SHALL be invocable as `kiss_interface sdr-beacon` with the following options: `--callsign` (required), `--lat` (required), `--lon` (required), `--comment` (default "github.com/g4dpz/ion-dtn-terrestrial"), `--beacon-interval` (default 120 seconds), `--freq` (default 144.850 MHz), `--gain` (default 50), `--sample-rate` (default 480000 Hz), `--deviation` (default 3000 Hz), and `--verbose`.
2. IF `--callsign`, `--lat`, or `--lon` is not provided, THEN THE SDR_Beacon SHALL print a usage message to stderr and exit with code 1.
3. WHEN the beacon interval is set to a value outside the range 10 to 3600 seconds, THE SDR_Beacon SHALL print an error to stderr and exit with code 1.
4. WHEN `--help` is specified, THE SDR_Beacon SHALL include the `sdr-beacon` subcommand in the help output with a description of all its options.
5. WHEN SIGINT is received (Ctrl+C), THE SDR_Beacon SHALL stop transmission, release the B200_Mini hardware via the UHD C API, and exit cleanly with code 0 within 1 second.

### Requirement 9: Periodic Beacon Scheduling

**User Story:** As a radio operator, I want the beacon to transmit at configurable intervals using monotonic time, so that my station is periodically identifiable on the APRS network.

#### Acceptance Criteria

1. WHEN the SDR_Beacon is started, THE SDR_Beacon SHALL transmit one APRS beacon immediately and then repeat at the configured beacon interval.
2. THE SDR_Beacon SHALL use CLOCK_MONOTONIC for scheduling, avoiding drift from wall-clock adjustments.
3. THE SDR_Beacon SHALL print a log line to stdout for each beacon transmitted, including a timestamp (HH:MM:SS format) and the source callsign.

### Requirement 10: Build System Integration

**User Story:** As a developer, I want the new SDR modules to integrate into the existing Makefile build system, so that the project builds cleanly with or without the UHD library installed.

#### Acceptance Criteria

1. THE Makefile SHALL add a new `sdr-beacon` build target that compiles and links the new modules (nrzi.c, afsk.c, uhd_tx.c) together with the existing modules (beacon.c, ax25.c, kiss.c, main.c) and libuhd.
2. THE Makefile SHALL detect the availability of libuhd at build time and conditionally compile the SDR beacon support, so that the rest of the project builds without libuhd installed.
3. THE Makefile SHALL add test targets for the new modules: `test_nrzi`, `test_afsk`, and `test_uhd_tx`, following the existing test naming convention.
4. THE new source files SHALL use C11 standard (gcc -std=c11) and compile cleanly with -Wall -Wextra, consistent with the existing codebase.

### Requirement 11: NRZI Encoding Round-Trip Verification

**User Story:** As a developer, I want to verify that NRZI encoding and decoding are exact inverses, so that the transmitted bitstream can be correctly recovered by receivers.

#### Acceptance Criteria

1. THE NRZI_Encoder module SHALL provide both `nrzi_encode` and `nrzi_decode` functions.
2. FOR ALL bitstreams of length 0 to 1000 bits containing only values 0 and 1, encoding with `nrzi_encode` (initial state 0) followed by decoding with `nrzi_decode` (initial state 0) SHALL reproduce the original bitstream exactly (round-trip property).

### Requirement 12: AFSK Modulation Structural Properties

**User Story:** As a developer, I want to verify that the AFSK modulator produces correctly structured audio output, so that the signal chain from bits to audio is demonstrably correct.

#### Acceptance Criteria

1. FOR ALL NRZI-encoded bitstreams of length 1 to 200 bits, the output of the AFSK_Modulator SHALL have exactly N × 40 float samples, where N is the number of input bits.
2. FOR ALL runs of 3 or more consecutive identical NRZI bits, the dominant frequency of the corresponding audio segment (measured by FFT peak) SHALL be within 50 Hz plus one FFT bin width of the expected frequency (2200 Hz for NRZI state 1, 1200 Hz for NRZI state 0).
3. THE AFSK_Modulator SHALL produce audio samples in the range [-1.0, +1.0].

### Requirement 13: Cross-Platform Compilation

**User Story:** As a developer, I want the new modules to compile on both x86_64 (Ubuntu) and ARM (Raspberry Pi), so that the beacon can be deployed on either platform.

#### Acceptance Criteria

1. THE new source files (nrzi.c, afsk.c, uhd_tx.c) SHALL use only C11 standard library functions, POSIX functions, and the UHD C API, with no platform-specific code.
2. THE new modules SHALL use static allocation only (no malloc/free), consistent with the existing codebase coding style.
3. THE new modules SHALL not depend on any external libraries other than libuhd, the C standard library, and the POSIX math library (libm).
