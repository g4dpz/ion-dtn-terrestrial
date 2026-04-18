# Requirements Document

## Introduction

This feature implements an APRS receive path using the Ettus B200 mini SDR via the UHD C API, completing the reverse signal chain from the existing SDR beacon transmitter. The system receives FM-modulated AFSK packets from the air on 144.800 MHz (UK APRS frequency), demodulates them, and decodes AX.25/APRS position reports.

The RX signal chain is the inverse of the existing TX path:
1. UHD USRP Source → IQ samples at 480 kHz
2. FM demodulation → extract audio at 48 kHz (decimate 10:1)
3. AFSK demodulation → extract NRZI bitstream from 1200/2200 Hz tones
4. NRZI decode → recover original bitstream
5. AX.25 frame detection → find flags, de-stuff bits, verify FCS
6. APRS decode → parse position reports using existing `aprs.c`

The implementation reuses the existing `nrzi_decode`, `nrzi_bit_destuff`, `nrzi_compute_fcs` functions from `nrzi.c`, the `ax25_strip_frame` function from `ax25.c`, and the `aprs_decode_position`/`aprs_log_packet` functions from `aprs.c`. New modules are needed for UHD receive with FM demodulation (`uhd_rx.c/uhd_rx.h`) and AFSK demodulation with clock recovery (`afsk_demod.c/afsk_demod.h`).

Critical design parameters derived from the TX path and lessons learned (`gnuradio/LESSONS-LEARNED.md`): SDR sample rate 480 kHz (B200 mini minimum), audio sample rate 48 kHz after 10:1 decimation, inverted AFSK tone mapping through FM (mark 1200 Hz ↔ space 2200 Hz polarity may be inverted), atan2-based FM discriminator, and the need to handle both tone polarities since the FM chain can invert frequency sense.

The implementation targets C11 (gcc), POSIX-only, static allocation only, and runs on both Ubuntu (x86_64) and Raspberry Pi (ARM). The only external dependency beyond the C standard library and POSIX is libuhd (UHD C API).

## Glossary

- **UHD_Receiver**: The module (`uhd_rx.c/uhd_rx.h`) responsible for configuring the Ettus B200 mini via the UHD C API for reception, streaming IQ samples from the hardware, applying FM demodulation, and decimating from SDR rate to audio rate.
- **FM_Demodulator**: The component within the UHD_Receiver that extracts instantaneous frequency from complex IQ samples using an atan2-based discriminator (computing the phase difference between consecutive samples).
- **Decimator**: The component within the UHD_Receiver that reduces the sample rate from 480 kHz (SDR rate) to 48 kHz (audio rate) using a 10:1 decimation ratio, with appropriate low-pass filtering to prevent aliasing.
- **AFSK_Demodulator**: The module (`afsk_demod.c/afsk_demod.h`) responsible for detecting Bell 202 AFSK tones (1200 Hz mark, 2200 Hz space) in the demodulated audio stream, recovering bit timing, and producing a raw bitstream.
- **Tone_Detector**: The component within the AFSK_Demodulator that determines whether each bit period contains a mark (1200 Hz) or space (2200 Hz) tone, using correlation or Goertzel-based detection.
- **Clock_Recovery**: The component within the AFSK_Demodulator that synchronises bit sampling to the received signal, compensating for timing drift between transmitter and receiver clocks.
- **Frame_Detector**: The component that scans the NRZI-decoded bitstream for AX.25 flag patterns (0x7E = 01111110), extracts frame content between flags, removes bit stuffing, and verifies the CRC-CCITT FCS before accepting a frame.
- **NRZI_Decoder**: The existing `nrzi_decode` function from `nrzi.c` that converts NRZI-encoded bits back to the original bitstream (transition → 0, no transition → 1).
- **SDR_Receiver**: The CLI subcommand (`sdr-recv`) added to `main.c` that orchestrates the full receive pipeline: UHD receive → FM demod → AFSK demod → NRZI decode → frame detect → APRS decode.
- **B200_Mini**: The Ettus Research USRP B200 mini Software Defined Radio, connected via USB3, controlled through the UHD C API (libuhd).
- **Bit_Destuffer**: The existing `nrzi_bit_destuff` function from `nrzi.c` that removes zero bits inserted after every five consecutive one bits during AX.25 transmission.
- **FCS_Verifier**: The component that recomputes CRC-CCITT over the de-stuffed frame content and compares it against the received FCS bytes to verify frame integrity.

## Requirements

### Requirement 1: UHD SDR Reception

**User Story:** As a radio operator, I want to receive IQ samples from the B200 mini SDR on the APRS frequency, so that I can capture over-the-air APRS packets for decoding.

#### Acceptance Criteria

1. THE UHD_Receiver SHALL configure the B200_Mini via the UHD C API with the specified centre frequency, receive gain, and sample rate.
2. THE UHD_Receiver SHALL use a default receive frequency of 144.800 MHz (UK APRS), configurable via a command-line option.
3. THE UHD_Receiver SHALL use a default SDR sample rate of 480000 Hz, meeting the B200 mini minimum sample rate constraint imposed by the AD9364 RFIC.
4. THE UHD_Receiver SHALL set the B200_Mini receive gain to a configurable value (default 50, range 0-76).
5. THE UHD_Receiver SHALL receive only on frequencies within the amateur 2-metre band (144.000-146.000 MHz). IF the configured frequency is outside this range, THEN THE SDR_Receiver SHALL print an error to stderr and exit with code 1.
6. IF the B200_Mini hardware is not detected or the UHD C API returns an error during initialisation, THEN THE UHD_Receiver SHALL print a descriptive error message to stderr and return an error code.
7. THE UHD_Receiver SHALL stream IQ samples continuously from the B200_Mini in `fc32` format (32-bit float complex) until stopped by SIGINT.

### Requirement 2: FM Demodulation

**User Story:** As a radio operator, I want the received IQ samples to be FM-demodulated to extract the audio signal, so that the AFSK tones carrying the packet data can be recovered.

#### Acceptance Criteria

1. THE FM_Demodulator SHALL extract instantaneous frequency from complex IQ samples using an atan2-based discriminator that computes the phase difference between consecutive samples.
2. THE FM_Demodulator SHALL produce one real-valued audio sample per input IQ sample, representing the instantaneous frequency deviation.
3. THE FM_Demodulator SHALL normalise the output so that the maximum FM deviation maps to a signal amplitude of 1.0.
4. FOR ALL constant-frequency complex sinusoid inputs at frequency offset f within the range -5000 Hz to +5000 Hz, the FM_Demodulator SHALL produce an output whose mean value is within 5% of the expected normalised value f/max_deviation (FM demodulation accuracy property).

### Requirement 3: Decimation from SDR Rate to Audio Rate

**User Story:** As a radio operator, I want the FM-demodulated signal to be decimated from 480 kHz to 48 kHz, so that the AFSK demodulator receives audio at the standard sample rate matching the 1200 baud bit period of 40 samples.

#### Acceptance Criteria

1. THE Decimator SHALL reduce the sample rate from 480000 Hz to 48000 Hz using a 10:1 integer decimation ratio.
2. THE Decimator SHALL apply a low-pass anti-aliasing filter before decimation to prevent spectral folding of out-of-band noise into the audio passband.
3. THE Decimator SHALL preserve signal content below 3000 Hz (the AFSK signal bandwidth) with less than 3 dB attenuation.
4. WHEN the SDR sample rate is not an integer multiple of the audio sample rate (48000 Hz), THE SDR_Receiver SHALL print an error to stderr and exit with code 1.

### Requirement 4: AFSK Demodulation

**User Story:** As a radio operator, I want the audio signal to be demodulated to detect Bell 202 AFSK tones and produce a raw bitstream, so that the NRZI-encoded AX.25 data can be recovered from the received signal.

#### Acceptance Criteria

1. THE AFSK_Demodulator SHALL detect mark (1200 Hz) and space (2200 Hz) tones in the audio stream at 1200 baud (40 samples per bit at 48 kHz).
2. THE AFSK_Demodulator SHALL use correlation-based tone detection, computing the correlation of each bit period with reference mark and space waveforms and selecting the tone with the higher correlation magnitude.
3. THE AFSK_Demodulator SHALL handle both normal and inverted tone polarity, since the FM transmission chain can invert the frequency sense of the AFSK tones.
4. THE AFSK_Demodulator SHALL produce one NRZI-level output bit (0 or 1) per detected bit period, where 1 represents the mark tone and 0 represents the space tone.
5. FOR ALL AFSK audio generated by the existing `afsk_modulate` function from a known NRZI bitstream, the AFSK_Demodulator SHALL recover the original NRZI bitstream exactly when processing the audio without noise or channel impairment (AFSK modulation/demodulation round-trip property).

### Requirement 5: Clock Recovery

**User Story:** As a radio operator, I want the AFSK demodulator to synchronise its bit sampling to the received signal, so that packets from transmitters with slightly different clock rates are decoded reliably.

#### Acceptance Criteria

1. THE Clock_Recovery SHALL adjust the bit sampling phase based on detected tone transitions in the audio stream.
2. THE Clock_Recovery SHALL tolerate a clock frequency offset of at least ±50 parts per million between transmitter and receiver, corresponding to the typical crystal oscillator tolerance in amateur radio equipment.
3. THE Clock_Recovery SHALL re-synchronise within 8 bit periods (one AX.25 flag byte) when a new packet preamble is detected.

### Requirement 6: NRZI Decoding

**User Story:** As a radio operator, I want the NRZI-encoded bitstream to be decoded back to the original data bits, so that the AX.25 frame content can be extracted.

#### Acceptance Criteria

1. THE NRZI_Decoder SHALL decode the bitstream using the existing `nrzi_decode` function from `nrzi.c`, where a transition between consecutive bits maps to 0 and no transition maps to 1.
2. FOR ALL NRZI-encoded bitstreams produced by the existing `nrzi_encode` function (initial state 0), decoding with `nrzi_decode` (initial state 0) SHALL reproduce the original bitstream exactly (round-trip property).

### Requirement 7: AX.25 Frame Detection and Validation

**User Story:** As a radio operator, I want the receiver to detect AX.25 frames in the decoded bitstream by finding flag patterns, removing bit stuffing, and verifying the FCS, so that only valid frames are passed to the APRS decoder.

#### Acceptance Criteria

1. THE Frame_Detector SHALL scan the NRZI-decoded bitstream for the AX.25 flag pattern 0x7E (01111110 in LSB-first bit order) to identify frame boundaries.
2. WHEN a sequence of bits between two flag patterns is detected, THE Frame_Detector SHALL remove bit stuffing using the existing `nrzi_bit_destuff` function from `nrzi.c`.
3. THE Frame_Detector SHALL convert the de-stuffed bitstream back to bytes (LSB-first per byte) and extract the 16-bit CRC-CCITT FCS from the last two bytes.
4. THE FCS_Verifier SHALL recompute CRC-CCITT (polynomial 0x8408, initial 0xFFFF, final XOR 0xFFFF) over the frame content (excluding FCS bytes) using the existing `nrzi_compute_fcs` function and compare the result against the received FCS.
5. IF the computed FCS does not match the received FCS, THEN THE Frame_Detector SHALL discard the frame silently and continue scanning for the next flag pattern.
6. IF the de-stuffed frame content is shorter than 16 bytes (minimum AX.25 UI header), THEN THE Frame_Detector SHALL discard the frame and continue scanning.
7. FOR ALL valid AX.25 frames constructed by the existing `ax25_build_frame` function and processed through the full TX pipeline (FCS → bit stuff → flag frame → NRZI encode → AFSK modulate) then the full RX pipeline (FM demod → AFSK demod → NRZI decode → flag detect → de-stuff → FCS verify), THE Frame_Detector SHALL recover the original AX.25 frame bytes exactly (full TX/RX round-trip property).

### Requirement 8: APRS Packet Decoding and Display

**User Story:** As a radio operator, I want received APRS position reports to be decoded and displayed with timestamp, callsign, coordinates, and comment, so that I can monitor APRS traffic on the frequency.

#### Acceptance Criteria

1. WHEN a valid AX.25 frame passes FCS verification, THE SDR_Receiver SHALL pass the frame to the existing `aprs_log_packet` function from `aprs.c` for decoding and display.
2. THE SDR_Receiver SHALL display each received packet with a timestamp in HH:MM:SS format, source callsign, destination callsign, and decoded position (latitude, longitude) when the packet contains an APRS position report.
3. WHEN a received AX.25 frame does not contain an APRS position report, THE SDR_Receiver SHALL display the raw info field content as text.
4. WHEN `--verbose` is specified, THE SDR_Receiver SHALL additionally display the raw frame bytes in hexadecimal format.

### Requirement 9: CLI Subcommand Integration

**User Story:** As a radio operator, I want an `sdr-recv` subcommand in the existing `kiss_interface` CLI, so that I can receive APRS packets via the B200 mini using the same tool I use for TNC-based operations and SDR beacon transmission.

#### Acceptance Criteria

1. THE SDR_Receiver SHALL be invocable as `kiss_interface sdr-recv` with the following options: `--freq` (default 144.800 MHz), `--gain` (default 50), `--sample-rate` (default 480000 Hz), and `--verbose`.
2. WHEN `--help` is specified, THE SDR_Receiver SHALL include the `sdr-recv` subcommand in the help output with a description of all its options.
3. WHEN SIGINT is received (Ctrl+C), THE SDR_Receiver SHALL stop reception, release the B200_Mini hardware via the UHD C API, and exit cleanly with code 0 within 1 second.
4. THE SDR_Receiver SHALL print a startup message to stdout indicating the receive frequency, gain, and sample rate.
5. THE SDR_Receiver SHALL print a summary line to stdout on exit showing the number of valid frames received.

### Requirement 10: Build System Integration

**User Story:** As a developer, I want the new RX modules to integrate into the existing Makefile build system, so that the project builds cleanly with or without the UHD library installed.

#### Acceptance Criteria

1. THE Makefile SHALL compile the new source files (`uhd_rx.c`, `afsk_demod.c`) as part of the main `kiss_interface` target.
2. THE Makefile SHALL detect the availability of libuhd at build time and conditionally compile the SDR receive support using the `HAVE_UHD` preprocessor flag, consistent with the existing SDR beacon build approach.
3. THE Makefile SHALL add test targets for the new modules: `test_uhd_rx` and `test_afsk_demod`, following the existing test naming convention.
4. THE new source files SHALL use C11 standard (gcc -std=c11) and compile cleanly with -Wall -Wextra, consistent with the existing codebase.

### Requirement 11: FM Demodulation Round-Trip Verification

**User Story:** As a developer, I want to verify that FM modulation followed by FM demodulation recovers the original audio signal, so that the receive path correctly inverts the transmit path.

#### Acceptance Criteria

1. FOR ALL audio signals consisting of sinusoidal tones at frequencies between 1000 Hz and 2500 Hz, FM modulation using the existing `uhd_tx_fm_modulate` function followed by FM demodulation using the new FM_Demodulator SHALL produce an output whose dominant frequency matches the input frequency within 50 Hz (FM modulation/demodulation round-trip property).
2. FOR ALL AFSK audio produced by the existing `afsk_modulate` function, FM modulation followed by FM demodulation SHALL preserve the mark/space tone structure sufficiently for the AFSK_Demodulator to recover the original NRZI bitstream (end-to-end audio round-trip property).

### Requirement 12: AFSK Demodulation Structural Properties

**User Story:** As a developer, I want to verify that the AFSK demodulator produces correctly structured output, so that the signal chain from audio to bits is demonstrably correct.

#### Acceptance Criteria

1. FOR ALL audio segments of length N × 40 samples (where N is 1 to 200), the AFSK_Demodulator SHALL produce exactly N output bits.
2. FOR ALL runs of 3 or more consecutive identical tones (all mark or all space), the AFSK_Demodulator SHALL correctly identify the tone for each bit in the run.
3. THE AFSK_Demodulator SHALL produce output bits with values restricted to 0 or 1.

### Requirement 13: Cross-Platform Compilation

**User Story:** As a developer, I want the new modules to compile on both x86_64 (Ubuntu) and ARM (Raspberry Pi), so that the receiver can be deployed on either platform.

#### Acceptance Criteria

1. THE new source files (`uhd_rx.c`, `afsk_demod.c`) SHALL use only C11 standard library functions, POSIX functions, and the UHD C API, with no platform-specific code.
2. THE new modules SHALL use static allocation only (no malloc/free), consistent with the existing codebase coding style.
3. THE new modules SHALL not depend on any external libraries other than libuhd, the C standard library, and the POSIX math library (libm).

### Requirement 14: Dual Polarity Tone Detection

**User Story:** As a radio operator, I want the receiver to handle both normal and inverted AFSK tone polarity, so that packets are decoded regardless of whether the FM chain inverts the frequency sense.

#### Acceptance Criteria

1. THE AFSK_Demodulator SHALL attempt decoding with both normal polarity (mark=1200 Hz, space=2200 Hz) and inverted polarity (mark=2200 Hz, space=1200 Hz).
2. WHEN a valid AX.25 frame (passing FCS verification) is found with one polarity, THE AFSK_Demodulator SHALL accept that frame and report the successful polarity in verbose mode.
3. THE dual polarity detection SHALL not require manual configuration; the receiver SHALL automatically determine the correct polarity from the received signal.
