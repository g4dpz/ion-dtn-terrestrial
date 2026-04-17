#define _POSIX_C_SOURCE 200809L

#include "bp.h"
#include "cbor.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ---- DTN Time ---- */

uint64_t bp_dtn_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if ((uint64_t)ts.tv_sec < BP_DTN_EPOCH) return 0;
    return ((uint64_t)ts.tv_sec - BP_DTN_EPOCH) * 1000 +
           (uint64_t)(ts.tv_nsec / 1000000);
}

uint64_t bp_dtn_to_unix(uint64_t dtn_ms)
{
    return BP_DTN_EPOCH + dtn_ms / 1000;
}

/* ---- CRC-16-CCITT ---- */

uint16_t bp_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

/* ---- EID encode/decode ---- */
/* dtn scheme: CBOR array [1, "//callsign/svc"] */

int bp_eid_encode(const bp_eid_t *eid, uint8_t *out, size_t out_size)
{
    if (!eid || !out) return -1;
    size_t pos = 0;
    int n;

    /* The SSP is everything after "dtn:" */
    const char *ssp = eid->uri;
    if (strncmp(ssp, "dtn:", 4) == 0) ssp += 4;

    n = cbor_encode_array(2, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(1, out + pos, out_size - pos); /* scheme=dtn */
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_tstr(ssp, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    return (int)pos;
}

int bp_eid_decode(const uint8_t *buf, size_t len, bp_eid_t *eid)
{
    if (!buf || !eid) return -1;
    size_t pos = 0;
    int n;

    uint64_t count;
    n = cbor_decode_array(buf + pos, len - pos, &count);
    if (n < 0 || count != 2) return -1;
    pos += (size_t)n;

    uint64_t scheme;
    n = cbor_decode_uint(buf + pos, len - pos, &scheme);
    if (n < 0) return -1;
    pos += (size_t)n;

    if (scheme == 1) {
        /* dtn scheme */
        const char *ssp; size_t ssp_len;
        n = cbor_decode_tstr(buf + pos, len - pos, &ssp, &ssp_len);
        if (n < 0) return -1;
        pos += (size_t)n;

        if (ssp_len + 4 >= BP_MAX_EID_LEN) return -1;
        memcpy(eid->uri, "dtn:", 4);
        memcpy(eid->uri + 4, ssp, ssp_len);
        eid->uri[4 + ssp_len] = '\0';
    } else {
        return -1; /* unsupported scheme */
    }

    return (int)pos;
}

/* ---- Internal: encode primary block ---- */

static int encode_primary(const bp_eid_t *src, const bp_eid_t *dst,
                          uint64_t flags, uint64_t lifetime_ms, uint64_t seq,
                          uint64_t creation_time,
                          uint64_t frag_offset, uint64_t total_adu,
                          uint8_t *out, size_t out_size)
{
    size_t pos = 0;
    int n;
    int is_frag = (flags & BP_FLAG_FRAGMENT) ? 1 : 0;
    uint64_t num_fields = is_frag ? 11 : 9; /* +2 for frag fields, +1 for CRC value */

    /* Actually BPv7 primary block fields:
     * version, flags, crc_type, dst, src, report_to, timestamp, lifetime
     * [fragment_offset, total_adu_len] (if fragment)
     * crc_value
     * = 8 + (2 if frag) + 1 crc = 9 or 11 */

    n = cbor_encode_array(num_fields, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* version */
    n = cbor_encode_uint(BP_VERSION, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* flags */
    n = cbor_encode_uint(flags, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* CRC type: 1 = CRC-16 */
    n = cbor_encode_uint(1, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* destination EID */
    n = bp_eid_encode(dst, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* source EID */
    n = bp_eid_encode(src, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* report-to EID (same as source) */
    n = bp_eid_encode(src, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* creation timestamp [time, seq] */
    n = cbor_encode_array(2, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(creation_time, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(seq, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* lifetime */
    n = cbor_encode_uint(lifetime_ms, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    /* fragment fields */
    if (is_frag) {
        n = cbor_encode_uint(frag_offset, out + pos, out_size - pos);
        if (n < 0) return -1; pos += (size_t)n;
        n = cbor_encode_uint(total_adu, out + pos, out_size - pos);
        if (n < 0) return -1; pos += (size_t)n;
    }

    /* CRC-16: compute over primary block bytes with CRC field as 0x0000 */
    /* Reserve 3 bytes for CRC bstr: 0x42 + 2 bytes */
    if (pos + 3 > out_size) return -1;
    size_t crc_pos = pos;
    out[pos++] = 0x42; /* CBOR bstr len 2 */
    out[pos++] = 0x00;
    out[pos++] = 0x00;

    uint16_t crc = bp_crc16(out, pos); /* CRC over entire primary block including zero CRC */
    out[crc_pos + 1] = (uint8_t)(crc >> 8);
    out[crc_pos + 2] = (uint8_t)(crc & 0xFF);

    return (int)pos;
}

/* ---- Internal: encode payload block ---- */

static int encode_payload_block(const uint8_t *payload, size_t payload_len,
                                uint8_t *out, size_t out_size)
{
    size_t pos = 0;
    int n;

    /* [block_type=1, block_number=1, flags=0, crc_type=0, payload_bstr] */
    n = cbor_encode_array(5, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(1, out + pos, out_size - pos); /* type */
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(1, out + pos, out_size - pos); /* number */
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(0, out + pos, out_size - pos); /* flags */
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_uint(0, out + pos, out_size - pos); /* crc_type=none */
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_encode_bstr(payload, payload_len, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    return (int)pos;
}

/* ---- Public API ---- */

int bp_encode_bundle(const bp_eid_t *src, const bp_eid_t *dst,
                     const uint8_t *payload, size_t payload_len,
                     uint64_t lifetime_ms, uint64_t seq,
                     uint8_t *out, size_t out_size)
{
    size_t pos = 0;
    int n;

    n = cbor_encode_indef_array_start(out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = encode_primary(src, dst, 0, lifetime_ms, seq, bp_dtn_time_now(), 0, 0, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = encode_payload_block(payload, payload_len, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = cbor_encode_break(out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    return (int)pos;
}

int bp_encode_fragment(const bp_eid_t *src, const bp_eid_t *dst,
                       const uint8_t *payload, size_t payload_len,
                       uint64_t lifetime_ms, uint64_t seq,
                       uint64_t fragment_offset, uint64_t total_adu_len,
                       uint64_t creation_time,
                       uint8_t *out, size_t out_size)
{
    size_t pos = 0;
    int n;

    n = cbor_encode_indef_array_start(out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = encode_primary(src, dst, BP_FLAG_FRAGMENT, lifetime_ms, seq,
                       creation_time,
                       fragment_offset, total_adu_len, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = encode_payload_block(payload, payload_len, out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    n = cbor_encode_break(out + pos, out_size - pos);
    if (n < 0) return -1; pos += (size_t)n;

    return (int)pos;
}

int bp_decode_bundle(const uint8_t *buf, size_t len, bp_bundle_t *bundle)
{
    if (!buf || !bundle || len < 3) return -1;
    memset(bundle, 0, sizeof(*bundle));

    size_t pos = 0;
    int n;

    /* Indefinite array start */
    if (!cbor_is_indef_array(buf + pos, len - pos)) return -1;
    pos++;

    /* Primary block: CBOR array */
    uint64_t pcount;
    n = cbor_decode_array(buf + pos, len - pos, &pcount);
    if (n < 0) return -1; pos += (size_t)n;

    /* version */
    uint64_t version;
    n = cbor_decode_uint(buf + pos, len - pos, &version);
    if (n < 0 || version != BP_VERSION) return -1;
    pos += (size_t)n;

    /* flags */
    n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.flags);
    if (n < 0) return -1; pos += (size_t)n;

    /* crc_type */
    uint64_t crc_type;
    n = cbor_decode_uint(buf + pos, len - pos, &crc_type);
    if (n < 0) return -1; pos += (size_t)n;
    bundle->primary.crc_type = (uint8_t)crc_type;

    /* dst EID */
    n = bp_eid_decode(buf + pos, len - pos, &bundle->primary.dst);
    if (n < 0) return -1; pos += (size_t)n;

    /* src EID */
    n = bp_eid_decode(buf + pos, len - pos, &bundle->primary.src);
    if (n < 0) return -1; pos += (size_t)n;

    /* report-to EID */
    n = bp_eid_decode(buf + pos, len - pos, &bundle->primary.report_to);
    if (n < 0) return -1; pos += (size_t)n;

    /* timestamp [time, seq] */
    uint64_t ts_count;
    n = cbor_decode_array(buf + pos, len - pos, &ts_count);
    if (n < 0 || ts_count != 2) return -1; pos += (size_t)n;
    n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.timestamp.time);
    if (n < 0) return -1; pos += (size_t)n;
    n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.timestamp.seq);
    if (n < 0) return -1; pos += (size_t)n;

    /* lifetime */
    n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.lifetime_ms);
    if (n < 0) return -1; pos += (size_t)n;

    /* fragment fields (if present) */
    if (bundle->primary.flags & BP_FLAG_FRAGMENT) {
        n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.fragment_offset);
        if (n < 0) return -1; pos += (size_t)n;
        n = cbor_decode_uint(buf + pos, len - pos, &bundle->primary.total_adu_len);
        if (n < 0) return -1; pos += (size_t)n;
    }

    /* CRC value (bstr) — skip for now, just consume */
    if (crc_type == 1) {
        const uint8_t *crc_data; size_t crc_len;
        n = cbor_decode_bstr(buf + pos, len - pos, &crc_data, &crc_len);
        if (n < 0) return -1; pos += (size_t)n;
    }

    /* Payload block: CBOR array */
    uint64_t bcount;
    n = cbor_decode_array(buf + pos, len - pos, &bcount);
    if (n < 0 || bcount < 5) return -1; pos += (size_t)n;

    /* block_type */
    uint64_t btype;
    n = cbor_decode_uint(buf + pos, len - pos, &btype);
    if (n < 0) return -1; pos += (size_t)n;

    /* block_number */
    uint64_t bnum;
    n = cbor_decode_uint(buf + pos, len - pos, &bnum);
    if (n < 0) return -1; pos += (size_t)n;

    /* block flags */
    uint64_t bflags;
    n = cbor_decode_uint(buf + pos, len - pos, &bflags);
    if (n < 0) return -1; pos += (size_t)n;

    /* block crc_type */
    uint64_t bcrc;
    n = cbor_decode_uint(buf + pos, len - pos, &bcrc);
    if (n < 0) return -1; pos += (size_t)n;

    /* payload bstr */
    n = cbor_decode_bstr(buf + pos, len - pos, &bundle->payload, &bundle->payload_len);
    if (n < 0) return -1; pos += (size_t)n;

    /* break */
    if (!cbor_is_break(buf + pos, len - pos)) return -1;

    return (int)(pos + 1);
}

int bp_fragment_count(size_t payload_len, size_t fragment_payload_size)
{
    if (fragment_payload_size == 0) return -1;
    return (int)((payload_len + fragment_payload_size - 1) / fragment_payload_size);
}

void bp_reassembly_init(bp_reassembly_t *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

int bp_reassembly_add(bp_reassembly_t *ctx, const bp_bundle_t *frag)
{
    if (!ctx || !frag) return -1;
    if (!(frag->primary.flags & BP_FLAG_FRAGMENT)) return -1;

    if (!ctx->active) {
        ctx->active = 1;
        ctx->src = frag->primary.src;
        ctx->timestamp = frag->primary.timestamp;
        ctx->total_adu_len = frag->primary.total_adu_len;
        ctx->lifetime_ms = frag->primary.lifetime_ms;
        ctx->bytes_received = 0;
    }

    /* Verify matching fragment */
    if (strcmp(ctx->src.uri, frag->primary.src.uri) != 0) return -1;
    if (ctx->timestamp.time != frag->primary.timestamp.time) return -1;
    if (ctx->timestamp.seq != frag->primary.timestamp.seq) return -1;
    if (ctx->total_adu_len != frag->primary.total_adu_len) return -1;

    uint64_t off = frag->primary.fragment_offset;
    size_t plen = frag->payload_len;
    if (off + plen > ctx->total_adu_len) return -1;
    if (off + plen > BP_MAX_PAYLOAD) return -1;

    /* Copy data and mark received */
    if (frag->payload && plen > 0) {
        memcpy(ctx->data + off, frag->payload, plen);
        for (size_t i = 0; i < plen; i++) {
            if (!ctx->received[off + i]) {
                ctx->received[off + i] = 1;
                ctx->bytes_received++;
            }
        }
    }

    return (ctx->bytes_received >= ctx->total_adu_len) ? 1 : 0;
}
