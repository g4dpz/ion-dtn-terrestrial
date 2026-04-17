# Design Document: APRS Decode

## Overview

A new `aprs.h`/`aprs.c` module that decodes APRS position reports from received AX.25 UI frames, plus a frame classifier function that routes incoming KISS payloads to either the APRS decoder (for AX.25 frames) or the LTP engine (for LTP segments). The classifier is integrated into the existing event loops in `main.c` with minimal changes.

### Key Design Decisions

- **Frame classification by structure, not by port**: Since AX.25 and LTP share the same KISS data port (command byte 0x00), classification is based on the payload structure. AX.25 UI frames have control=0x03 at byte 14 and PID=0xF0 at byte 15 with minimum 16 bytes. LTP segments have a version/type byte at offset 0 where the upper nibble is 0 (version 0). In practice, any payload ≥16 bytes with bytes [14]=0x03 and [15]=0xF0 is AX.25; everything else goes to LTP.
- **Reuse `ax25_strip_frame`**: The existing function already validates and parses AX.25 headers. The APRS decoder only needs to parse the information field.
- **Decode what we can, log the rest**: Position types (`!`, `=`, `/`, `@`) are fully parsed. Other APRS types (messages, telemetry, etc.) are logged as raw text.
- **No state**: The decoder is stateless — each packet is decoded independently. No station database or duplicate detection.

## Architecture

```
Received KISS payload
        │
        ▼
  ┌─────────────┐
  │ classify_    │
  │ frame()      │
  └──────┬──────┘
         │
    ┌────┴────┐
    │         │
  AX.25?    No
    │         │
    ▼         ▼
  aprs.c    ltp.c
  decode    process
  & log     segment
```

### Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| `aprs.h` / `aprs.c` | APRS position parsing from info field, frame classification helper |
| `main.c` | Integration: call classifier in event loops, route to APRS or LTP |
| `ax25.c` | AX.25 strip (unchanged) |
| `ltp.c` | LTP segment processing (unchanged) |

## Components and Interfaces

### aprs.h

```c
#ifndef APRS_H
#define APRS_H

#include <stdint.h>
#include <stddef.h>

/* Decoded APRS position */
typedef struct {
    double lat;           /* Decimal degrees, -90 to +90 */
    double lon;           /* Decimal degrees, -180 to +180 */
    char   symbol_table;  /* '/' or '\\' */
    char   symbol_code;   /* e.g. '-' for house */
    char   comment[128];  /* Trailing comment text */
    int    has_position;  /* 1 if position was successfully parsed */
} aprs_position_t;

/* Classify a KISS payload: returns 1 if AX.25 UI frame, 0 if not.
 * Checks: len >= 16, byte[14] == 0x03, byte[15] == 0xF0. */
int aprs_is_ax25_frame(const uint8_t *payload, size_t len);

/* Parse APRS position from an AX.25 info field.
 * Handles data types: '!' '=' '/' '@'
 * Returns 0 on success (position parsed), -1 if not a position packet.
 * Always sets comment field if any trailing text exists. */
int aprs_decode_position(const uint8_t *info, size_t info_len,
                         aprs_position_t *pos);

/* Log a received APRS packet to stdout.
 * Strips AX.25 header, attempts position decode, prints formatted output.
 * verbose: if 1, also prints hex dump of raw frame. */
void aprs_log_packet(const uint8_t *ax25_frame, size_t frame_len,
                     int verbose);

#endif
```

## Data Models

### APRS Data Type Identifiers (first byte of info field)

| Byte | Type | Parsing |
|------|------|---------|
| `!` | Position (no timestamp, no messaging) | Full decode |
| `=` | Position (no timestamp, with messaging) | Full decode |
| `/` | Position (with timestamp, no messaging) | Skip 7-byte timestamp, then decode |
| `@` | Position (with timestamp, with messaging) | Skip 7-byte timestamp, then decode |
| Other | Status, message, telemetry, etc. | Log as raw text |

### Position Parsing (uncompressed format)

```
Info field for '!' or '=':
!DDMM.MMN/DDDMM.MMW-comment
│        │ │         │
│  lat   │ │  lon    │
│        │ │         └ symbol code + comment
│        │ └ symbol table
└ data type

Info field for '/' or '@':
/DDHHMMzDDMM.MMN/DDDMM.MMW-comment
│       │        │ │         │
│ time  │  lat   │ │  lon    │
│ stamp │        │ │         └ symbol code + comment
│       │        │ └ symbol table
└ data  └ 7 chars└ same as above
  type
```

### Frame Classification Logic

```c
int aprs_is_ax25_frame(const uint8_t *payload, size_t len)
{
    if (len < 16) return 0;
    if (payload[14] != 0x03) return 0;  /* Not UI control */
    if (payload[15] != 0xF0) return 0;  /* Not no-layer-3 PID */
    return 1;
}
```

### Integration Points in main.c

In every event loop that reads KISS frames, replace:
```c
/* Before: */
ltp_process_segment(&eng, fd, kiss_payload, kiss_payload_len);

/* After: */
if (aprs_is_ax25_frame(kiss_payload, kiss_payload_len)) {
    aprs_log_packet(kiss_payload, kiss_payload_len, verbose);
} else {
    ltp_process_segment(&eng, fd, kiss_payload, kiss_payload_len);
}
```

This applies to:
- `cmd_ltp_recv` (non-beacon path via `ltp_engine_run` — needs a hook or inline change)
- LTP recv with beacon (custom event loop in main.c dispatch)
- LTP send with beacon (custom event loop in main.c dispatch)

For `ltp_engine_run`, the cleanest approach is to add a frame classifier callback to the engine, or simply modify the custom event loops in main.c (which already exist for beacon mode). For the non-beacon `ltp_engine_run` path, we can add a pre-process hook.

## Correctness Properties

### Property 1: Frame classification correctness

*For any* byte buffer, `aprs_is_ax25_frame` SHALL return 1 if and only if the buffer has length ≥ 16, byte[14] == 0x03, and byte[15] == 0xF0.

**Validates: Requirements 1.1, 1.2, 1.3**

### Property 2: Position decode round-trip with beacon encoder

*For any* valid latitude in [-90, +90], longitude in [-180, +180], and comment string, building a position with `beacon_build_position` and then parsing it with `aprs_decode_position` SHALL recover coordinates within 0.02 arcminutes of the original.

**Validates: Requirements 5.5**

### Property 3: Malformed input resilience

*For any* arbitrary byte sequence of length 0 to 256, calling `aprs_decode_position` SHALL NOT crash and SHALL return -1 for sequences that don't match a valid APRS position format.

**Validates: Requirements 2.5**

## Error Handling

| Condition | Behavior |
|-----------|----------|
| KISS payload < 16 bytes | Classified as non-AX.25, passed to LTP |
| AX.25 frame with unsupported APRS type | Log raw info as text, continue |
| Position string too short | `aprs_decode_position` returns -1, log raw |
| Invalid lat/lon in position string | `aprs_decode_position` returns -1, log raw |
| Empty info field | Log source/dst callsigns only |

## Testing Strategy

### Property-Based Tests

| Test | Property | Iterations |
|------|----------|------------|
| `test_frame_classification` | Property 1 | 1000 |
| `test_position_decode_roundtrip` | Property 2 | 1000 |
| `test_malformed_input_resilience` | Property 3 | 1000 |

### Unit Tests

| Test | Validates |
|------|-----------|
| Classify valid AX.25 frame → returns 1 | Req 1.1 |
| Classify LTP segment → returns 0 | Req 1.3 |
| Classify short buffer (< 16 bytes) → returns 0 | Req 1.1 |
| Decode `!5228.02N/00201.32W-comment` → lat=52.467, lon=-2.022 | Req 2.1, 5.1, 5.2 |
| Decode `=` position type | Req 2.2 |
| Decode `/` position with timestamp | Req 2.3 |
| Decode unknown type → returns -1 | Req 2.4 |
| Empty info field → returns -1 | Req 2.5 |

### Build Integration

```makefile
SRC = main.c kiss.c ax25.c serial.c ping.c ltp.c sdnv.c beacon.c aprs.c

test_aprs: test_aprs.c aprs.c ax25.c kiss.c beacon.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm
```
