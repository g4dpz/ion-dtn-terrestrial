# Design Document: Bundle Protocol over LTP over KISS

## Overview

A minimal BPv7 (RFC 9171) implementation for point-to-point bundle exchange over the existing LTP-over-KISS stack. Two new modules — `cbor.h`/`cbor.c` for CBOR serialization and `bp.h`/`bp.c` for the bundle agent — sit above the LTP engine. Bundles are CBOR-encoded, optionally fragmented, and submitted to LTP as client service data. The receiver reassembles fragments and delivers complete bundles.

### Key Design Decisions

- **Separate CBOR module**: CBOR encoding/decoding is isolated in `cbor.h`/`cbor.c` for independent testing and potential reuse. Only the subset needed for BPv7 is implemented (unsigned ints, byte strings, text strings, definite arrays, indefinite arrays).
- **No dynamic allocation**: All buffers are statically sized. Bundle encoding uses a fixed 1024-byte buffer per fragment. Reassembly uses a fixed 65536-byte buffer.
- **Sequential fragment transmission**: Fragments are sent one at a time, each as a separate LTP session. This is simple and works well for the half-duplex 1200 baud link.
- **Fragment reassembly by source EID + creation timestamp**: Per BPv7, fragments are identified by matching source EID, creation timestamp (time + sequence), and total ADU length.
- **CRC-16-CCITT on primary block only**: Keeps overhead minimal. Payload integrity is already covered by LTP's checkpoint/report mechanism.
- **DTN time**: Milliseconds since 2000-01-01 00:00:00 UTC. Stored as uint64_t.

## Architecture

```
Application ("Hello" or file contents)
        │
        ▼
  ┌─────────────┐
  │ bp.c        │  Bundle encode, fragment, reassemble
  │ Bundle Agent│
  └──────┬──────┘
         │ CBOR-encoded bundle (≤1024 bytes per fragment)
         ▼
  ┌─────────────┐
  │ ltp.c       │  Reliable delivery (existing, unchanged)
  │ LTP Engine  │
  └──────┬──────┘
         │ LTP segments
         ▼
  ┌─────────────┐
  │ kiss.c      │  KISS framing (existing, unchanged)
  └──────┬──────┘
         │
         ▼
      1200 baud RF
```

### Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| `cbor.h` / `cbor.c` | CBOR encode/decode: unsigned ints, byte strings, text strings, arrays |
| `bp.h` / `bp.c` | Bundle primary block, payload block, bundle encode/decode, fragmentation, reassembly, DTN time |
| `main.c` | CLI extensions: `bp-send`, `bp-recv` subcommands, file I/O |
| `ltp.c` | LTP engine (unchanged) |

## Components and Interfaces

### cbor.h

```c
#ifndef CBOR_H
#define CBOR_H

#include <stdint.h>
#include <stddef.h>

/* Encode a CBOR unsigned integer. Returns bytes written, or -1. */
int cbor_encode_uint(uint64_t value, uint8_t *out, size_t out_size);

/* Encode a CBOR byte string. Returns bytes written, or -1. */
int cbor_encode_bstr(const uint8_t *data, size_t len,
                     uint8_t *out, size_t out_size);

/* Encode a CBOR text string. Returns bytes written, or -1. */
int cbor_encode_tstr(const char *str, uint8_t *out, size_t out_size);

/* Encode a CBOR definite-length array header. Returns bytes written, or -1. */
int cbor_encode_array(uint64_t count, uint8_t *out, size_t out_size);

/* Encode CBOR indefinite-length array start (0x9F). Returns 1, or -1. */
int cbor_encode_indef_array_start(uint8_t *out, size_t out_size);

/* Encode CBOR break code (0xFF). Returns 1, or -1. */
int cbor_encode_break(uint8_t *out, size_t out_size);

/* Decode a CBOR unsigned integer. Returns bytes consumed, or -1. */
int cbor_decode_uint(const uint8_t *buf, size_t len, uint64_t *value);

/* Decode a CBOR byte string. Sets *data and *data_len to point into buf.
 * Returns total bytes consumed (header + data), or -1. */
int cbor_decode_bstr(const uint8_t *buf, size_t len,
                     const uint8_t **data, size_t *data_len);

/* Decode a CBOR text string. Sets *str and *str_len to point into buf.
 * Returns total bytes consumed, or -1. */
int cbor_decode_tstr(const uint8_t *buf, size_t len,
                     const char **str, size_t *str_len);

/* Decode a CBOR array header. Returns bytes consumed, sets *count. -1 on error. */
int cbor_decode_array(const uint8_t *buf, size_t len, uint64_t *count);

/* Check if next byte is indefinite array start (0x9F). Returns 1 if yes, 0 if no. */
int cbor_is_indef_array(const uint8_t *buf, size_t len);

/* Check if next byte is break code (0xFF). Returns 1 if yes, 0 if no. */
int cbor_is_break(const uint8_t *buf, size_t len);

#endif
```

### bp.h

```c
#ifndef BP_H
#define BP_H

#include <stdint.h>
#include <stddef.h>

#define BP_VERSION 7
#define BP_DTN_EPOCH 946684800ULL  /* Unix timestamp of 2000-01-01 00:00:00 UTC */
#define BP_MAX_EID_LEN 64
#define BP_MAX_PAYLOAD 65535
#define BP_MAX_BUNDLE_BUF 1024    /* Max encoded bundle/fragment size for LTP */
#define BP_MAX_FRAGMENTS 64
#define BP_DEFAULT_LIFETIME_SEC 3600
#define BP_DEFAULT_FRAGMENT_SIZE 900  /* Payload per fragment, leaving room for headers */

/* Bundle processing control flags (BPv7 Section 4.2.3) */
#define BP_FLAG_FRAGMENT       0x0001  /* Bundle is a fragment */
#define BP_FLAG_NO_FRAGMENT    0x0004  /* Bundle must not be fragmented */
#define BP_FLAG_DELETE_REPORT  0x0040  /* Request deletion status report */

/* Endpoint ID */
typedef struct {
    char uri[BP_MAX_EID_LEN];  /* e.g. "dtn://g4dpz-1/msg" */
} bp_eid_t;

/* Creation timestamp */
typedef struct {
    uint64_t time;      /* DTN time in milliseconds */
    uint64_t seq;       /* Sequence number */
} bp_timestamp_t;

/* Bundle primary block (decoded) */
typedef struct {
    uint64_t       flags;
    bp_eid_t       dst;
    bp_eid_t       src;
    bp_eid_t       report_to;
    bp_timestamp_t timestamp;
    uint64_t       lifetime_ms;
    uint64_t       fragment_offset;   /* Only if BP_FLAG_FRAGMENT set */
    uint64_t       total_adu_len;     /* Only if BP_FLAG_FRAGMENT set */
    uint8_t        crc_type;          /* 0=none, 1=CRC-16, 2=CRC-32 */
} bp_primary_t;

/* Bundle (decoded) */
typedef struct {
    bp_primary_t primary;
    const uint8_t *payload;      /* Points into decode buffer */
    size_t        payload_len;
} bp_bundle_t;

/* Fragment reassembly context */
typedef struct {
    int            active;
    bp_eid_t       src;
    bp_timestamp_t timestamp;
    uint64_t       total_adu_len;
    uint8_t        data[BP_MAX_PAYLOAD];
    uint8_t        received[BP_MAX_PAYLOAD]; /* 1 = byte received */
    size_t         bytes_received;
    uint64_t       lifetime_ms;
} bp_reassembly_t;

/* ---- DTN Time ---- */
uint64_t bp_dtn_time_now(void);
uint64_t bp_dtn_to_unix(uint64_t dtn_ms);

/* ---- EID ---- */
int bp_eid_encode(const bp_eid_t *eid, uint8_t *out, size_t out_size);
int bp_eid_decode(const uint8_t *buf, size_t len, bp_eid_t *eid);

/* ---- CRC-16-CCITT ---- */
uint16_t bp_crc16(const uint8_t *data, size_t len);

/* ---- Bundle Encoding ---- */
int bp_encode_bundle(const bp_eid_t *src, const bp_eid_t *dst,
                     const uint8_t *payload, size_t payload_len,
                     uint64_t lifetime_ms, uint64_t seq,
                     uint8_t *out, size_t out_size);

/* Encode a fragment bundle. Returns encoded size, or -1. */
int bp_encode_fragment(const bp_eid_t *src, const bp_eid_t *dst,
                       const uint8_t *payload, size_t payload_len,
                       uint64_t lifetime_ms, uint64_t seq,
                       uint64_t fragment_offset, uint64_t total_adu_len,
                       uint8_t *out, size_t out_size);

/* ---- Bundle Decoding ---- */
int bp_decode_bundle(const uint8_t *buf, size_t len, bp_bundle_t *bundle);

/* ---- Fragmentation ---- */
/* Returns number of fragments needed, or -1. */
int bp_fragment_count(size_t payload_len, size_t fragment_payload_size);

/* ---- Reassembly ---- */
void bp_reassembly_init(bp_reassembly_t *ctx);

/* Add a fragment. Returns 1 if complete, 0 if more needed, -1 on error. */
int bp_reassembly_add(bp_reassembly_t *ctx, const bp_bundle_t *fragment);

#endif
```

### main.c Extensions

```c
typedef enum {
    /* ... existing modes ... */
    CMD_MODE_BP_SEND,
    CMD_MODE_BP_RECV
} cmd_mode_t;

typedef struct {
    /* ... existing fields ... */
    const char *file_path;     /* --file for bp-send */
    const char *outdir;        /* --outdir for bp-recv */
    int         lifetime_sec;  /* --lifetime (default 3600) */
} cli_args_t;
```

## Data Models

### CBOR Encoding Summary

| Type | Major | Header | Example |
|------|-------|--------|---------|
| Unsigned int 0-23 | 0 | 1 byte | `0x07` = 7 |
| Unsigned int 24-255 | 0 | 2 bytes | `0x18 0x19` = 25 |
| Unsigned int 256-65535 | 0 | 3 bytes | `0x19 0x01 0x00` = 256 |
| Byte string | 2 | 1+ len bytes + data | `0x45` + 5 bytes |
| Text string | 3 | 1+ len bytes + data | `0x65` + "hello" |
| Array(n) | 4 | 1+ bytes | `0x84` = array of 4 |
| Indef array | 4 | `0x9F` | start |
| Break | 7 | `0xFF` | end indef |

### BPv7 Bundle Wire Format

```
Bundle (CBOR indefinite-length array):
┌──────┬───────────────┬───────────────┬───────┐
│ 0x9F │ Primary Block │ Payload Block │ 0xFF  │
│      │ (CBOR array)  │ (CBOR array)  │ break │
└──────┴───────────────┴───────────────┴───────┘

Primary Block (CBOR array of 8 or 10 items):
[version, flags, crc_type, dst_eid, src_eid, report_eid,
 [timestamp_time, timestamp_seq], lifetime_ms,
 fragment_offset?, total_adu_len?, crc_value]

EID encoding (dtn scheme):
[1, "//g4dpz-1/msg"]   (scheme_code=1 for dtn, SSP as text)

Payload Block (CBOR array of 5 items):
[block_type=1, block_number=1, flags=0, crc_type=0, payload_bstr]
```

### Fragment Example (4KB file, 900-byte fragments)

```
Original: 4000 bytes payload
Fragment 0: offset=0,    len=900,  total_adu=4000
Fragment 1: offset=900,  len=900,  total_adu=4000
Fragment 2: offset=1800, len=900,  total_adu=4000
Fragment 3: offset=2700, len=900,  total_adu=4000
Fragment 4: offset=3600, len=400,  total_adu=4000
```

Each fragment is a complete BPv7 bundle with the fragment flag set, encoded to ≤1024 bytes, submitted as a separate LTP block.

### DTN Time

```
DTN epoch: 2000-01-01 00:00:00 UTC = Unix 946684800
DTN time (ms) = (unix_time - 946684800) * 1000

Example: 2026-04-17 22:00:00 UTC
  Unix: 1776636000
  DTN:  (1776636000 - 946684800) * 1000 = 829951200000 ms
```

### CRC-16-CCITT

Standard CRC-16-CCITT with polynomial 0x1021, initial value 0xFFFF. Computed over the primary block bytes with the CRC field set to zero during computation.

## Correctness Properties

### Property 1: CBOR unsigned integer round-trip

*For any* unsigned integer value in [0, 2^64-1], encoding with `cbor_encode_uint` and decoding with `cbor_decode_uint` SHALL produce the original value.

**Validates: Requirements 1.1, 1.6, 9.1**

### Property 2: CBOR byte string round-trip

*For any* byte string of length 0 to 900, encoding with `cbor_encode_bstr` and decoding with `cbor_decode_bstr` SHALL produce the original byte string.

**Validates: Requirements 1.2, 1.6, 9.2**

### Property 3: CBOR text string round-trip

*For any* text string of length 0 to 256, encoding with `cbor_encode_tstr` and decoding with `cbor_decode_tstr` SHALL produce the original text string.

**Validates: Requirements 1.3, 1.6, 9.3**

### Property 4: Bundle encode/decode round-trip

*For any* valid source EID, destination EID, payload (0-900 bytes), lifetime, and sequence number, encoding a bundle with `bp_encode_bundle` and decoding with `bp_decode_bundle` SHALL produce a bundle with identical EIDs, timestamp, lifetime, and payload.

**Validates: Requirements 4.5, 9.4**

### Property 5: Fragment/reassembly round-trip

*For any* payload of size 1 to 65535 bytes, fragmenting into fragments of at most 900 bytes, encoding each fragment with `bp_encode_fragment`, decoding with `bp_decode_bundle`, and reassembling with `bp_reassembly_add` SHALL produce the original payload.

**Validates: Requirements 10.4, 11.6**

### Property 6: DTN time round-trip

*For any* Unix timestamp after 2000-01-01, converting to DTN time with `bp_dtn_time_now` logic and back with `bp_dtn_to_unix` SHALL produce the original Unix timestamp.

**Validates: Requirements 7.4**

### Property 7: CRC-16 determinism

*For any* byte sequence, computing `bp_crc16` twice SHALL produce the same result.

**Validates: Requirements 2.6**

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Payload > 65535 bytes | `bp-send` prints error, exits 1 |
| File not found / unreadable | `bp-send` prints error, exits 1 |
| Encoded bundle > 1024 bytes (unfragmented) | Auto-fragment |
| Fragment count > 64 | `bp-send` prints error, exits 1 |
| Bundle version != 7 | `bp_decode_bundle` returns -1 |
| CRC mismatch | `bp_decode_bundle` returns -1 |
| Expired bundle received | Discard, log warning |
| CBOR decode error | `bp_decode_bundle` returns -1 |
| Outdir doesn't exist | `bp-recv` prints error, exits 1 |
| Missing --local or --remote | Print error, exit 1 |
| SIGINT/SIGTERM | Exit cleanly, print summary |

## Testing Strategy

### Property-Based Tests

| Test File | Test | Property | Iterations |
|-----------|------|----------|------------|
| `test_cbor.c` | `test_cbor_uint_roundtrip` | Property 1 | 1000 |
| `test_cbor.c` | `test_cbor_bstr_roundtrip` | Property 2 | 1000 |
| `test_cbor.c` | `test_cbor_tstr_roundtrip` | Property 3 | 1000 |
| `test_bp.c` | `test_bundle_roundtrip` | Property 4 | 1000 |
| `test_bp.c` | `test_fragment_reassembly_roundtrip` | Property 5 | 100 |
| `test_bp.c` | `test_dtn_time_roundtrip` | Property 6 | 1000 |
| `test_bp.c` | `test_crc16_determinism` | Property 7 | 1000 |

### Unit Tests

| Test | Validates |
|------|-----------|
| CBOR encode uint 0 → `[0x00]` | Req 1.1 |
| CBOR encode uint 23 → `[0x17]` | Req 1.1 |
| CBOR encode uint 24 → `[0x18, 0x18]` | Req 1.1 |
| CBOR encode uint 256 → `[0x19, 0x01, 0x00]` | Req 1.1 |
| CBOR decode truncated → -1 | Req 1.7 |
| Bundle version = 7 in encoded output | Req 2.2 |
| EID "dtn://g4dpz-1/msg" encodes as [1, "//g4dpz-1/msg"] | Req 2.3 |
| Fragment flag set in fragment bundle | Req 10.2 |
| Fragment offset and total ADU length correct | Req 10.2 |
| Reassembly of 3 fragments produces original | Req 11.2 |
| Expired fragment discarded | Req 11.5 |
| DTN epoch = 946684800 | Req 7.1 |

### Build Integration

```makefile
SRC = main.c kiss.c ax25.c serial.c ping.c ltp.c sdnv.c beacon.c aprs.c cbor.c bp.c

test_cbor: test_cbor.c cbor.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_bp: test_bp.c bp.c cbor.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)
```
