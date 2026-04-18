# GNU Radio APRS Beacon — Lessons Learned

Notes on issues encountered during development and the fixes applied. Useful reference for future GNU Radio / AFSK / AX.25 work.

## 1. AX.25 SSID Byte Reserved Bits

The AX.25 address SSID byte (byte 7 of each address field) has reserved bits 5, 6, 7 that must all be set to 1. The correct mask is `0xE0`, not `0x60`.

- Wrong: `addr[6] = 0x60 | (ssid << 1) | ext`
- Right: `addr[6] = 0xE0 | (ssid << 1) | ext`

Without this, some decoders reject the frame as malformed.

## 2. Multiple Closing Flags Required

A single closing flag byte (0x7E) after the frame data is not sufficient for reliable decoding. Direwolf's `atest` and real TNC hardware need at least 2-3 closing flags to detect the end of frame.

- Wrong: 1 closing flag
- Right: 3 closing flags

## 3. NRZI Initial State

The NRZI encoder initial state determines whether the preamble flags produce mark (1200 Hz) or space (2200 Hz) tone during the run of six 1-bits in each flag byte (0x7E = 01111110 LSB first).

Receivers expect the preamble to be predominantly mark tone (1200 Hz). Starting NRZI at state 0 (space) means the first 0-bit toggles to mark, and the six 1-bits hold at mark — which is correct.

- Wrong: initial state = 1 (mark) → preamble is mostly space (2200 Hz)
- Right: initial state = 0 (space) → preamble is mostly mark (1200 Hz)

## 4. AFSK Tone Mapping Through FM

The NBFM TX block (or FM modulation in general) can invert the frequency sense of the AFSK tones. What goes in as 1200 Hz baseband may come out as a different deviation polarity after FM modulation and demodulation.

In our case, swapping the AFSK tone mapping in the modulator was needed:

- Original: NRZI 1 (mark) → 1200 Hz, NRZI 0 (space) → 2200 Hz
- Working:  NRZI 1 (mark) → 2200 Hz, NRZI 0 (space) → 1200 Hz

This is because the FM modulation + demodulation chain inverts the audio frequency relationship. The `atest` tool (direwolf) decodes both polarities, so WAV file testing doesn't catch this — it only shows up over the air.

## 5. Use frequency_modulator_fc Instead of nbfm_tx

The GNU Radio `analog.nbfm_tx` block includes pre-emphasis filtering and other voice-oriented processing that interferes with data signals. For AFSK packet radio:

- Wrong: `analog.nbfm_tx(tau=1e10, fh=-1.0, ...)` — pre-emphasis can't be fully disabled
- Right: `analog.frequency_modulator_fc(sensitivity)` — clean FM with no filtering

Sensitivity is calculated as: `2 * pi * max_deviation / sample_rate`

## 6. FM Deviation

The default FM deviation of 3500 Hz was too high. Standard amateur 1200 baud packet radio uses approximately 3000 Hz peak deviation. The `--deviation` CLI option was added to allow experimentation.

- Default that worked: 3000 Hz
- Too high: 3500 Hz (over-deviated, TNC couldn't demodulate)

## 7. B200 Mini TX Ramp-Up Time

The B200 mini needs significant silence padding before the AFSK signal begins, to allow the TX chain to stabilise. Without this, the start of the preamble is corrupted.

- Working values: 1 second of silence before and after the AFSK audio
- Also needed: 80 preamble flag bytes (~533ms of AFSK tones) to give the receiver time to sync after the B200 starts transmitting

## 8. B200 Mini Minimum Sample Rate

The Ettus B200 mini uses the AD9364 RFIC which has a practical minimum sample rate of approximately 250-500 kHz due to the BBPLL and decimation chain constraints. Setting the SDR sample rate below this causes underruns or UHD errors.

- Wrong: 240 kHz (right on the edge, unreliable)
- Right: 480 kHz (safe, clean 10:1 ratio from 48 kHz audio)

## 9. GNU Radio 3.10+ API Changes

Several GNU Radio API names changed between 3.8 and 3.10:

- `gr.vector_source_f` → `blocks.vector_source_f` (moved to `gnuradio.blocks`)
- `filter.rational_resampler_cc` → `filter.rational_resampler_ccc` (triple suffix)
- Need to import `blocks` module: `from gnuradio import blocks`

## 10. Preamble Flag Count

Standard TNCs typically send 300-500ms of preamble flags. At 1200 baud, each flag is ~6.67ms, so:

- 25 flags = ~167ms (too short for SDR TX)
- 50 flags = ~333ms (marginal)
- 80 flags = ~533ms (reliable with B200 mini)

The extra preamble compensates for the B200's TX ramp-up time eating into the initial flags.

## Summary of Working Configuration

```
Preamble:       80 flag bytes (0x7E) + 1s silence padding
Closing:        3 flag bytes
NRZI init:      state = 0 (space)
AFSK mapping:   NRZI 1 → 2200 Hz, NRZI 0 → 1200 Hz (inverted through FM)
FM modulator:   frequency_modulator_fc (not nbfm_tx)
FM deviation:   3000 Hz
SDR sample rate: 480 kHz
Audio rate:     48 kHz (40 samples/bit)
Resampler:      10:1 rational resampler
```
