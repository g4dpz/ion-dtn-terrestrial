/*
 * ltp.c — Licklider Transmission Protocol over KISS
 *
 * Implements LTP segment encoding/decoding, session management,
 * checkpoint/report exchange, retransmission timers, and event loop.
 *
 * Task 3: Only segment encoding/decoding and ltp_eid_to_engine_id
 * are fully implemented. Other functions are stubs returning -1 or 0.
 */

#define _POSIX_C_SOURCE 199309L

#include "ltp.h"
#include "sdnv.h"
#include "kiss.h"
#include "aprs.h"
#include "ax25.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>

extern volatile sig_atomic_t g_running;

/* ================================================================== */
/* Internal: encode LTP segment header                                 */
/* ================================================================== */

/*
 * Encode the common LTP segment header into out.
 * Wire format:
 *   Byte 0: (version << 4) | (type & 0x0F)
 *   SDNV:   sender engine ID
 *   SDNV:   session number
 *   Byte:   header extension count (0)
 *   Byte:   trailer extension count (0)
 *
 * Returns number of bytes written, or -1 on error.
 */
static int ltp_encode_header(const ltp_segment_hdr_t *hdr,
                             uint8_t *out, size_t out_size)
{
    if (!hdr || !out || out_size < 4)
        return -1;

    size_t pos = 0;

    /* Byte 0: version (upper 4 bits) | type (lower 4 bits) */
    out[pos++] = (uint8_t)((hdr->version << 4) | (hdr->type & 0x0F));

    /* SDNV: sender engine ID */
    int n = sdnv_encode(hdr->sender_engine_id, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* SDNV: session number */
    n = sdnv_encode(hdr->session_number, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Extension counts (both 0) */
    if (pos + 2 > out_size) return -1;
    out[pos++] = hdr->hdr_ext_count;
    out[pos++] = hdr->trailer_ext_count;

    return (int)pos;
}

/* ================================================================== */
/* Segment Encoding                                                    */
/* ================================================================== */

/*
 * ltp_encode_data_segment — encode a data segment (types 0-4)
 *
 * Wire format after header:
 *   SDNV: client service ID
 *   SDNV: offset within block
 *   SDNV: data length
 *   [If type 1 or 2 (checkpoint): SDNV checkpoint serial, SDNV report serial]
 *   Raw data bytes (length bytes)
 *
 * Returns total encoded length, or -1 on error.
 */
int ltp_encode_data_segment(const ltp_data_segment_t *seg,
                            uint8_t *out, size_t out_size)
{
    if (!seg || !out)
        return -1;

    size_t pos = 0;
    int n;

    /* Encode header */
    n = ltp_encode_header(&seg->hdr, out, out_size);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Client service ID */
    n = sdnv_encode(seg->client_svc_id, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Offset within block */
    n = sdnv_encode(seg->offset, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Data length */
    n = sdnv_encode(seg->length, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Checkpoint serial + report serial (only for checkpoint types) */
    if (seg->hdr.type == LTP_SEG_RED_DATA_CP ||
        seg->hdr.type == LTP_SEG_RED_DATA_EORP_CP) {
        n = sdnv_encode(seg->cp_serial, out + pos, out_size - pos);
        if (n < 0) return -1;
        pos += (size_t)n;

        n = sdnv_encode(seg->rpt_serial, out + pos, out_size - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }

    /* Raw data bytes */
    if (seg->length > 0) {
        if (!seg->data) return -1;
        if (pos + seg->length > out_size) return -1;
        memcpy(out + pos, seg->data, (size_t)seg->length);
        pos += (size_t)seg->length;
    }

    return (int)pos;
}

/*
 * ltp_encode_report — encode a reception report segment (type 8)
 *
 * Wire format after header:
 *   SDNV: report serial number
 *   SDNV: checkpoint serial number
 *   SDNV: upper bound
 *   SDNV: lower bound
 *   SDNV: claim count
 *   For each claim:
 *     SDNV: claim offset (relative to lower bound)
 *     SDNV: claim length
 *
 * Returns total encoded length, or -1 on error.
 */
int ltp_encode_report(const ltp_report_segment_t *rpt,
                      uint8_t *out, size_t out_size)
{
    if (!rpt || !out)
        return -1;

    size_t pos = 0;
    int n;

    /* Encode header */
    n = ltp_encode_header(&rpt->hdr, out, out_size);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Report serial */
    n = sdnv_encode(rpt->rpt_serial, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Checkpoint serial */
    n = sdnv_encode(rpt->cp_serial, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Upper bound */
    n = sdnv_encode(rpt->upper_bound, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Lower bound */
    n = sdnv_encode(rpt->lower_bound, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Claim count */
    n = sdnv_encode(rpt->claim_count, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Claims */
    for (uint32_t i = 0; i < rpt->claim_count; i++) {
        n = sdnv_encode(rpt->claims[i].offset, out + pos, out_size - pos);
        if (n < 0) return -1;
        pos += (size_t)n;

        n = sdnv_encode(rpt->claims[i].length, out + pos, out_size - pos);
        if (n < 0) return -1;
        pos += (size_t)n;
    }

    return (int)pos;
}

/*
 * ltp_encode_report_ack — encode a report acknowledgment (type 9)
 *
 * Wire format after header:
 *   SDNV: report serial number
 *
 * Returns total encoded length, or -1 on error.
 */
int ltp_encode_report_ack(const ltp_report_ack_segment_t *ack,
                          uint8_t *out, size_t out_size)
{
    if (!ack || !out)
        return -1;

    size_t pos = 0;
    int n;

    /* Encode header */
    n = ltp_encode_header(&ack->hdr, out, out_size);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Report serial */
    n = sdnv_encode(ack->rpt_serial, out + pos, out_size - pos);
    if (n < 0) return -1;
    pos += (size_t)n;

    return (int)pos;
}

/*
 * ltp_encode_cancel — encode a cancel segment (types 12-15)
 *
 * Wire format after header:
 *   1 byte: reason code
 *
 * Returns total encoded length, or -1 on error.
 */
int ltp_encode_cancel(const ltp_cancel_segment_t *cancel,
                      uint8_t *out, size_t out_size)
{
    if (!cancel || !out)
        return -1;

    size_t pos = 0;
    int n;

    /* Encode header */
    n = ltp_encode_header(&cancel->hdr, out, out_size);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Reason code (1 byte) */
    if (pos + 1 > out_size) return -1;
    out[pos++] = cancel->reason;

    return (int)pos;
}

/* ================================================================== */
/* Segment Decoding                                                    */
/* ================================================================== */

/*
 * ltp_decode_segment — parse the common LTP segment header and
 * copy the remaining body bytes to the output buffer.
 *
 * Parses:
 *   Byte 0: version (upper 4 bits), type (lower 4 bits)
 *   SDNV:   sender engine ID
 *   SDNV:   session number
 *   Byte:   header extension count
 *   Byte:   trailer extension count
 *
 * Unrecognized types (5-7, 10-11) return -1.
 *
 * Returns 0 on success, -1 on error.
 */
int ltp_decode_segment(const uint8_t *buf, size_t len,
                       ltp_segment_hdr_t *hdr,
                       uint8_t *body, size_t body_size,
                       size_t *body_len)
{
    if (!buf || !hdr || len < 4)
        return -1;

    size_t pos = 0;

    /* Byte 0: version | type */
    hdr->version = (buf[pos] >> 4) & 0x0F;
    uint8_t type_val = buf[pos] & 0x0F;
    pos++;

    /* Validate type — reject reserved values 5-7, 10-11 */
    if ((type_val >= 5 && type_val <= 7) ||
        (type_val >= 10 && type_val <= 11)) {
        return -1;
    }
    hdr->type = (ltp_seg_type_t)type_val;

    /* SDNV: sender engine ID */
    int n = sdnv_decode(buf + pos, len - pos, &hdr->sender_engine_id);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* SDNV: session number */
    n = sdnv_decode(buf + pos, len - pos, &hdr->session_number);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Extension counts */
    if (pos + 2 > len) return -1;
    hdr->hdr_ext_count = buf[pos++];
    hdr->trailer_ext_count = buf[pos++];

    /* Skip header extensions (each is SDNV tag + SDNV length + data) */
    for (uint8_t i = 0; i < hdr->hdr_ext_count; i++) {
        uint64_t tag, ext_len;
        n = sdnv_decode(buf + pos, len - pos, &tag);
        if (n < 0) return -1;
        pos += (size_t)n;
        n = sdnv_decode(buf + pos, len - pos, &ext_len);
        if (n < 0) return -1;
        pos += (size_t)n;
        if (pos + ext_len > len) return -1;
        pos += (size_t)ext_len;
    }

    /* Copy remaining body bytes */
    size_t remaining = len - pos;
    if (body && body_size > 0) {
        if (remaining > body_size) return -1;
        memcpy(body, buf + pos, remaining);
    }
    if (body_len)
        *body_len = remaining;

    return 0;
}

/* ================================================================== */
/* Type-specific decode helpers                                        */
/* ================================================================== */

/*
 * ltp_decode_data_content — parse data segment body
 *
 * Parses:
 *   SDNV: client service ID
 *   SDNV: offset
 *   SDNV: data length
 *   [If type 1 or 2: SDNV checkpoint serial, SDNV report serial]
 *   Raw data bytes
 *
 * out->data will point into the body buffer (not copied).
 * Returns 0 on success, -1 on error.
 */
int ltp_decode_data_content(const uint8_t *body, size_t body_len,
                            ltp_seg_type_t type,
                            ltp_data_segment_t *out)
{
    if (!body || !out)
        return -1;

    size_t pos = 0;
    int n;

    /* Client service ID */
    n = sdnv_decode(body + pos, body_len - pos, &out->client_svc_id);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Offset */
    n = sdnv_decode(body + pos, body_len - pos, &out->offset);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Data length */
    n = sdnv_decode(body + pos, body_len - pos, &out->length);
    if (n < 0) return -1;
    pos += (size_t)n;

    /* Checkpoint serial + report serial (only for checkpoint types) */
    out->cp_serial = 0;
    out->rpt_serial = 0;
    if (type == LTP_SEG_RED_DATA_CP || type == LTP_SEG_RED_DATA_EORP_CP) {
        n = sdnv_decode(body + pos, body_len - pos, &out->cp_serial);
        if (n < 0) return -1;
        pos += (size_t)n;

        n = sdnv_decode(body + pos, body_len - pos, &out->rpt_serial);
        if (n < 0) return -1;
        pos += (size_t)n;
    }

    /* Verify remaining bytes match declared length */
    if (body_len - pos < (size_t)out->length)
        return -1;

    /* Point data into the body buffer */
    out->data = (out->length > 0) ? (body + pos) : NULL;

    return 0;
}

/*
 * ltp_decode_report_content — parse report segment body
 *
 * Parses:
 *   SDNV: report serial
 *   SDNV: checkpoint serial
 *   SDNV: upper bound
 *   SDNV: lower bound
 *   SDNV: claim count
 *   For each claim: SDNV offset, SDNV length
 *
 * Returns 0 on success, -1 on error.
 */
int ltp_decode_report_content(const uint8_t *body, size_t body_len,
                              ltp_report_segment_t *out)
{
    if (!body || !out)
        return -1;

    size_t pos = 0;
    int n;

    n = sdnv_decode(body + pos, body_len - pos, &out->rpt_serial);
    if (n < 0) return -1;
    pos += (size_t)n;

    n = sdnv_decode(body + pos, body_len - pos, &out->cp_serial);
    if (n < 0) return -1;
    pos += (size_t)n;

    n = sdnv_decode(body + pos, body_len - pos, &out->upper_bound);
    if (n < 0) return -1;
    pos += (size_t)n;

    n = sdnv_decode(body + pos, body_len - pos, &out->lower_bound);
    if (n < 0) return -1;
    pos += (size_t)n;

    uint64_t count;
    n = sdnv_decode(body + pos, body_len - pos, &count);
    if (n < 0) return -1;
    pos += (size_t)n;

    if (count > LTP_MAX_CLAIMS)
        return -1;
    out->claim_count = (uint32_t)count;

    for (uint32_t i = 0; i < out->claim_count; i++) {
        n = sdnv_decode(body + pos, body_len - pos, &out->claims[i].offset);
        if (n < 0) return -1;
        pos += (size_t)n;

        n = sdnv_decode(body + pos, body_len - pos, &out->claims[i].length);
        if (n < 0) return -1;
        pos += (size_t)n;
    }

    return 0;
}

/*
 * ltp_decode_report_ack_content — parse report ack body
 *
 * Parses:
 *   SDNV: report serial
 *
 * Returns 0 on success, -1 on error.
 */
int ltp_decode_report_ack_content(const uint8_t *body, size_t body_len,
                                  ltp_report_ack_segment_t *out)
{
    if (!body || !out)
        return -1;

    int n = sdnv_decode(body, body_len, &out->rpt_serial);
    if (n < 0) return -1;

    return 0;
}

/*
 * ltp_decode_cancel_content — parse cancel segment body
 *
 * Parses:
 *   1 byte: reason code
 *
 * Returns 0 on success, -1 on error.
 */
int ltp_decode_cancel_content(const uint8_t *body, size_t body_len,
                              ltp_cancel_segment_t *out)
{
    if (!body || !out || body_len < 1)
        return -1;

    out->reason = body[0];
    return 0;
}

/* ================================================================== */
/* Endpoint Mapping                                                    */
/* ================================================================== */

/*
 * ltp_eid_to_engine_id — derive numeric engine ID from DTN endpoint
 *
 * Strips "dtn://" prefix if present, then applies DJB2 hash.
 */
uint64_t ltp_eid_to_engine_id(const char *eid)
{
    const char *callsign = eid;
    if (eid && strncmp(eid, "dtn://", 6) == 0)
        callsign = eid + 6;

    uint64_t hash = 5381;
    if (callsign) {
        for (const char *p = callsign; *p; p++)
            hash = ((hash << 5) + hash) + (uint8_t)*p;  /* hash * 33 + c */
    }
    return hash;
}

/* ================================================================== */
/* Receive Map Helpers                                                 */
/* ================================================================== */

/*
 * ltp_recv_map_add_claim — add a byte range and merge overlapping/adjacent claims.
 * Keeps claims sorted by offset. Simple insertion sort approach.
 */
void ltp_recv_map_add_claim(ltp_recv_map_t *map, uint64_t offset,
                            uint64_t length)
{
    if (!map || length == 0) return;

    /* If map is full, try to merge in-place anyway */
    uint64_t new_start = offset;
    uint64_t new_end = offset + length;

    /* Merge with any overlapping or adjacent existing claims */
    for (uint32_t i = 0; i < map->claim_count; ) {
        uint64_t cs = map->claims[i].offset;
        uint64_t ce = cs + map->claims[i].length;

        /* Check if overlapping or adjacent */
        if (new_start <= ce && new_end >= cs) {
            /* Merge: expand new range */
            if (cs < new_start) new_start = cs;
            if (ce > new_end) new_end = ce;
            /* Remove this claim by shifting */
            for (uint32_t j = i; j + 1 < map->claim_count; j++)
                map->claims[j] = map->claims[j + 1];
            map->claim_count--;
        } else {
            i++;
        }
    }

    /* Insert the merged claim if there's room */
    if (map->claim_count >= LTP_MAX_CLAIMS) return;

    /* Find insertion point (keep sorted by offset) */
    uint32_t ins = map->claim_count;
    for (uint32_t i = 0; i < map->claim_count; i++) {
        if (new_start < map->claims[i].offset) {
            ins = i;
            break;
        }
    }

    /* Shift claims to make room */
    for (uint32_t j = map->claim_count; j > ins; j--)
        map->claims[j] = map->claims[j - 1];

    map->claims[ins].offset = new_start;
    map->claims[ins].length = new_end - new_start;
    map->claim_count++;
}

/* ================================================================== */
/* Timer Helpers                                                       */
/* ================================================================== */

/*
 * ltp_start_timer — find an inactive timer slot and start a timer.
 *
 * type: 0=checkpoint, 1=report, 2=cancel
 * Duration = 2 * OWLT + 200ms
 *
 * Returns timer slot index, or -1 if no free slots.
 */
static int ltp_start_timer(ltp_engine_t *eng, int type,
                           uint64_t session_engine_id, uint64_t session_number,
                           uint64_t serial)
{
    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (!eng->timers[i].active) {
            ltp_timer_t *t = &eng->timers[i];
            t->active = 1;
            t->type = type;
            t->session_engine_id = session_engine_id;
            t->session_number = session_number;
            t->serial = serial;
            t->retries = 0;
            clock_gettime(CLOCK_MONOTONIC, &t->expiry);
            int64_t duration_ms = 2 * (int64_t)eng->config.owlt_ms + 200;
            t->expiry.tv_sec += duration_ms / 1000;
            t->expiry.tv_nsec += (duration_ms % 1000) * 1000000;
            if (t->expiry.tv_nsec >= 1000000000) {
                t->expiry.tv_sec++;
                t->expiry.tv_nsec -= 1000000000;
            }
            return i;
        }
    }
    return -1;
}

/*
 * ltp_cancel_timer — deactivate a timer matching type, session_number, serial.
 */
static void ltp_cancel_timer(ltp_engine_t *eng, int type,
                             uint64_t session_number, uint64_t serial)
{
    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (eng->timers[i].active &&
            eng->timers[i].type == type &&
            eng->timers[i].session_number == session_number &&
            eng->timers[i].serial == serial) {
            eng->timers[i].active = 0;
            break;
        }
    }
}

/* ================================================================== */
/* Engine Lifecycle                                                     */
/* ================================================================== */

int ltp_engine_init(ltp_engine_t *eng, const char *local_eid,
                    const ltp_config_t *config)
{
    if (!eng || !local_eid) return -1;

    memset(eng, 0, sizeof(*eng));

    if (config) {
        eng->config = *config;
    } else {
        eng->config.segment_mtu = LTP_DEFAULT_SEGMENT_MTU;
        eng->config.owlt_ms = LTP_DEFAULT_OWLT_MS;
        eng->config.max_retries = LTP_DEFAULT_MAX_RETRIES;
        eng->config.max_block_size = LTP_MAX_BLOCK_SIZE;
        eng->config.verbose = 0;
    }

    eng->local_engine_id = ltp_eid_to_engine_id(local_eid);
    strncpy(eng->local_eid, local_eid, sizeof(eng->local_eid) - 1);
    eng->local_eid[sizeof(eng->local_eid) - 1] = '\0';
    eng->next_session_number = 1;

    return ltp_register_endpoint(eng, local_eid);
}

/* ================================================================== */
/* Block Segmentation (callback-based, for testing)                    */
/* ================================================================== */

/*
 * ltp_segment_block — segment a block into data segments, calling cb
 * for each segment produced. Does not perform I/O.
 *
 * Allocates an export session internally.
 * Returns number of segments produced, or -1 on error.
 */
int ltp_segment_block(ltp_engine_t *eng, uint64_t remote_engine_id,
                      const uint8_t *data, uint32_t len,
                      ltp_segment_cb cb, void *ctx)
{
    if (!eng || !data || len == 0) return -1;
    if (len > eng->config.max_block_size) return -1;

    /* Find first inactive export session slot */
    int slot = -1;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (!eng->export_sessions[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    ltp_export_session_t *sess = &eng->export_sessions[slot];
    memset(sess, 0, sizeof(*sess));
    sess->active = 1;
    sess->engine_id = eng->local_engine_id;
    sess->session_number = eng->next_session_number++;
    sess->remote_engine_id = remote_engine_id;
    sess->block_len = len;
    sess->segment_mtu = eng->config.segment_mtu;
    sess->next_cp_serial = 1;
    memcpy(sess->block_data, data, len);

    uint32_t mtu = sess->segment_mtu;
    uint32_t offset = 0;
    int seg_count = 0;

    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > mtu) chunk = mtu;

        int is_last = (offset + chunk >= len);

        ltp_data_segment_t seg;
        memset(&seg, 0, sizeof(seg));
        seg.hdr.version = 0;
        seg.hdr.type = is_last ? LTP_SEG_RED_DATA_EORP_CP : LTP_SEG_RED_DATA;
        seg.hdr.sender_engine_id = sess->engine_id;
        seg.hdr.session_number = sess->session_number;
        seg.hdr.hdr_ext_count = 0;
        seg.hdr.trailer_ext_count = 0;
        seg.client_svc_id = 1;
        seg.offset = offset;
        seg.length = chunk;
        seg.data = sess->block_data + offset;

        if (is_last) {
            seg.cp_serial = sess->next_cp_serial++;
            seg.rpt_serial = 0;
        }

        if (cb) cb(&seg, ctx);
        seg_count++;
        offset += chunk;
    }

    return seg_count;
}

/* ================================================================== */
/* Block Transmission                                                  */
/* ================================================================== */

/*
 * Callback context for ltp_send_block — writes KISS-encoded segments to fd.
 */
typedef struct {
    int fd;
    int error;
} send_block_ctx_t;

static void send_block_cb(const ltp_data_segment_t *seg, void *ctx)
{
    send_block_ctx_t *sctx = (send_block_ctx_t *)ctx;
    if (sctx->error) return;

    uint8_t ltp_buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_data_segment(seg, ltp_buf, sizeof(ltp_buf));
    if (enc_len < 0) { sctx->error = 1; return; }

    uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
    int kiss_len = kiss_encode(ltp_buf, (size_t)enc_len,
                               kiss_buf, sizeof(kiss_buf));
    if (kiss_len < 0) { sctx->error = 1; return; }

    ssize_t written = write(sctx->fd, kiss_buf, (size_t)kiss_len);
    if (written < 0) { sctx->error = 1; return; }
    tcdrain(sctx->fd);
}

int ltp_send_block(ltp_engine_t *eng, int fd,
                   const char *remote_eid,
                   const uint8_t *data, uint32_t len)
{
    if (!eng || !remote_eid || !data || len == 0) return -1;
    if (len > eng->config.max_block_size) return -1;

    uint64_t remote_eid_id = ltp_eid_to_engine_id(remote_eid);

    send_block_ctx_t sctx;
    sctx.fd = fd;
    sctx.error = 0;

    int seg_count = ltp_segment_block(eng, remote_eid_id, data, len,
                                      send_block_cb, &sctx);
    if (seg_count < 0 || sctx.error) return -1;

    eng->segments_sent += (uint32_t)seg_count;

    /* Start checkpoint retransmission timer for the export session */
    /* Find the session we just created (most recent) */
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        ltp_export_session_t *sess = &eng->export_sessions[i];
        if (sess->active &&
            sess->engine_id == eng->local_engine_id &&
            sess->session_number == eng->next_session_number - 1) {
            /* CP serial was incremented after assignment, so last used = next - 1 */
            ltp_start_timer(eng, 0, sess->engine_id,
                            sess->session_number,
                            sess->next_cp_serial - 1);
            break;
        }
    }

    return 0;
}

/* ================================================================== */
/* Retransmission of missing ranges                                    */
/* ================================================================== */

/*
 * ltp_retransmit_missing — compute missing byte ranges from a report's
 * claims and retransmit them as new data segments.
 *
 * Missing ranges = [lower_bound, upper_bound) minus the union of claims.
 * The final retransmitted segment is marked as a new checkpoint.
 */
static int ltp_retransmit_missing(ltp_engine_t *eng, int fd,
                                  ltp_export_session_t *sess,
                                  const ltp_report_segment_t *rpt)
{
    if (!eng || !sess || !rpt) return -1;

    /* Compute missing ranges by subtracting claims from [lower_bound, upper_bound) */
    /* Claims are sorted by offset from the report */
    uint64_t lower = rpt->lower_bound;
    uint64_t upper = rpt->upper_bound;
    if (upper > sess->block_len) upper = sess->block_len;

    /* Collect missing ranges */
    typedef struct { uint64_t start; uint64_t end; } range_t;
    range_t missing[LTP_MAX_CLAIMS + 1];
    int miss_count = 0;

    uint64_t cursor = lower;
    for (uint32_t i = 0; i < rpt->claim_count; i++) {
        uint64_t claim_start = rpt->claims[i].offset;
        uint64_t claim_end = claim_start + rpt->claims[i].length;

        if (claim_start > cursor && miss_count < (int)(sizeof(missing)/sizeof(missing[0]))) {
            missing[miss_count].start = cursor;
            missing[miss_count].end = claim_start;
            miss_count++;
        }
        if (claim_end > cursor)
            cursor = claim_end;
    }
    /* Gap after last claim */
    if (cursor < upper && miss_count < (int)(sizeof(missing)/sizeof(missing[0]))) {
        missing[miss_count].start = cursor;
        missing[miss_count].end = upper;
        miss_count++;
    }

    if (miss_count == 0) return 0;

    /* Retransmit each missing range, splitting by MTU */
    uint32_t mtu = sess->segment_mtu;
    if (mtu == 0) mtu = eng->config.segment_mtu;
    if (mtu == 0) mtu = LTP_DEFAULT_SEGMENT_MTU;

    /* Count total segments to determine which is last */
    int total_segs = 0;
    for (int m = 0; m < miss_count; m++) {
        uint64_t range_len = missing[m].end - missing[m].start;
        total_segs += (int)((range_len + mtu - 1) / mtu);
    }

    int seg_idx = 0;
    for (int m = 0; m < miss_count; m++) {
        uint64_t off = missing[m].start;
        uint64_t range_end = missing[m].end;

        while (off < range_end) {
            uint64_t chunk = range_end - off;
            if (chunk > mtu) chunk = mtu;
            seg_idx++;

            int is_last = (seg_idx == total_segs);

            ltp_data_segment_t seg;
            memset(&seg, 0, sizeof(seg));
            seg.hdr.version = 0;
            seg.hdr.type = is_last ? LTP_SEG_RED_DATA_CP : LTP_SEG_RED_DATA;
            seg.hdr.sender_engine_id = sess->engine_id;
            seg.hdr.session_number = sess->session_number;
            seg.hdr.hdr_ext_count = 0;
            seg.hdr.trailer_ext_count = 0;
            seg.client_svc_id = 1;
            seg.offset = off;
            seg.length = chunk;
            seg.data = sess->block_data + off;

            if (is_last) {
                seg.cp_serial = sess->next_cp_serial++;
                seg.rpt_serial = 0;
            }

            /* Encode and send */
            uint8_t ltp_buf[LTP_MAX_SEGMENT_BUF];
            int enc_len = ltp_encode_data_segment(&seg, ltp_buf, sizeof(ltp_buf));
            if (enc_len > 0 && fd >= 0) {
                uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
                int kiss_len = kiss_encode(ltp_buf, (size_t)enc_len,
                                           kiss_buf, sizeof(kiss_buf));
                if (kiss_len > 0) {
                    ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                    (void)wr;
                    eng->segments_sent++;
                }
            }

            off += chunk;
        }
    }

    return total_segs;
}

/* ================================================================== */
/* Segment Processing                                                  */
/* ================================================================== */

int ltp_process_segment(ltp_engine_t *eng, int fd,
                        const uint8_t *buf, size_t len)
{
    if (!eng || !buf || len < 4) return -1;

    /* Decode the segment header */
    ltp_segment_hdr_t hdr;
    uint8_t body[LTP_MAX_SEGMENT_BUF];
    size_t body_len = 0;
    if (ltp_decode_segment(buf, len, &hdr, body, sizeof(body), &body_len) < 0)
        return -1;

    eng->segments_received++;

    uint8_t type = (uint8_t)hdr.type;

    /* Handle data segment types (0, 1, 2) */
    if (type == LTP_SEG_RED_DATA ||
        type == LTP_SEG_RED_DATA_CP ||
        type == LTP_SEG_RED_DATA_EORP_CP) {

        ltp_data_segment_t dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.hdr = hdr;
        if (ltp_decode_data_content(body, body_len, hdr.type, &dseg) < 0)
            return -1;

        /* Find or create import session */
        ltp_import_session_t *sess = NULL;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            if (eng->import_sessions[i].active &&
                eng->import_sessions[i].engine_id == hdr.sender_engine_id &&
                eng->import_sessions[i].session_number == hdr.session_number) {
                sess = &eng->import_sessions[i];
                break;
            }
        }

        if (!sess) {
            /* Create new import session */
            int slot = -1;
            for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
                if (!eng->import_sessions[i].active) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) return -1; /* No free import session slots */

            sess = &eng->import_sessions[slot];
            memset(sess, 0, sizeof(*sess));
            sess->active = 1;
            sess->engine_id = hdr.sender_engine_id;
            sess->session_number = hdr.session_number;
            sess->next_rpt_serial = 1;
        }

        /* Validate offset + length doesn't exceed max block size */
        if (dseg.offset + dseg.length > LTP_MAX_BLOCK_SIZE)
            return -1;

        /* Copy data into session buffer at offset */
        if (dseg.length > 0 && dseg.data) {
            memcpy(sess->block_data + dseg.offset, dseg.data,
                   (size_t)dseg.length);
        }

        /* Update recv_map */
        ltp_recv_map_add_claim(&sess->recv_map, dseg.offset, dseg.length);

        /* If EoRP+CP (type 2): set block_len = offset + length */
        if (type == LTP_SEG_RED_DATA_EORP_CP) {
            sess->block_len = (uint32_t)(dseg.offset + dseg.length);
        }

        /* Check if block is complete whenever block_len is known */
        if (sess->block_len > 0 &&
            sess->recv_map.claim_count == 1 &&
            sess->recv_map.claims[0].offset == 0 &&
            sess->recv_map.claims[0].length >= sess->block_len) {

            sess->complete = 1;
        }

        /* If this is a checkpoint (type 1 or 2), generate a reception report */
        if (type == LTP_SEG_RED_DATA_CP ||
            type == LTP_SEG_RED_DATA_EORP_CP) {

            /* Build reception report from recv_map */
            ltp_report_segment_t rpt;
            memset(&rpt, 0, sizeof(rpt));
            rpt.hdr.version = 0;
            rpt.hdr.type = LTP_SEG_REPORT;
            rpt.hdr.sender_engine_id = eng->local_engine_id;
            rpt.hdr.session_number = hdr.session_number;
            rpt.hdr.hdr_ext_count = 0;
            rpt.hdr.trailer_ext_count = 0;
            rpt.rpt_serial = sess->next_rpt_serial++;
            rpt.cp_serial = dseg.cp_serial;
            rpt.lower_bound = 0;

            /* upper_bound = block_len if known (EoRP), else offset+length */
            if (sess->block_len > 0)
                rpt.upper_bound = sess->block_len;
            else
                rpt.upper_bound = dseg.offset + dseg.length;

            /* Copy claims from recv_map */
            rpt.claim_count = sess->recv_map.claim_count;
            for (uint32_t ci = 0; ci < rpt.claim_count && ci < LTP_MAX_CLAIMS; ci++) {
                rpt.claims[ci].offset = sess->recv_map.claims[ci].offset;
                rpt.claims[ci].length = sess->recv_map.claims[ci].length;
            }

            /* Encode and send the report */
            uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
            int rpt_len = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
            if (rpt_len > 0 && fd >= 0) {
                uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
                int kiss_len = kiss_encode(rpt_buf, (size_t)rpt_len,
                                           kiss_buf, sizeof(kiss_buf));
                if (kiss_len > 0) {
                    ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                    (void)wr;
                    eng->segments_sent++;
                }
            }
            /* Start report retransmission timer */
            ltp_start_timer(eng, 1, sess->engine_id,
                            sess->session_number, rpt.rpt_serial);
        }

        /* Deliver block if complete */
        if (sess->complete && !sess->delivered) {
            if (eng->on_block_received) {
                eng->on_block_received(sess->block_data, sess->block_len,
                                       sess->engine_id, eng->cb_ctx);
            }
            eng->blocks_delivered++;
            sess->delivered = 1;
            sess->active = 0;
        }

        return 0;
    }

    /* ---- Report segment (type 8) ---- */
    if (type == LTP_SEG_REPORT) {
        ltp_report_segment_t rpt;
        memset(&rpt, 0, sizeof(rpt));
        rpt.hdr = hdr;
        if (ltp_decode_report_content(body, body_len, &rpt) < 0)
            return -1;

        /* Find matching export session */
        ltp_export_session_t *sess = NULL;
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
            if (eng->export_sessions[i].active &&
                eng->export_sessions[i].engine_id == eng->local_engine_id &&
                eng->export_sessions[i].session_number == hdr.session_number) {
                sess = &eng->export_sessions[i];
                break;
            }
        }
        if (!sess) return -1; /* Unknown export session */

        /* Send report acknowledgment */
        ltp_report_ack_segment_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.hdr.version = 0;
        ack.hdr.type = LTP_SEG_REPORT_ACK;
        ack.hdr.sender_engine_id = eng->local_engine_id;
        ack.hdr.session_number = hdr.session_number;
        ack.hdr.hdr_ext_count = 0;
        ack.hdr.trailer_ext_count = 0;
        ack.rpt_serial = rpt.rpt_serial;

        uint8_t ack_buf[LTP_MAX_SEGMENT_BUF];
        int ack_len = ltp_encode_report_ack(&ack, ack_buf, sizeof(ack_buf));
        if (ack_len > 0 && fd >= 0) {
            uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
            int kiss_len = kiss_encode(ack_buf, (size_t)ack_len,
                                       kiss_buf, sizeof(kiss_buf));
            if (kiss_len > 0) {
                ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                (void)wr;
                eng->segments_sent++;
            }
        }

        /* Cancel checkpoint retransmission timer */
        ltp_cancel_timer(eng, 0, hdr.session_number, rpt.cp_serial);

        /* Check if report covers entire block [0, block_len) */
        int fully_acked = 0;
        if (rpt.claim_count > 0) {
            /* Compute total coverage from claims */
            uint64_t covered = 0;
            for (uint32_t ci = 0; ci < rpt.claim_count; ci++)
                covered += rpt.claims[ci].length;

            /* Check if claims cover [0, block_len) completely */
            if (rpt.claim_count == 1 &&
                rpt.claims[0].offset == 0 &&
                rpt.claims[0].length >= sess->block_len) {
                fully_acked = 1;
            }
        }

        if (fully_acked) {
            sess->completed = 1;
            eng->sessions_completed++;
        } else {
            /* Retransmit missing ranges */
            ltp_retransmit_missing(eng, fd, sess, &rpt);
            /* Start a new checkpoint timer for the retransmitted checkpoint */
            ltp_start_timer(eng, 0, sess->engine_id,
                            sess->session_number,
                            sess->next_cp_serial - 1);
        }

        return 0;
    }

    /* ---- Report acknowledgment segment (type 9) ---- */
    if (type == LTP_SEG_REPORT_ACK) {
        ltp_report_ack_segment_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.hdr = hdr;
        if (ltp_decode_report_ack_content(body, body_len, &ack) < 0)
            return -1;

        /* Find matching import session */
        /* Cancel report retransmission timer */
        ltp_cancel_timer(eng, 1, hdr.session_number, ack.rpt_serial);
        (void)fd;
        return 0;
    }

    /* ---- Cancel-by-sender (type 12) ---- */
    if (type == LTP_SEG_CANCEL_BY_SENDER) {
        ltp_cancel_segment_t cancel;
        memset(&cancel, 0, sizeof(cancel));
        cancel.hdr = hdr;
        if (ltp_decode_cancel_content(body, body_len, &cancel) < 0)
            return -1;

        /* Send cancel-ack-to-sender (type 13) regardless of session existence */
        ltp_cancel_segment_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.hdr.version = 0;
        ack.hdr.type = LTP_SEG_CANCEL_ACK_SENDER;
        ack.hdr.sender_engine_id = eng->local_engine_id;
        ack.hdr.session_number = hdr.session_number;
        ack.reason = 0;

        uint8_t ack_buf[LTP_MAX_SEGMENT_BUF];
        int ack_len = ltp_encode_cancel(&ack, ack_buf, sizeof(ack_buf));
        if (ack_len > 0 && fd >= 0) {
            uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
            int kiss_len = kiss_encode(ack_buf, (size_t)ack_len,
                                       kiss_buf, sizeof(kiss_buf));
            if (kiss_len > 0) {
                ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                (void)wr;
                eng->segments_sent++;
            }
        }

        /* Close import session if it exists */
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            if (eng->import_sessions[i].active &&
                eng->import_sessions[i].engine_id == hdr.sender_engine_id &&
                eng->import_sessions[i].session_number == hdr.session_number) {
                eng->import_sessions[i].active = 0;
                eng->sessions_cancelled++;
                break;
            }
        }

        return 0;
    }

    /* ---- Cancel-ack-to-sender (type 13) ---- */
    if (type == LTP_SEG_CANCEL_ACK_SENDER) {
        /* Sender gets this → close export session, cancel cancel timer */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
            if (eng->export_sessions[i].active &&
                eng->export_sessions[i].session_number == hdr.session_number) {
                eng->export_sessions[i].active = 0;
                eng->export_sessions[i].cancelled = 1;
                eng->sessions_cancelled++;
                break;
            }
        }
        /* Cancel the cancel retransmission timer (type 2, serial 0) */
        ltp_cancel_timer(eng, 2, hdr.session_number, 0);
        return 0;
    }

    /* ---- Cancel-by-receiver (type 14) ---- */
    if (type == LTP_SEG_CANCEL_BY_RECVR) {
        ltp_cancel_segment_t cancel;
        memset(&cancel, 0, sizeof(cancel));
        cancel.hdr = hdr;
        if (ltp_decode_cancel_content(body, body_len, &cancel) < 0)
            return -1;

        /* Send cancel-ack-to-receiver (type 15) regardless of session existence */
        ltp_cancel_segment_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.hdr.version = 0;
        ack.hdr.type = LTP_SEG_CANCEL_ACK_RECVR;
        ack.hdr.sender_engine_id = eng->local_engine_id;
        ack.hdr.session_number = hdr.session_number;
        ack.reason = 0;

        uint8_t ack_buf[LTP_MAX_SEGMENT_BUF];
        int ack_len = ltp_encode_cancel(&ack, ack_buf, sizeof(ack_buf));
        if (ack_len > 0 && fd >= 0) {
            uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
            int kiss_len = kiss_encode(ack_buf, (size_t)ack_len,
                                       kiss_buf, sizeof(kiss_buf));
            if (kiss_len > 0) {
                ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                (void)wr;
                eng->segments_sent++;
            }
        }

        /* Close export session if it exists */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
            if (eng->export_sessions[i].active &&
                eng->export_sessions[i].session_number == hdr.session_number) {
                eng->export_sessions[i].active = 0;
                eng->export_sessions[i].cancelled = 1;
                eng->sessions_cancelled++;
                break;
            }
        }

        return 0;
    }

    /* ---- Cancel-ack-to-receiver (type 15) ---- */
    if (type == LTP_SEG_CANCEL_ACK_RECVR) {
        /* Receiver gets this → close import session, cancel cancel timer */
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            if (eng->import_sessions[i].active &&
                eng->import_sessions[i].engine_id == hdr.sender_engine_id &&
                eng->import_sessions[i].session_number == hdr.session_number) {
                eng->import_sessions[i].active = 0;
                eng->sessions_cancelled++;
                break;
            }
        }
        /* Cancel the cancel retransmission timer (type 2, serial 0) */
        ltp_cancel_timer(eng, 2, hdr.session_number, 0);
        return 0;
    }

    /* Unknown segment type — should not reach here */
    (void)fd;
    return -1;
}

/* ================================================================== */
/* Timer Helpers                                                       */
/* ================================================================== */

/* ================================================================== */
/* Timer Management                                                    */
/* ================================================================== */

int ltp_get_next_timeout_ms(const ltp_engine_t *eng)
{
    if (!eng) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    int min_ms = -1;
    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (!eng->timers[i].active) continue;

        const struct timespec *exp = &eng->timers[i].expiry;
        int64_t diff_sec = (int64_t)exp->tv_sec - (int64_t)now.tv_sec;
        int64_t diff_ns  = (int64_t)exp->tv_nsec - (int64_t)now.tv_nsec;
        int64_t diff_ms  = diff_sec * 1000 + diff_ns / 1000000;

        if (diff_ms < 0) diff_ms = 0;

        int ms = (int)diff_ms;
        if (min_ms < 0 || ms < min_ms)
            min_ms = ms;
    }

    return min_ms;
}

int ltp_fire_expired_timers(ltp_engine_t *eng, int fd)
{
    if (!eng) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    int fired = 0;

    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (!eng->timers[i].active) continue;

        ltp_timer_t *t = &eng->timers[i];
        /* Check if expired: expiry <= now */
        if (t->expiry.tv_sec > now.tv_sec) continue;
        if (t->expiry.tv_sec == now.tv_sec && t->expiry.tv_nsec > now.tv_nsec) continue;

        /* Timer has expired */
        t->retries++;

        if (t->retries >= (int)eng->config.max_retries) {
            /* Max retries exceeded — cancel the session */
            if (t->type == 0) {
                /* Checkpoint timer — cancel export session */
                for (int j = 0; j < LTP_MAX_EXPORT_SESSIONS; j++) {
                    if (eng->export_sessions[j].active &&
                        eng->export_sessions[j].session_number == t->session_number) {
                        eng->export_sessions[j].cancelled = 1;
                        eng->export_sessions[j].active = 0;
                        eng->sessions_cancelled++;
                        break;
                    }
                }
            } else if (t->type == 1) {
                /* Report timer — cancel import session */
                for (int j = 0; j < LTP_MAX_IMPORT_SESSIONS; j++) {
                    if (eng->import_sessions[j].active &&
                        eng->import_sessions[j].session_number == t->session_number) {
                        eng->import_sessions[j].active = 0;
                        eng->sessions_cancelled++;
                        break;
                    }
                }
            }
            t->active = 0;
            fired++;
            continue;
        }

        /* Retransmit and restart timer */
        int64_t duration_ms = 2 * (int64_t)eng->config.owlt_ms + 200;

        if (t->type == 0) {
            /* Checkpoint timer — retransmit checkpoint for export session */
            ltp_export_session_t *sess = NULL;
            for (int j = 0; j < LTP_MAX_EXPORT_SESSIONS; j++) {
                if (eng->export_sessions[j].active &&
                    eng->export_sessions[j].session_number == t->session_number) {
                    sess = &eng->export_sessions[j];
                    break;
                }
            }
            if (sess) {
                /* Retransmit the last data segment as a checkpoint */
                ltp_data_segment_t seg;
                memset(&seg, 0, sizeof(seg));
                seg.hdr.version = 0;
                seg.hdr.type = LTP_SEG_RED_DATA_EORP_CP;
                seg.hdr.sender_engine_id = sess->engine_id;
                seg.hdr.session_number = sess->session_number;
                seg.client_svc_id = 1;
                /* Retransmit the tail of the block */
                uint32_t mtu = sess->segment_mtu;
                if (mtu == 0) mtu = eng->config.segment_mtu;
                uint32_t offset = 0;
                if (sess->block_len > mtu)
                    offset = sess->block_len - mtu;
                seg.offset = offset;
                seg.length = sess->block_len - offset;
                seg.data = sess->block_data + offset;
                seg.cp_serial = sess->next_cp_serial++;
                seg.rpt_serial = 0;

                uint8_t ltp_buf[LTP_MAX_SEGMENT_BUF];
                int enc_len = ltp_encode_data_segment(&seg, ltp_buf, sizeof(ltp_buf));
                if (enc_len > 0 && fd >= 0) {
                    uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
                    int kiss_len = kiss_encode(ltp_buf, (size_t)enc_len,
                                               kiss_buf, sizeof(kiss_buf));
                    if (kiss_len > 0) {
                        ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                        (void)wr;
                        eng->segments_sent++;
                    }
                }
            }
        } else if (t->type == 1) {
            /* Report timer — retransmit report for import session */
            ltp_import_session_t *sess = NULL;
            for (int j = 0; j < LTP_MAX_IMPORT_SESSIONS; j++) {
                if (eng->import_sessions[j].active &&
                    eng->import_sessions[j].session_number == t->session_number) {
                    sess = &eng->import_sessions[j];
                    break;
                }
            }
            if (sess) {
                ltp_report_segment_t rpt;
                memset(&rpt, 0, sizeof(rpt));
                rpt.hdr.version = 0;
                rpt.hdr.type = LTP_SEG_REPORT;
                rpt.hdr.sender_engine_id = eng->local_engine_id;
                rpt.hdr.session_number = sess->session_number;
                rpt.rpt_serial = t->serial;
                rpt.cp_serial = 0;
                rpt.lower_bound = 0;
                rpt.upper_bound = sess->block_len > 0 ? sess->block_len : 0;
                rpt.claim_count = sess->recv_map.claim_count;
                for (uint32_t ci = 0; ci < rpt.claim_count && ci < LTP_MAX_CLAIMS; ci++) {
                    rpt.claims[ci] = sess->recv_map.claims[ci];
                }

                uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
                int rpt_len = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
                if (rpt_len > 0 && fd >= 0) {
                    uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
                    int kiss_len = kiss_encode(rpt_buf, (size_t)rpt_len,
                                               kiss_buf, sizeof(kiss_buf));
                    if (kiss_len > 0) {
                        ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                        (void)wr;
                        eng->segments_sent++;
                    }
                }
            }
        } else if (t->type == 2) {
            /* Cancel timer — retransmit cancel segment */
            ltp_cancel_segment_t cancel;
            memset(&cancel, 0, sizeof(cancel));
            cancel.hdr.version = 0;
            cancel.hdr.type = LTP_SEG_CANCEL_BY_SENDER;
            cancel.hdr.sender_engine_id = eng->local_engine_id;
            cancel.hdr.session_number = t->session_number;
            cancel.reason = 0;

            uint8_t cancel_buf[LTP_MAX_SEGMENT_BUF];
            int cancel_len = ltp_encode_cancel(&cancel, cancel_buf, sizeof(cancel_buf));
            if (cancel_len > 0 && fd >= 0) {
                uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
                int kiss_len = kiss_encode(cancel_buf, (size_t)cancel_len,
                                           kiss_buf, sizeof(kiss_buf));
                if (kiss_len > 0) {
                    ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                    (void)wr;
                    eng->segments_sent++;
                }
            }
        }

        /* Restart timer: expiry = now + duration */
        t->expiry = now;
        t->expiry.tv_sec += duration_ms / 1000;
        t->expiry.tv_nsec += (duration_ms % 1000) * 1000000;
        if (t->expiry.tv_nsec >= 1000000000) {
            t->expiry.tv_sec++;
            t->expiry.tv_nsec -= 1000000000;
        }

        fired++;
    }

    return fired;
}

int ltp_engine_run(ltp_engine_t *eng, int fd, int send_mode)
{
    if (!eng) return -1;

    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t read_buf[256];
    uint8_t kiss_payload[KISS_MAX_PAYLOAD];
    size_t kiss_payload_len = 0;

    /* For send_mode: find the session we're waiting on */
    uint64_t send_session = 0;
    if (send_mode) {
        /* Most recent session = next_session_number - 1 */
        send_session = eng->next_session_number - 1;
    }

    while (g_running) {
        int timeout = ltp_get_next_timeout_ms(eng);
        if (send_mode && timeout < 0) timeout = 1000; /* poll at least every 1s */
        /* recv_mode with no timers: timeout = -1 (block indefinitely) */

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        int pret = poll(&pfd, 1, timeout);
        if (pret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (pret == 0) {
            /* Timeout — fire expired timers */
            ltp_fire_expired_timers(eng, fd);
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, read_buf, sizeof(read_buf));
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            for (ssize_t i = 0; i < n; i++) {
                int rc = kiss_decoder_feed(&dec, read_buf[i],
                                           kiss_payload, sizeof(kiss_payload),
                                           &kiss_payload_len);
                if (rc == 1) {
                    if (aprs_is_ax25_frame(kiss_payload, kiss_payload_len)) {
                        aprs_log_packet(kiss_payload, kiss_payload_len,
                                        eng->config.verbose);
                        /* Auto-register sender as DTN endpoint for reverse lookup */
                        char asrc[16];
                        int alen = ax25_strip_frame(kiss_payload, kiss_payload_len,
                                                    asrc, NULL, NULL);
                        if (alen >= 0 && asrc[0] != '\0') {
                            /* Strip SSID "-0" suffix for clean EID, keep others */
                            char aeid[80];
                            snprintf(aeid, sizeof(aeid), "dtn://%s", asrc);
                            ltp_register_endpoint(eng, aeid);
                        }
                    } else {
                        ltp_process_segment(eng, fd, kiss_payload, kiss_payload_len);
                    }
                }
            }
        }

        /* Also fire timers after processing data (in case processing took time) */
        ltp_fire_expired_timers(eng, fd);

        /* send_mode: check if our session completed or was cancelled */
        if (send_mode) {
            for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
                if (eng->export_sessions[i].session_number == send_session) {
                    if (eng->export_sessions[i].completed)
                        return 0; /* success */
                    if (eng->export_sessions[i].cancelled)
                        return -1; /* cancelled */
                    break;
                }
            }
        }
    }

    return 0; /* signal exit */
}

int ltp_cancel_session(ltp_engine_t *eng, int fd,
                       uint64_t session_number)
{
    if (!eng) return -1;

    /* Find the export session */
    ltp_export_session_t *sess = NULL;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (eng->export_sessions[i].active &&
            eng->export_sessions[i].session_number == session_number) {
            sess = &eng->export_sessions[i];
            break;
        }
    }
    if (!sess) return -1;

    /* Send cancel-by-sender segment */
    ltp_cancel_segment_t cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.hdr.version = 0;
    cancel.hdr.type = LTP_SEG_CANCEL_BY_SENDER;
    cancel.hdr.sender_engine_id = eng->local_engine_id;
    cancel.hdr.session_number = session_number;
    cancel.reason = 0; /* USR_CNCLD */

    uint8_t buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_cancel(&cancel, buf, sizeof(buf));
    if (enc_len > 0 && fd >= 0) {
        uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
        int kiss_len = kiss_encode(buf, (size_t)enc_len, kiss_buf, sizeof(kiss_buf));
        if (kiss_len > 0) {
                ssize_t wr = write(fd, kiss_buf, (size_t)kiss_len);
                (void)wr;
            }
    }

    /* Start cancel retransmission timer */
    ltp_start_timer(eng, 2, sess->engine_id, session_number, 0);

    sess->cancelled = 1;
    return 0;
}

int ltp_register_endpoint(ltp_engine_t *eng, const char *eid)
{
    if (!eng || !eid) return -1;
    if (eng->endpoint_count >= LTP_MAX_ENDPOINTS) return -1;

    ltp_endpoint_t *ep = &eng->endpoints[eng->endpoint_count];
    strncpy(ep->eid, eid, sizeof(ep->eid) - 1);
    ep->eid[sizeof(ep->eid) - 1] = '\0';
    ep->engine_id = ltp_eid_to_engine_id(eid);
    eng->endpoint_count++;

    return 0;
}

const char *ltp_engine_id_to_eid(const ltp_engine_t *eng, uint64_t engine_id)
{
    if (!eng) return NULL;
    for (uint32_t i = 0; i < eng->endpoint_count; i++) {
        if (eng->endpoints[i].engine_id == engine_id)
            return eng->endpoints[i].eid;
    }
    return NULL;
}
