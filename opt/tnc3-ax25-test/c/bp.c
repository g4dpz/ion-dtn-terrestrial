/*
 * bp.c - Minimal Bundle Protocol v7 (RFC 9171) implementation
 *
 * BPv7 uses CBOR (RFC 8949) encoding. This implements just enough
 * CBOR to encode/decode simple bundles for testing over RF.
 *
 * Bundle structure (CBOR):
 *   Indefinite-length array [
 *     Primary Block (array),
 *     Payload Block (array),
 *     ... (other extension blocks)
 *     CBOR break
 *   ]
 *
 * Primary Block (array of 8 items):
 *   [version, flags, crc_type, dest_eid, src_eid, report_eid,
 *    creation_timestamp, lifetime]
 *
 * EID encoding: [scheme, ssp]
 *   dtn scheme (1): [1, text-string]
 *   dtn:none:        [1, 0]
 *
 * Creation timestamp: [time, sequence]
 *
 * Payload Block (array of 5-6 items):
 *   [block_type, block_number, flags, crc_type, data]
 */

#include "bp.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ── Minimal CBOR encoder ── */

/* CBOR major types */
#define CBOR_UINT     0
#define CBOR_NEGINT   1
#define CBOR_BSTR     2
#define CBOR_TSTR     3
#define CBOR_ARRAY    4
#define CBOR_MAP      5
#define CBOR_TAG      6
#define CBOR_SIMPLE   7

#define CBOR_BREAK    0xFF
#define CBOR_INDEF_ARRAY 0x9F  /* indefinite-length array */

static int cbor_encode_head(uint8_t *buf, size_t cap, uint8_t major, uint64_t val)
{
    uint8_t mt = major << 5;
    if (val <= 23) {
        if (cap < 1) return -1;
        buf[0] = mt | (uint8_t)val;
        return 1;
    } else if (val <= 0xFF) {
        if (cap < 2) return -1;
        buf[0] = mt | 24;
        buf[1] = (uint8_t)val;
        return 2;
    } else if (val <= 0xFFFF) {
        if (cap < 3) return -1;
        buf[0] = mt | 25;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = val & 0xFF;
        return 3;
    } else if (val <= 0xFFFFFFFF) {
        if (cap < 5) return -1;
        buf[0] = mt | 26;
        buf[1] = (val >> 24) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
        buf[3] = (val >> 8) & 0xFF;
        buf[4] = val & 0xFF;
        return 5;
    } else {
        if (cap < 9) return -1;
        buf[0] = mt | 27;
        for (int i = 7; i >= 0; i--)
            buf[8 - i] = (val >> (i * 8)) & 0xFF;
        return 9;
    }
}

static int cbor_encode_uint(uint8_t *buf, size_t cap, uint64_t val)
{
    return cbor_encode_head(buf, cap, CBOR_UINT, val);
}

static int cbor_encode_tstr(uint8_t *buf, size_t cap, const char *str)
{
    size_t len = strlen(str);
    int hlen = cbor_encode_head(buf, cap, CBOR_TSTR, len);
    if (hlen < 0 || (size_t)hlen + len > cap) return -1;
    memcpy(buf + hlen, str, len);
    return hlen + (int)len;
}

static int cbor_encode_bstr(uint8_t *buf, size_t cap, const uint8_t *data, size_t len)
{
    int hlen = cbor_encode_head(buf, cap, CBOR_BSTR, len);
    if (hlen < 0 || (size_t)hlen + len > cap) return -1;
    memcpy(buf + hlen, data, len);
    return hlen + (int)len;
}

static int cbor_encode_array(uint8_t *buf, size_t cap, uint64_t count)
{
    return cbor_encode_head(buf, cap, CBOR_ARRAY, count);
}

/* Encode a dtn:// EID as [1, "//authority/path"] or [1, 0] for dtn:none */
static int encode_eid(uint8_t *buf, size_t cap, const char *eid)
{
    int idx = 0, n;

    n = cbor_encode_array(buf + idx, cap - idx, 2);
    if (n < 0) return -1;
    idx += n;

    if (strcmp(eid, "dtn:none") == 0) {
        /* dtn:none → [1, 0] */
        n = cbor_encode_uint(buf + idx, cap - idx, 1);
        if (n < 0) return -1;
        idx += n;
        n = cbor_encode_uint(buf + idx, cap - idx, 0);
        if (n < 0) return -1;
        idx += n;
    } else {
        /* dtn scheme = 1 */
        n = cbor_encode_uint(buf + idx, cap - idx, 1);
        if (n < 0) return -1;
        idx += n;

        /* SSP: strip "dtn:" prefix, keep the rest (e.g. "//g4dpz-1/demo") */
        const char *ssp = eid;
        if (strncmp(eid, "dtn:", 4) == 0) ssp = eid + 4;
        n = cbor_encode_tstr(buf + idx, cap - idx, ssp);
        if (n < 0) return -1;
        idx += n;
    }
    return idx;
}

/* ── Minimal CBOR decoder ── */

static int cbor_decode_head(const uint8_t *buf, size_t len, uint8_t *major, uint64_t *val)
{
    if (len < 1) return -1;
    *major = buf[0] >> 5;
    uint8_t ai = buf[0] & 0x1F;

    if (ai <= 23) {
        *val = ai;
        return 1;
    } else if (ai == 24) {
        if (len < 2) return -1;
        *val = buf[1];
        return 2;
    } else if (ai == 25) {
        if (len < 3) return -1;
        *val = ((uint64_t)buf[1] << 8) | buf[2];
        return 3;
    } else if (ai == 26) {
        if (len < 5) return -1;
        *val = ((uint64_t)buf[1] << 24) | ((uint64_t)buf[2] << 16) |
               ((uint64_t)buf[3] << 8) | buf[4];
        return 5;
    } else if (ai == 27) {
        if (len < 9) return -1;
        *val = 0;
        for (int i = 0; i < 8; i++)
            *val = (*val << 8) | buf[1 + i];
        return 9;
    } else if (ai == 31) {
        /* indefinite length or break */
        *val = 0;
        return 1;
    }
    return -1;
}

/* Skip one CBOR item (recursively for arrays/maps) */
static int cbor_skip(const uint8_t *buf, size_t len)
{
    uint8_t major;
    uint64_t val;
    int hlen = cbor_decode_head(buf, len, &major, &val);
    if (hlen < 0) return -1;

    switch (major) {
    case CBOR_UINT:
    case CBOR_NEGINT:
    case CBOR_SIMPLE:
        return hlen;
    case CBOR_BSTR:
    case CBOR_TSTR:
        if ((size_t)hlen + val > len) return -1;
        return hlen + (int)val;
    case CBOR_ARRAY: {
        int off = hlen;
        for (uint64_t i = 0; i < val; i++) {
            int n = cbor_skip(buf + off, len - off);
            if (n < 0) return -1;
            off += n;
        }
        return off;
    }
    case CBOR_MAP: {
        int off = hlen;
        for (uint64_t i = 0; i < val * 2; i++) {
            int n = cbor_skip(buf + off, len - off);
            if (n < 0) return -1;
            off += n;
        }
        return off;
    }
    case CBOR_TAG: {
        int n = cbor_skip(buf + hlen, len - hlen);
        if (n < 0) return -1;
        return hlen + n;
    }
    }
    return -1;
}

/* Decode a dtn EID: [scheme, ssp] */
static int decode_eid(const uint8_t *buf, size_t len, char *eid, size_t eid_size)
{
    uint8_t major;
    uint64_t val;
    int off = 0, n;

    /* Array of 2 */
    n = cbor_decode_head(buf + off, len - off, &major, &val);
    if (n < 0 || major != CBOR_ARRAY || val != 2) return -1;
    off += n;

    /* Scheme number */
    uint64_t scheme;
    n = cbor_decode_head(buf + off, len - off, &major, &scheme);
    if (n < 0 || major != CBOR_UINT) return -1;
    off += n;

    /* SSP */
    n = cbor_decode_head(buf + off, len - off, &major, &val);
    if (n < 0) return -1;

    if (major == CBOR_UINT && val == 0) {
        /* dtn:none */
        snprintf(eid, eid_size, "dtn:none");
        off += n;
    } else if (major == CBOR_TSTR) {
        if ((size_t)off + n + val > len || val >= eid_size - 4) return -1;
        snprintf(eid, eid_size, "dtn:%.*s", (int)val, (const char *)(buf + off + n));
        off += n + (int)val;
    } else {
        return -1;
    }
    return off;
}

/* ── BPv7 Bundle Encoding ── */

void create_simple_bundle(bp_bundle_t *bundle, const char *dest, const char *src,
                          const uint8_t *payload, size_t payload_len)
{
    memset(bundle, 0, sizeof(bp_bundle_t));

    bundle->version = BP_VERSION;
    bundle->flags = 0;
    bundle->crc_type = BP_CRC_NONE;

    strncpy(bundle->dest_eid, dest, sizeof(bundle->dest_eid) - 1);
    strncpy(bundle->src_eid, src, sizeof(bundle->src_eid) - 1);
    strncpy(bundle->report_eid, "dtn:none", sizeof(bundle->report_eid) - 1);

    /* DTN time: milliseconds since 2000-01-01 00:00:00 UTC */
    time_t now = time(NULL);
    bundle->creation_time = (uint64_t)(now - 946684800) * 1000;
    bundle->creation_seq = 0;
    bundle->lifetime = 3600000; /* 1 hour in ms */

    if (payload_len > sizeof(bundle->payload))
        payload_len = sizeof(bundle->payload);
    memcpy(bundle->payload, payload, payload_len);
    bundle->payload_len = payload_len;
}

int encode_bp_bundle(const bp_bundle_t *bundle, uint8_t *output, size_t output_size)
{
    int idx = 0, n;
    uint8_t *buf = output;
    size_t cap = output_size;

    /* Outer: indefinite-length array */
    if (cap < 1) return -1;
    buf[idx++] = CBOR_INDEF_ARRAY;

    /* ── Primary Block: array of 8 ── */
    n = cbor_encode_array(buf + idx, cap - idx, 8);
    if (n < 0) return -1;
    idx += n;

    /* 0: version */
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->version);
    if (n < 0) return -1;
    idx += n;

    /* 1: bundle processing control flags */
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->flags);
    if (n < 0) return -1;
    idx += n;

    /* 2: CRC type */
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->crc_type);
    if (n < 0) return -1;
    idx += n;

    /* 3: destination EID */
    n = encode_eid(buf + idx, cap - idx, bundle->dest_eid);
    if (n < 0) return -1;
    idx += n;

    /* 4: source EID */
    n = encode_eid(buf + idx, cap - idx, bundle->src_eid);
    if (n < 0) return -1;
    idx += n;

    /* 5: report-to EID */
    n = encode_eid(buf + idx, cap - idx, bundle->report_eid);
    if (n < 0) return -1;
    idx += n;

    /* 6: creation timestamp [time, sequence] */
    n = cbor_encode_array(buf + idx, cap - idx, 2);
    if (n < 0) return -1;
    idx += n;
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->creation_time);
    if (n < 0) return -1;
    idx += n;
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->creation_seq);
    if (n < 0) return -1;
    idx += n;

    /* 7: lifetime (ms) */
    n = cbor_encode_uint(buf + idx, cap - idx, bundle->lifetime);
    if (n < 0) return -1;
    idx += n;

    /* ── Payload Block: array of 5 ── */
    n = cbor_encode_array(buf + idx, cap - idx, 5);
    if (n < 0) return -1;
    idx += n;

    /* 0: block type (1 = payload) */
    n = cbor_encode_uint(buf + idx, cap - idx, BP_BLOCK_PAYLOAD);
    if (n < 0) return -1;
    idx += n;

    /* 1: block number */
    n = cbor_encode_uint(buf + idx, cap - idx, 1);
    if (n < 0) return -1;
    idx += n;

    /* 2: block processing control flags */
    n = cbor_encode_uint(buf + idx, cap - idx, 0);
    if (n < 0) return -1;
    idx += n;

    /* 3: CRC type */
    n = cbor_encode_uint(buf + idx, cap - idx, BP_CRC_NONE);
    if (n < 0) return -1;
    idx += n;

    /* 4: block-type-specific data (byte string) */
    n = cbor_encode_bstr(buf + idx, cap - idx, bundle->payload, bundle->payload_len);
    if (n < 0) return -1;
    idx += n;

    /* Close indefinite-length array */
    if ((size_t)idx >= cap) return -1;
    buf[idx++] = CBOR_BREAK;

    return idx;
}

/* ── BPv7 Bundle Decoding ── */

int decode_bp_bundle(const uint8_t *data, size_t data_len, bp_bundle_t *bundle)
{
    memset(bundle, 0, sizeof(bp_bundle_t));

    size_t off = 0;
    uint8_t major;
    uint64_t val;
    int n;

    /* Outer array: could be definite or indefinite */
    if (off >= data_len) return -1;
    int indefinite = (data[off] == CBOR_INDEF_ARRAY);
    if (indefinite) {
        off++;
    } else {
        n = cbor_decode_head(data + off, data_len - off, &major, &val);
        if (n < 0 || major != CBOR_ARRAY) return -1;
        off += n;
    }

    /* ── Primary Block ── */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_ARRAY || val < 8) return -1;
    off += n;

    /* 0: version */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->version = (uint8_t)val;
    off += n;

    /* 1: flags */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->flags = (uint32_t)val;
    off += n;

    /* 2: CRC type */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->crc_type = (uint32_t)val;
    off += n;

    /* 3: destination EID */
    n = decode_eid(data + off, data_len - off, bundle->dest_eid, sizeof(bundle->dest_eid));
    if (n < 0) return -1;
    off += n;

    /* 4: source EID */
    n = decode_eid(data + off, data_len - off, bundle->src_eid, sizeof(bundle->src_eid));
    if (n < 0) return -1;
    off += n;

    /* 5: report-to EID */
    n = decode_eid(data + off, data_len - off, bundle->report_eid, sizeof(bundle->report_eid));
    if (n < 0) return -1;
    off += n;

    /* 6: creation timestamp [time, seq] */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_ARRAY || val != 2) return -1;
    off += n;

    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->creation_time = val;
    off += n;

    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->creation_seq = val;
    off += n;

    /* 7: lifetime */
    n = cbor_decode_head(data + off, data_len - off, &major, &val);
    if (n < 0 || major != CBOR_UINT) return -1;
    bundle->lifetime = val;
    off += n;

    /* ── Scan for Payload Block ── */
    while (off < data_len) {
        /* Check for break (end of indefinite array) */
        if (data[off] == CBOR_BREAK) break;

        /* Each block is an array */
        n = cbor_decode_head(data + off, data_len - off, &major, &val);
        if (n < 0 || major != CBOR_ARRAY || val < 5) break;
        off += n;

        /* block type */
        uint64_t block_type;
        n = cbor_decode_head(data + off, data_len - off, &major, &block_type);
        if (n < 0) break;
        off += n;

        /* block number */
        n = cbor_skip(data + off, data_len - off);
        if (n < 0) break;
        off += n;

        /* block flags */
        n = cbor_skip(data + off, data_len - off);
        if (n < 0) break;
        off += n;

        /* CRC type */
        n = cbor_skip(data + off, data_len - off);
        if (n < 0) break;
        off += n;

        /* block data (byte string) */
        n = cbor_decode_head(data + off, data_len - off, &major, &val);
        if (n < 0) break;

        if (block_type == BP_BLOCK_PAYLOAD && major == CBOR_BSTR) {
            bundle->payload_len = val;
            if (bundle->payload_len > sizeof(bundle->payload))
                bundle->payload_len = sizeof(bundle->payload);
            if (off + n + bundle->payload_len > data_len) return -1;
            memcpy(bundle->payload, data + off + n, bundle->payload_len);
            off += n + (int)val;
            break;
        } else {
            /* Skip this block's data */
            off += n + (int)val;
        }
    }

    return 0;
}
