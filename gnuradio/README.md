# GNU Radio APRS Beacon Transmitter

Software-defined APRS beacon transmitter using GNU Radio and an Ettus B200 mini SDR. Part of the AMSAT-UK DTN over terrestrial amateur radio project.

## Overview

Constructs AX.25 UI frames with APRS position reports, applies NRZI encoding, generates Bell 202 AFSK audio (1200 baud), FM-modulates, and transmits via the B200 mini on 144.850 MHz.

## Hardware

- Ettus Research USRP B200 mini (USB3)
- Antenna suitable for 2m VHF

## Dependencies

- Python 3.8+
- GNU Radio 3.10+ with gr-uhd
- UHD (USRP Hardware Driver)
- numpy

Install Python dependencies:

```bash
pip install -r requirements.txt
```

For testing:

```bash
pip install -r test-requirements.txt
```

## Usage

```bash
python3 aprs_beacon.py --callsign G4DPZ-1 --lat 52.467 --lon -2.022
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--callsign` | Source callsign-SSID (required) | — |
| `--lat` | Latitude in decimal degrees (required) | — |
| `--lon` | Longitude in decimal degrees (required) | — |
| `--comment` | APRS comment text | repo URL |
| `--interval` | Beacon interval in seconds | 120 |
| `--freq` | Transmit frequency in MHz | 144.850 |
| `--gain` | B200 transmit gain (0-89) | 50 |
| `--sample-rate` | SDR sample rate in Hz | 480000 |

### Example with all options

```bash
python3 aprs_beacon.py \
  --callsign G4DPZ-1 \
  --lat 52.467 \
  --lon -2.022 \
  --comment "AMSAT-UK DTN experiment" \
  --interval 120 \
  --freq 144.850 \
  --gain 50
```

## Running Tests

```bash
cd gnuradio/
python -m pytest tests/ -v
```

Tests for AX.25 framing, NRZI encoding, and AFSK modulation run without hardware. Flowgraph tests require GNU Radio + UHD and are skipped if not available.

## Receiving

Use the existing `kiss_interface` tool with a Mobilinkd TNC3 to receive and decode the APRS beacon:

```bash
./kiss_interface bp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 \
  --beacon --callsign G4DPZ-2 --lat 52.467 --lon -2.022
```

The built-in APRS decoder will display received beacons.

## Licence

Apache License 2.0. See [LICENSE](../LICENSE).
