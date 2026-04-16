/*
 * test_ltp.c — Property-based and unit tests for LTP segment encoding/decoding
 *
 * Uses hand-rolled random testing with rand()/srand() (1000 iterations),
 * matching the existing test pattern in this project.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "ltp.h"
#include "sdnv.h"
#include "kiss.h"

/* Provide g_running for ltp.c's extern declaration (normally in main.c) */
volatile sig_atomic_t g_running = 1;

/* ================================================================== */
/* Test infrastructure                                                 */
/* ================================================================== */

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

#define ITERATIONS 1000
#define HEAVY_ITERATIONS 100  /* For tests involving pipe I/O per iteration */

/* Helper: generate a random uint64_t in [0, max] */
static uint64_t rand_uint64_max(uint64_t max)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u = (u << 8) | (uint8_t)(rand() & 0xFF);
    /* Ensure value is in [0, 2^63-1] first */
    u >>= 1;
    if (max < (uint64_t)INT64_MAX)
        u = u % (max + 1);
    return u;
}

/* Helper: generate a random uint32_t in [0, max] */
static uint32_t rand_uint32_max(uint32_t max)
{
    return (uint32_t)(rand_uint64_max(max));
}

/* Helper: fill buffer with random bytes */
static void rand_bytes(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);
}

/* ================================================================== */
/* Property 3: LTP data segment encode/decode round-trip               */
/* Feature: ltp-over-kiss, Property 3: LTP data segment round-trip     */
/* Validates: Requirements 1.1, 1.2, 1.7, 2.1                         */
/* ================================================================== */

static int test_ltp_data_segment_roundtrip(void)
{
    uint8_t encoded[LTP_MAX_SEGMENT_BUF];
    uint8_t body[LTP_MAX_SEGMENT_BUF];
    uint8_t payload[64];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Generate random valid data segment */
        ltp_seg_type_t types[] = {
            LTP_SEG_RED_DATA, LTP_SEG_RED_DATA_CP,
            LTP_SEG_RED_DATA_EORP_CP, LTP_SEG_GREEN_DATA,
            LTP_SEG_GREEN_DATA_EOB
        };
        ltp_seg_type_t type = types[rand() % 5];

        uint64_t engine_id = rand_uint64_max((uint64_t)UINT32_MAX);
        uint64_t session_num = rand_uint64_max(0xFFFFFF);
        uint64_t offset = rand_uint64_max(1023);
        uint32_t payload_len = rand_uint32_max(64);
        uint64_t cp_serial = rand_uint64_max(0xFFFF);
        uint64_t rpt_serial = rand_uint64_max(0xFFFF);

        rand_bytes(payload, payload_len);

        ltp_data_segment_t seg_in;
        memset(&seg_in, 0, sizeof(seg_in));
        seg_in.hdr.version = 0;
        seg_in.hdr.type = type;
        seg_in.hdr.sender_engine_id = engine_id;
        seg_in.hdr.session_number = session_num;
        seg_in.hdr.hdr_ext_count = 0;
        seg_in.hdr.trailer_ext_count = 0;
        seg_in.client_svc_id = 1;
        seg_in.offset = offset;
        seg_in.length = payload_len;
        seg_in.cp_serial = cp_serial;
        seg_in.rpt_serial = rpt_serial;
        seg_in.data = payload;

        /* Encode */
        int enc_len = ltp_encode_data_segment(&seg_in, encoded, sizeof(encoded));
        if (enc_len < 0) {
            printf("\n    FAIL at iter %d: encode returned %d\n", iter, enc_len);
            return 0;
        }

        /* Decode header */
        ltp_segment_hdr_t hdr;
        size_t body_len = 0;
        int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                    body, sizeof(body), &body_len);
        if (rc < 0) {
            printf("\n    FAIL at iter %d: decode_segment returned %d\n", iter, rc);
            return 0;
        }

        /* Verify header fields */
        if (hdr.version != 0) {
            printf("\n    FAIL at iter %d: version=%u expected 0\n",
                   iter, hdr.version);
            return 0;
        }
        if (hdr.type != type) {
            printf("\n    FAIL at iter %d: type=%d expected %d\n",
                   iter, hdr.type, type);
            return 0;
        }
        if (hdr.sender_engine_id != engine_id) {
            printf("\n    FAIL at iter %d: engine_id mismatch\n", iter);
            return 0;
        }
        if (hdr.session_number != session_num) {
            printf("\n    FAIL at iter %d: session_number mismatch\n", iter);
            return 0;
        }

        /* Decode data content */
        ltp_data_segment_t seg_out;
        memset(&seg_out, 0, sizeof(seg_out));
        rc = ltp_decode_data_content(body, body_len, type, &seg_out);
        if (rc < 0) {
            printf("\n    FAIL at iter %d: decode_data_content returned %d\n",
                   iter, rc);
            return 0;
        }

        /* Verify data fields */
        if (seg_out.client_svc_id != seg_in.client_svc_id) {
            printf("\n    FAIL at iter %d: client_svc_id mismatch\n", iter);
            return 0;
        }
        if (seg_out.offset != seg_in.offset) {
            printf("\n    FAIL at iter %d: offset mismatch\n", iter);
            return 0;
        }
        if (seg_out.length != seg_in.length) {
            printf("\n    FAIL at iter %d: length mismatch\n", iter);
            return 0;
        }

        /* Verify checkpoint serials for checkpoint types */
        if (type == LTP_SEG_RED_DATA_CP || type == LTP_SEG_RED_DATA_EORP_CP) {
            if (seg_out.cp_serial != seg_in.cp_serial) {
                printf("\n    FAIL at iter %d: cp_serial mismatch\n", iter);
                return 0;
            }
            if (seg_out.rpt_serial != seg_in.rpt_serial) {
                printf("\n    FAIL at iter %d: rpt_serial mismatch\n", iter);
                return 0;
            }
        }

        /* Verify payload data */
        if (payload_len > 0 && seg_out.data) {
            if (memcmp(seg_out.data, payload, payload_len) != 0) {
                printf("\n    FAIL at iter %d: payload data mismatch\n", iter);
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Property 4: LTP control segment encode/decode round-trip            */
/* Feature: ltp-over-kiss, Property 4: LTP control segment round-trip  */
/* Validates: Requirements 1.7, 2.2                                    */
/* ================================================================== */

static int test_ltp_control_segment_roundtrip(void)
{
    uint8_t encoded[LTP_MAX_SEGMENT_BUF];
    uint8_t body[LTP_MAX_SEGMENT_BUF];

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Randomly choose: report, report_ack, or cancel */
        int choice = rand() % 3;

        if (choice == 0) {
            /* ---- Report segment ---- */
            ltp_report_segment_t rpt_in;
            memset(&rpt_in, 0, sizeof(rpt_in));
            rpt_in.hdr.version = 0;
            rpt_in.hdr.type = LTP_SEG_REPORT;
            rpt_in.hdr.sender_engine_id = rand_uint64_max((uint64_t)UINT32_MAX);
            rpt_in.hdr.session_number = rand_uint64_max(0xFFFFFF);
            rpt_in.hdr.hdr_ext_count = 0;
            rpt_in.hdr.trailer_ext_count = 0;
            rpt_in.rpt_serial = rand_uint64_max(0xFFFF);
            rpt_in.cp_serial = rand_uint64_max(0xFFFF);
            rpt_in.lower_bound = rand_uint64_max(512);
            rpt_in.upper_bound = rpt_in.lower_bound + rand_uint64_max(512) + 1;
            rpt_in.claim_count = rand_uint32_max(LTP_MAX_CLAIMS);

            for (uint32_t i = 0; i < rpt_in.claim_count; i++) {
                rpt_in.claims[i].offset = rand_uint64_max(255);
                rpt_in.claims[i].length = rand_uint64_max(255) + 1;
            }

            /* Encode */
            int enc_len = ltp_encode_report(&rpt_in, encoded, sizeof(encoded));
            if (enc_len < 0) {
                printf("\n    FAIL at iter %d: encode_report returned %d\n",
                       iter, enc_len);
                return 0;
            }

            /* Decode header */
            ltp_segment_hdr_t hdr;
            size_t body_len = 0;
            int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                        body, sizeof(body), &body_len);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_segment (report) returned %d\n",
                       iter, rc);
                return 0;
            }

            if (hdr.type != LTP_SEG_REPORT) {
                printf("\n    FAIL at iter %d: report type=%d expected %d\n",
                       iter, hdr.type, LTP_SEG_REPORT);
                return 0;
            }
            if (hdr.sender_engine_id != rpt_in.hdr.sender_engine_id) {
                printf("\n    FAIL at iter %d: report engine_id mismatch\n", iter);
                return 0;
            }
            if (hdr.session_number != rpt_in.hdr.session_number) {
                printf("\n    FAIL at iter %d: report session_number mismatch\n", iter);
                return 0;
            }

            /* Decode report content */
            ltp_report_segment_t rpt_out;
            memset(&rpt_out, 0, sizeof(rpt_out));
            rc = ltp_decode_report_content(body, body_len, &rpt_out);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_report_content returned %d\n",
                       iter, rc);
                return 0;
            }

            if (rpt_out.rpt_serial != rpt_in.rpt_serial ||
                rpt_out.cp_serial != rpt_in.cp_serial ||
                rpt_out.upper_bound != rpt_in.upper_bound ||
                rpt_out.lower_bound != rpt_in.lower_bound ||
                rpt_out.claim_count != rpt_in.claim_count) {
                printf("\n    FAIL at iter %d: report field mismatch\n", iter);
                return 0;
            }

            for (uint32_t i = 0; i < rpt_in.claim_count; i++) {
                if (rpt_out.claims[i].offset != rpt_in.claims[i].offset ||
                    rpt_out.claims[i].length != rpt_in.claims[i].length) {
                    printf("\n    FAIL at iter %d: claim %u mismatch\n", iter, i);
                    return 0;
                }
            }

        } else if (choice == 1) {
            /* ---- Report ack segment ---- */
            ltp_report_ack_segment_t ack_in;
            memset(&ack_in, 0, sizeof(ack_in));
            ack_in.hdr.version = 0;
            ack_in.hdr.type = LTP_SEG_REPORT_ACK;
            ack_in.hdr.sender_engine_id = rand_uint64_max((uint64_t)UINT32_MAX);
            ack_in.hdr.session_number = rand_uint64_max(0xFFFFFF);
            ack_in.hdr.hdr_ext_count = 0;
            ack_in.hdr.trailer_ext_count = 0;
            ack_in.rpt_serial = rand_uint64_max(0xFFFF);

            /* Encode */
            int enc_len = ltp_encode_report_ack(&ack_in, encoded, sizeof(encoded));
            if (enc_len < 0) {
                printf("\n    FAIL at iter %d: encode_report_ack returned %d\n",
                       iter, enc_len);
                return 0;
            }

            /* Decode header */
            ltp_segment_hdr_t hdr;
            size_t body_len = 0;
            int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                        body, sizeof(body), &body_len);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_segment (ack) returned %d\n",
                       iter, rc);
                return 0;
            }

            if (hdr.type != LTP_SEG_REPORT_ACK) {
                printf("\n    FAIL at iter %d: ack type=%d expected %d\n",
                       iter, hdr.type, LTP_SEG_REPORT_ACK);
                return 0;
            }

            /* Decode ack content */
            ltp_report_ack_segment_t ack_out;
            memset(&ack_out, 0, sizeof(ack_out));
            rc = ltp_decode_report_ack_content(body, body_len, &ack_out);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_report_ack_content returned %d\n",
                       iter, rc);
                return 0;
            }

            if (ack_out.rpt_serial != ack_in.rpt_serial) {
                printf("\n    FAIL at iter %d: ack rpt_serial mismatch\n", iter);
                return 0;
            }

        } else {
            /* ---- Cancel segment ---- */
            ltp_seg_type_t cancel_types[] = {
                LTP_SEG_CANCEL_BY_SENDER, LTP_SEG_CANCEL_ACK_SENDER,
                LTP_SEG_CANCEL_BY_RECVR, LTP_SEG_CANCEL_ACK_RECVR
            };
            ltp_cancel_segment_t cancel_in;
            memset(&cancel_in, 0, sizeof(cancel_in));
            cancel_in.hdr.version = 0;
            cancel_in.hdr.type = cancel_types[rand() % 4];
            cancel_in.hdr.sender_engine_id = rand_uint64_max((uint64_t)UINT32_MAX);
            cancel_in.hdr.session_number = rand_uint64_max(0xFFFFFF);
            cancel_in.hdr.hdr_ext_count = 0;
            cancel_in.hdr.trailer_ext_count = 0;
            cancel_in.reason = (uint8_t)(rand() & 0xFF);

            /* Encode */
            int enc_len = ltp_encode_cancel(&cancel_in, encoded, sizeof(encoded));
            if (enc_len < 0) {
                printf("\n    FAIL at iter %d: encode_cancel returned %d\n",
                       iter, enc_len);
                return 0;
            }

            /* Decode header */
            ltp_segment_hdr_t hdr;
            size_t body_len = 0;
            int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                        body, sizeof(body), &body_len);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_segment (cancel) returned %d\n",
                       iter, rc);
                return 0;
            }

            if (hdr.type != cancel_in.hdr.type) {
                printf("\n    FAIL at iter %d: cancel type=%d expected %d\n",
                       iter, hdr.type, cancel_in.hdr.type);
                return 0;
            }

            /* Decode cancel content */
            ltp_cancel_segment_t cancel_out;
            memset(&cancel_out, 0, sizeof(cancel_out));
            rc = ltp_decode_cancel_content(body, body_len, &cancel_out);
            if (rc < 0) {
                printf("\n    FAIL at iter %d: decode_cancel_content returned %d\n",
                       iter, rc);
                return 0;
            }

            if (cancel_out.reason != cancel_in.reason) {
                printf("\n    FAIL at iter %d: cancel reason mismatch: "
                       "got %u expected %u\n",
                       iter, cancel_out.reason, cancel_in.reason);
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================== */
/* Unit tests for segment encoding edge cases                          */
/* Requirements: 2.3, 6.5                                              */
/* ================================================================== */

/* Test: decode segment with invalid type (5) returns error */
static int test_decode_invalid_type_5(void)
{
    /* Build a minimal segment with type 5 (reserved) */
    uint8_t buf[32];
    size_t pos = 0;

    /* Byte 0: version=0, type=5 */
    buf[pos++] = 0x05;

    /* SDNV engine ID = 1 */
    int n = sdnv_encode(1, buf + pos, sizeof(buf) - pos);
    if (n < 0) return 0;
    pos += (size_t)n;

    /* SDNV session number = 1 */
    n = sdnv_encode(1, buf + pos, sizeof(buf) - pos);
    if (n < 0) return 0;
    pos += (size_t)n;

    /* Extension counts */
    buf[pos++] = 0;
    buf[pos++] = 0;

    ltp_segment_hdr_t hdr;
    uint8_t body[32];
    size_t body_len = 0;
    int rc = ltp_decode_segment(buf, pos, &hdr, body, sizeof(body), &body_len);
    if (rc != -1) {
        printf("\n    FAIL: decode of type 5 returned %d, expected -1\n", rc);
        return 0;
    }
    return 1;
}

/* Test: segment with type 0 (red data, no checkpoint) encodes/decodes correctly */
static int test_type0_red_data_no_checkpoint(void)
{
    uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    ltp_data_segment_t seg_in;
    memset(&seg_in, 0, sizeof(seg_in));
    seg_in.hdr.version = 0;
    seg_in.hdr.type = LTP_SEG_RED_DATA;
    seg_in.hdr.sender_engine_id = 42;
    seg_in.hdr.session_number = 7;
    seg_in.hdr.hdr_ext_count = 0;
    seg_in.hdr.trailer_ext_count = 0;
    seg_in.client_svc_id = 1;
    seg_in.offset = 0;
    seg_in.length = 4;
    seg_in.data = payload;

    uint8_t encoded[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_data_segment(&seg_in, encoded, sizeof(encoded));
    if (enc_len < 0) {
        printf("\n    FAIL: encode returned %d\n", enc_len);
        return 0;
    }

    /* Decode header */
    ltp_segment_hdr_t hdr;
    uint8_t body[LTP_MAX_SEGMENT_BUF];
    size_t body_len = 0;
    int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                body, sizeof(body), &body_len);
    if (rc < 0) {
        printf("\n    FAIL: decode_segment returned %d\n", rc);
        return 0;
    }

    if (hdr.type != LTP_SEG_RED_DATA) {
        printf("\n    FAIL: type=%d expected %d\n", hdr.type, LTP_SEG_RED_DATA);
        return 0;
    }
    if (hdr.sender_engine_id != 42) {
        printf("\n    FAIL: engine_id=%lu expected 42\n",
               (unsigned long)hdr.sender_engine_id);
        return 0;
    }

    /* Decode data content */
    ltp_data_segment_t seg_out;
    memset(&seg_out, 0, sizeof(seg_out));
    rc = ltp_decode_data_content(body, body_len, LTP_SEG_RED_DATA, &seg_out);
    if (rc < 0) {
        printf("\n    FAIL: decode_data_content returned %d\n", rc);
        return 0;
    }

    if (seg_out.offset != 0 || seg_out.length != 4) {
        printf("\n    FAIL: offset=%lu length=%lu\n",
               (unsigned long)seg_out.offset, (unsigned long)seg_out.length);
        return 0;
    }

    /* Type 0 should NOT have checkpoint serials */
    if (seg_out.cp_serial != 0 || seg_out.rpt_serial != 0) {
        printf("\n    FAIL: type 0 should have cp_serial=0, rpt_serial=0\n");
        return 0;
    }

    /* Verify payload */
    if (!seg_out.data || memcmp(seg_out.data, payload, 4) != 0) {
        printf("\n    FAIL: payload mismatch\n");
        return 0;
    }

    return 1;
}

/* Test: segment with type 2 (red data + EoRP + checkpoint) includes CP and RPT serials */
static int test_type2_eorp_checkpoint(void)
{
    uint8_t payload[] = { 0xCA, 0xFE };
    ltp_data_segment_t seg_in;
    memset(&seg_in, 0, sizeof(seg_in));
    seg_in.hdr.version = 0;
    seg_in.hdr.type = LTP_SEG_RED_DATA_EORP_CP;
    seg_in.hdr.sender_engine_id = 100;
    seg_in.hdr.session_number = 3;
    seg_in.hdr.hdr_ext_count = 0;
    seg_in.hdr.trailer_ext_count = 0;
    seg_in.client_svc_id = 1;
    seg_in.offset = 64;
    seg_in.length = 2;
    seg_in.cp_serial = 5;
    seg_in.rpt_serial = 0;
    seg_in.data = payload;

    uint8_t encoded[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_data_segment(&seg_in, encoded, sizeof(encoded));
    if (enc_len < 0) {
        printf("\n    FAIL: encode returned %d\n", enc_len);
        return 0;
    }

    /* Decode header */
    ltp_segment_hdr_t hdr;
    uint8_t body[LTP_MAX_SEGMENT_BUF];
    size_t body_len = 0;
    int rc = ltp_decode_segment(encoded, (size_t)enc_len, &hdr,
                                body, sizeof(body), &body_len);
    if (rc < 0) {
        printf("\n    FAIL: decode_segment returned %d\n", rc);
        return 0;
    }

    if (hdr.type != LTP_SEG_RED_DATA_EORP_CP) {
        printf("\n    FAIL: type=%d expected %d\n",
               hdr.type, LTP_SEG_RED_DATA_EORP_CP);
        return 0;
    }

    /* Decode data content */
    ltp_data_segment_t seg_out;
    memset(&seg_out, 0, sizeof(seg_out));
    rc = ltp_decode_data_content(body, body_len, LTP_SEG_RED_DATA_EORP_CP, &seg_out);
    if (rc < 0) {
        printf("\n    FAIL: decode_data_content returned %d\n", rc);
        return 0;
    }

    if (seg_out.cp_serial != 5) {
        printf("\n    FAIL: cp_serial=%lu expected 5\n",
               (unsigned long)seg_out.cp_serial);
        return 0;
    }
    if (seg_out.rpt_serial != 0) {
        printf("\n    FAIL: rpt_serial=%lu expected 0\n",
               (unsigned long)seg_out.rpt_serial);
        return 0;
    }
    if (seg_out.offset != 64 || seg_out.length != 2) {
        printf("\n    FAIL: offset=%lu length=%lu\n",
               (unsigned long)seg_out.offset, (unsigned long)seg_out.length);
        return 0;
    }

    /* Verify payload */
    if (!seg_out.data || memcmp(seg_out.data, payload, 2) != 0) {
        printf("\n    FAIL: payload mismatch\n");
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* Property 10: Engine ID hash determinism                             */
/* Feature: ltp-over-kiss, Property 10: Engine ID hash determinism     */
/* Validates: Requirements 8.2                                         */
/* ================================================================== */

/* Helper: generate a random callsign string (1-6 uppercase alphanumeric chars) */
static void rand_callsign(char *buf, size_t max_len)
{
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int len = 1 + (rand() % 6);  /* 1 to 6 chars */
    if ((size_t)len >= max_len) len = (int)(max_len - 1);
    for (int i = 0; i < len; i++)
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    buf[len] = '\0';
}

static int test_engine_id_determinism(void)
{
    /* Part 1: Same callsign produces same engine ID (1000 iterations) */
    for (int iter = 0; iter < ITERATIONS; iter++) {
        char callsign[8];
        rand_callsign(callsign, sizeof(callsign));

        uint64_t id1 = ltp_eid_to_engine_id(callsign);
        uint64_t id2 = ltp_eid_to_engine_id(callsign);
        if (id1 != id2) {
            printf("\n    FAIL at iter %d: same callsign '%s' produced "
                   "different IDs: %lu vs %lu\n",
                   iter, callsign,
                   (unsigned long)id1, (unsigned long)id2);
            return 0;
        }

        /* Part 2: "dtn://" prefix is stripped — bare callsign and
         * prefixed callsign produce the same engine ID */
        char prefixed[72];
        snprintf(prefixed, sizeof(prefixed), "dtn://%s", callsign);
        uint64_t id_prefixed = ltp_eid_to_engine_id(prefixed);
        if (id_prefixed != id1) {
            printf("\n    FAIL at iter %d: '%s' and '%s' produced "
                   "different IDs: %lu vs %lu\n",
                   iter, callsign, prefixed,
                   (unsigned long)id1, (unsigned long)id_prefixed);
            return 0;
        }
    }

    /* Part 3: Different callsigns produce different engine IDs
     * (check 1000 random pairs for no collisions) */
    for (int iter = 0; iter < ITERATIONS; iter++) {
        char cs1[8], cs2[8];
        rand_callsign(cs1, sizeof(cs1));
        rand_callsign(cs2, sizeof(cs2));

        /* Only test if the callsigns are actually different */
        if (strcmp(cs1, cs2) == 0)
            continue;

        uint64_t id1 = ltp_eid_to_engine_id(cs1);
        uint64_t id2 = ltp_eid_to_engine_id(cs2);
        if (id1 == id2) {
            printf("\n    FAIL at iter %d: different callsigns '%s' and '%s' "
                   "produced same ID: %lu\n",
                   iter, cs1, cs2, (unsigned long)id1);
            return 0;
        }
    }

    return 1;
}

/* ================================================================== */
/* Property 5: Block segmentation completeness                         */
/* Feature: ltp-over-kiss, Property 5: Block segmentation completeness */
/* Validates: Requirements 3.1, 4.1, 6.1                               */
/* ================================================================== */

/* Callback context for collecting segments */
typedef struct {
    ltp_data_segment_t segs[256];
    uint8_t seg_data[256][256]; /* copy of each segment's data */
    int count;
} seg_collector_t;

static void collect_segment(const ltp_data_segment_t *seg, void *ctx)
{
    seg_collector_t *col = (seg_collector_t *)ctx;
    if (col->count >= 256) return;
    col->segs[col->count] = *seg;
    /* Copy the data so it persists */
    if (seg->length > 0 && seg->data && seg->length <= 256) {
        memcpy(col->seg_data[col->count], seg->data, (size_t)seg->length);
        col->segs[col->count].data = col->seg_data[col->count];
    }
    col->count++;
}

static int test_block_segmentation_completeness(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint32_t block_len = 1 + rand_uint32_max(1023); /* 1-1024 */
        uint32_t mtu = 16 + rand_uint32_max(240);       /* 16-256 */

        uint8_t block[LTP_MAX_BLOCK_SIZE];
        rand_bytes(block, block_len);

        ltp_engine_t eng;
        ltp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.segment_mtu = mtu;
        cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
        cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
        cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
        ltp_engine_init(&eng, "dtn://test", &cfg);

        seg_collector_t col;
        memset(&col, 0, sizeof(col));

        uint64_t remote_eid = ltp_eid_to_engine_id("dtn://remote");
        int seg_count = ltp_segment_block(&eng, remote_eid, block, block_len,
                                          collect_segment, &col);

        /* Verify segment count = ceil(block_len / mtu) */
        uint32_t expected = (block_len + mtu - 1) / mtu;
        if (seg_count < 0 || (uint32_t)seg_count != expected) {
            printf("\n    FAIL at iter %d: seg_count=%d expected=%u "
                   "(block_len=%u mtu=%u)\n",
                   iter, seg_count, expected, block_len, mtu);
            return 0;
        }
        if ((uint32_t)col.count != expected) {
            printf("\n    FAIL at iter %d: collected %d expected %u\n",
                   iter, col.count, expected);
            return 0;
        }

        /* Verify each segment's data length <= MTU */
        for (int i = 0; i < col.count; i++) {
            if (col.segs[i].length > mtu) {
                printf("\n    FAIL at iter %d: seg %d length=%lu > mtu=%u\n",
                       iter, i, (unsigned long)col.segs[i].length, mtu);
                return 0;
            }
        }

        /* Verify segments cover [0, block_len) without gaps/overlaps */
        uint64_t covered = 0;
        for (int i = 0; i < col.count; i++) {
            if (col.segs[i].offset != covered) {
                printf("\n    FAIL at iter %d: seg %d offset=%lu expected=%lu\n",
                       iter, i, (unsigned long)col.segs[i].offset,
                       (unsigned long)covered);
                return 0;
            }
            covered += col.segs[i].length;
        }
        if (covered != block_len) {
            printf("\n    FAIL at iter %d: total covered=%lu expected=%u\n",
                   iter, (unsigned long)covered, block_len);
            return 0;
        }

        /* Verify final segment is type 2 with non-zero CP serial */
        ltp_data_segment_t *last = &col.segs[col.count - 1];
        if (last->hdr.type != LTP_SEG_RED_DATA_EORP_CP) {
            printf("\n    FAIL at iter %d: last seg type=%d expected %d\n",
                   iter, last->hdr.type, LTP_SEG_RED_DATA_EORP_CP);
            return 0;
        }
        if (last->cp_serial == 0) {
            printf("\n    FAIL at iter %d: last seg cp_serial=0\n", iter);
            return 0;
        }

        /* Verify non-last segments are type 0 */
        for (int i = 0; i < col.count - 1; i++) {
            if (col.segs[i].hdr.type != LTP_SEG_RED_DATA) {
                printf("\n    FAIL at iter %d: seg %d type=%d expected %d\n",
                       iter, i, col.segs[i].hdr.type, LTP_SEG_RED_DATA);
                return 0;
            }
        }

        /* Clean up: deactivate the export session */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++)
            eng.export_sessions[i].active = 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 6: Block segmentation/reassembly round-trip                */
/* Feature: ltp-over-kiss, Property 6: Block segmentation/reassembly   */
/* Validates: Requirements 3.5, 6.2, 6.3                               */
/* ================================================================== */

/* Helper: simple Fisher-Yates shuffle for int array */
static void shuffle_ints(int *arr, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int test_block_segmentation_reassembly_roundtrip(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Limit segments to avoid recv_map overflow (LTP_MAX_CLAIMS=16)
         * when segments arrive in random order */
        uint32_t mtu = 16 + rand_uint32_max(240);       /* 16-256 */
        uint32_t max_segs = LTP_MAX_CLAIMS;
        uint32_t max_block = max_segs * mtu;
        if (max_block > LTP_MAX_BLOCK_SIZE) max_block = LTP_MAX_BLOCK_SIZE;
        uint32_t block_len = 1 + rand_uint32_max(max_block - 1);

        uint8_t block[LTP_MAX_BLOCK_SIZE];
        rand_bytes(block, block_len);

        /* Segment the block */
        ltp_engine_t eng;
        ltp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.segment_mtu = mtu;
        cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
        cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
        cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
        ltp_engine_init(&eng, "dtn://sender", &cfg);

        seg_collector_t col;
        memset(&col, 0, sizeof(col));

        uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
        int seg_count = ltp_segment_block(&eng, remote_eid, block, block_len,
                                          collect_segment, &col);
        if (seg_count < 0) {
            printf("\n    FAIL at iter %d: segment_block returned %d\n",
                   iter, seg_count);
            return 0;
        }

        /* Create a receiver engine */
        ltp_engine_t recv_eng;
        ltp_engine_init(&recv_eng, "dtn://receiver", &cfg);

        /* Shuffle segment order */
        int order[256];
        for (int i = 0; i < col.count; i++) order[i] = i;
        shuffle_ints(order, col.count);

        /* Feed segments in random order to the receiver */
        uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
        int delivered = 0;
        uint8_t delivered_data[LTP_MAX_BLOCK_SIZE];
        uint32_t delivered_len = 0;

        /* Set up callback to capture delivered block */
        /* We'll use a simple approach: check import session state after feeding */

        for (int i = 0; i < col.count; i++) {
            int idx = order[i];
            int enc_len = ltp_encode_data_segment(&col.segs[idx],
                                                  enc_buf, sizeof(enc_buf));
            if (enc_len < 0) {
                printf("\n    FAIL at iter %d: encode seg %d returned %d\n",
                       iter, idx, enc_len);
                return 0;
            }

            ltp_process_segment(&recv_eng, -1, enc_buf, (size_t)enc_len);
        }

        /* Find the import session and check if block was delivered */
        int found = 0;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            ltp_import_session_t *sess = &recv_eng.import_sessions[i];
            /* Session may have been closed (active=0) after delivery */
            if (sess->delivered) {
                delivered = 1;
                delivered_len = sess->block_len;
                memcpy(delivered_data, sess->block_data, delivered_len);
                found = 1;
                break;
            }
        }

        /* If not found via delivered flag (session closed), check via callback */
        if (!found) {
            /* The session was closed and active=0, but delivered flag persists.
             * Let's try again with a callback approach. */
        }

        if (!delivered) {
            printf("\n    FAIL at iter %d: block not delivered "
                   "(block_len=%u mtu=%u segs=%d)\n",
                   iter, block_len, mtu, col.count);
            return 0;
        }

        if (delivered_len != block_len) {
            printf("\n    FAIL at iter %d: delivered_len=%u expected=%u\n",
                   iter, delivered_len, block_len);
            return 0;
        }

        if (memcmp(delivered_data, block, block_len) != 0) {
            printf("\n    FAIL at iter %d: delivered data mismatch\n", iter);
            return 0;
        }

        /* Clean up */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++)
            eng.export_sessions[i].active = 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests: Session limits and block size limits                    */
/* Feature: ltp-over-kiss, Task 6.6                                    */
/* Validates: Requirements 3.6, 3.7, 3.8, 6.4                         */
/* ================================================================== */

/* Test: max export sessions enforced (fill all 128 slots, 129th returns -1) */
static int test_max_export_sessions(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://test", &cfg);

    uint8_t data[] = { 0x42 };

    /* We need a valid fd for ltp_send_block. Use a pipe. */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("\n    FAIL: pipe() failed\n");
        return 0;
    }

    /* Fill all 128 export session slots */
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        int rc = ltp_send_block(&eng, pipefd[1], "dtn://remote", data, 1);
        if (rc != 0) {
            printf("\n    FAIL: send_block %d returned %d\n", i, rc);
            close(pipefd[0]);
            close(pipefd[1]);
            return 0;
        }
    }

    /* 129th should fail */
    int rc = ltp_send_block(&eng, pipefd[1], "dtn://remote", data, 1);
    if (rc != -1) {
        printf("\n    FAIL: 129th send_block returned %d, expected -1\n", rc);
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }

    close(pipefd[0]);
    close(pipefd[1]);
    return 1;
}

/* Test: max import sessions enforced (fill all 128 slots, 129th is rejected) */
static int test_max_import_sessions(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://receiver", &cfg);

    /* Fill all 128 import session slots by sending data segments
     * with different session numbers */
    uint8_t payload[] = { 0xAA };
    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];

    for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
        ltp_data_segment_t seg;
        memset(&seg, 0, sizeof(seg));
        seg.hdr.version = 0;
        seg.hdr.type = LTP_SEG_RED_DATA; /* type 0, no checkpoint — session stays open */
        seg.hdr.sender_engine_id = 12345;
        seg.hdr.session_number = (uint64_t)(i + 1);
        seg.hdr.hdr_ext_count = 0;
        seg.hdr.trailer_ext_count = 0;
        seg.client_svc_id = 1;
        seg.offset = 0;
        seg.length = 1;
        seg.data = payload;

        int enc_len = ltp_encode_data_segment(&seg, enc_buf, sizeof(enc_buf));
        if (enc_len < 0) {
            printf("\n    FAIL: encode seg %d returned %d\n", i, enc_len);
            return 0;
        }

        int rc = ltp_process_segment(&eng, -1, enc_buf, (size_t)enc_len);
        if (rc != 0) {
            printf("\n    FAIL: process_segment %d returned %d\n", i, rc);
            return 0;
        }
    }

    /* 129th session should be rejected */
    ltp_data_segment_t seg;
    memset(&seg, 0, sizeof(seg));
    seg.hdr.version = 0;
    seg.hdr.type = LTP_SEG_RED_DATA;
    seg.hdr.sender_engine_id = 12345;
    seg.hdr.session_number = 999;
    seg.hdr.hdr_ext_count = 0;
    seg.hdr.trailer_ext_count = 0;
    seg.client_svc_id = 1;
    seg.offset = 0;
    seg.length = 1;
    seg.data = payload;

    int enc_len = ltp_encode_data_segment(&seg, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) {
        printf("\n    FAIL: encode 129th seg returned %d\n", enc_len);
        return 0;
    }

    int rc = ltp_process_segment(&eng, -1, enc_buf, (size_t)enc_len);
    if (rc != -1) {
        printf("\n    FAIL: 129th import session returned %d, expected -1\n", rc);
        return 0;
    }

    return 1;
}

/* Test: 1024-byte block accepted by ltp_send_block */
static int test_block_1024_accepted(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://test", &cfg);

    uint8_t data[1024];
    memset(data, 0xAB, sizeof(data));

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("\n    FAIL: pipe() failed\n");
        return 0;
    }

    int rc = ltp_send_block(&eng, pipefd[1], "dtn://remote", data, 1024);
    close(pipefd[0]);
    close(pipefd[1]);

    if (rc != 0) {
        printf("\n    FAIL: 1024-byte block returned %d, expected 0\n", rc);
        return 0;
    }
    return 1;
}

/* Test: 1025-byte block rejected by ltp_send_block */
static int test_block_1025_rejected(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://test", &cfg);

    uint8_t data[1025];
    memset(data, 0xAB, sizeof(data));

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("\n    FAIL: pipe() failed\n");
        return 0;
    }

    int rc = ltp_send_block(&eng, pipefd[1], "dtn://remote", data, 1025);
    close(pipefd[0]);
    close(pipefd[1]);

    if (rc != -1) {
        printf("\n    FAIL: 1025-byte block returned %d, expected -1\n", rc);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Helper: decode a KISS frame from a pipe fd, extract LTP payload     */
/* ================================================================== */

static int read_kiss_frame(int pipe_read_fd, uint8_t *out, size_t out_size)
{
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t byte;
    size_t out_len = 0;
    int max_bytes = LTP_MAX_SEGMENT_BUF * 3;

    /* Set non-blocking so we don't hang when pipe is drained */
    int flags = fcntl(pipe_read_fd, F_GETFL, 0);
    fcntl(pipe_read_fd, F_SETFL, flags | O_NONBLOCK);

    for (int i = 0; i < max_bytes; i++) {
        ssize_t n = read(pipe_read_fd, &byte, 1);
        if (n <= 0) {
            fcntl(pipe_read_fd, F_SETFL, flags); /* restore */
            return -1;
        }

        int rc = kiss_decoder_feed(&dec, byte, out, out_size, &out_len);
        if (rc == 1) {
            fcntl(pipe_read_fd, F_SETFL, flags);
            return (int)out_len;
        }
        if (rc < 0) continue;
    }
    fcntl(pipe_read_fd, F_SETFL, flags);
    return -1;
}

/* Helper: read multiple KISS frames from pipe */
static int read_all_kiss_frames(int pipe_read_fd,
                                uint8_t frames[][LTP_MAX_SEGMENT_BUF],
                                int *frame_lens, int max_frames)
{
    int count = 0;
    while (count < max_frames) {
        int len = read_kiss_frame(pipe_read_fd, frames[count],
                                  LTP_MAX_SEGMENT_BUF);
        if (len < 0) break;
        frame_lens[count] = len;
        count++;
    }
    return count;
}

/* ================================================================== */
/* Property 7: Reception report claims accuracy                        */
/* Feature: ltp-over-kiss, Property 7: Reception report claims accuracy*/
/* Validates: Requirements 4.2                                         */
/* ================================================================== */

static int test_reception_report_claims_accuracy(void)
{
    for (int iter = 0; iter < HEAVY_ITERATIONS; iter++) {
        uint32_t block_len = 1 + rand_uint32_max(255);
        uint32_t mtu = 16 + rand_uint32_max(48);

        uint8_t block[LTP_MAX_BLOCK_SIZE];
        rand_bytes(block, block_len);

        /* Segment the block */
        ltp_engine_t send_eng;
        ltp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.segment_mtu = mtu;
        cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
        cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
        cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
        ltp_engine_init(&send_eng, "dtn://sender", &cfg);

        seg_collector_t col;
        memset(&col, 0, sizeof(col));
        uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
        int seg_count = ltp_segment_block(&send_eng, remote_eid, block,
                                          block_len, collect_segment, &col);
        if (seg_count < 0 || col.count == 0) {
            printf("\n    FAIL at iter %d: segment_block failed\n", iter);
            return 0;
        }

        /* Pick a random subset (limit to avoid recv_map overflow) */
        int subset_count = 1 + (rand() % col.count);
        if (subset_count > 12) subset_count = 12;

        int order[256];
        for (int i = 0; i < col.count; i++) order[i] = i;
        shuffle_ints(order, col.count);

        /* Create receiver engine */
        ltp_engine_t recv_eng;
        ltp_engine_init(&recv_eng, "dtn://receiver", &cfg);

        /* Feed subset as type 0 (no checkpoint) */
        uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
        for (int i = 0; i < subset_count; i++) {
            int idx = order[i];
            ltp_data_segment_t seg_copy = col.segs[idx];
            seg_copy.hdr.type = LTP_SEG_RED_DATA;
            seg_copy.cp_serial = 0;
            seg_copy.rpt_serial = 0;

            int enc_len = ltp_encode_data_segment(&seg_copy, enc_buf,
                                                  sizeof(enc_buf));
            if (enc_len < 0) continue;
            ltp_process_segment(&recv_eng, -1, enc_buf, (size_t)enc_len);
        }

        /* Find import session and record expected claims */
        ltp_import_session_t *sess = NULL;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            if (recv_eng.import_sessions[i].active) {
                sess = &recv_eng.import_sessions[i];
                break;
            }
        }
        if (!sess) {
            printf("\n    FAIL at iter %d: no import session\n", iter);
            return 0;
        }

        ltp_recv_map_t expected_map = sess->recv_map;

        /* Create pipe to capture report */
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            printf("\n    FAIL at iter %d: pipe() failed\n", iter);
            return 0;
        }

        /* Send a checkpoint to trigger report */
        ltp_data_segment_t cp_seg;
        memset(&cp_seg, 0, sizeof(cp_seg));
        cp_seg.hdr.version = 0;
        cp_seg.hdr.type = LTP_SEG_RED_DATA_CP;
        cp_seg.hdr.sender_engine_id = ltp_eid_to_engine_id("dtn://sender");
        cp_seg.hdr.session_number = sess->session_number;
        cp_seg.client_svc_id = 1;
        cp_seg.offset = col.segs[0].offset;
        cp_seg.length = 0;
        cp_seg.cp_serial = 1;
        cp_seg.data = NULL;

        int enc_len = ltp_encode_data_segment(&cp_seg, enc_buf,
                                              sizeof(enc_buf));
        if (enc_len < 0) {
            close(pipefd[0]); close(pipefd[1]);
            printf("\n    FAIL at iter %d: encode CP failed\n", iter);
            return 0;
        }

        ltp_process_segment(&recv_eng, pipefd[1], enc_buf, (size_t)enc_len);

        /* Read report from pipe */
        uint8_t rpt_payload[LTP_MAX_SEGMENT_BUF];
        int rpt_len = read_kiss_frame(pipefd[0], rpt_payload,
                                      sizeof(rpt_payload));
        close(pipefd[0]); close(pipefd[1]);

        if (rpt_len < 0) {
            printf("\n    FAIL at iter %d: no report from pipe\n", iter);
            return 0;
        }

        /* Decode report */
        ltp_segment_hdr_t rpt_hdr;
        uint8_t rpt_body[LTP_MAX_SEGMENT_BUF];
        size_t rpt_body_len = 0;
        if (ltp_decode_segment(rpt_payload, (size_t)rpt_len, &rpt_hdr,
                               rpt_body, sizeof(rpt_body), &rpt_body_len) < 0) {
            printf("\n    FAIL at iter %d: decode report failed\n", iter);
            return 0;
        }
        if (rpt_hdr.type != LTP_SEG_REPORT) {
            printf("\n    FAIL at iter %d: type=%d expected 8\n",
                   iter, rpt_hdr.type);
            return 0;
        }

        ltp_report_segment_t rpt;
        memset(&rpt, 0, sizeof(rpt));
        if (ltp_decode_report_content(rpt_body, rpt_body_len, &rpt) < 0) {
            printf("\n    FAIL at iter %d: decode report content failed\n", iter);
            return 0;
        }

        /* Verify claims match expected recv_map */
        if (rpt.claim_count != expected_map.claim_count) {
            printf("\n    FAIL at iter %d: claim_count=%u expected=%u\n",
                   iter, rpt.claim_count, expected_map.claim_count);
            return 0;
        }
        for (uint32_t ci = 0; ci < rpt.claim_count; ci++) {
            if (rpt.claims[ci].offset != expected_map.claims[ci].offset ||
                rpt.claims[ci].length != expected_map.claims[ci].length) {
                printf("\n    FAIL at iter %d: claim %u mismatch\n", iter, ci);
                return 0;
            }
        }

        /* Clean up */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++)
            send_eng.export_sessions[i].active = 0;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++)
            recv_eng.import_sessions[i].active = 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 8: Retransmission targets exactly missing ranges           */
/* Feature: ltp-over-kiss, Property 8: Retransmission targets missing  */
/* Validates: Requirements 4.4                                         */
/* ================================================================== */

static int test_retransmission_targets_missing(void)
{
    for (int iter = 0; iter < HEAVY_ITERATIONS; iter++) {
        uint32_t block_len = 32 + rand_uint32_max(224); /* 32-256 */
        uint32_t mtu = 16 + rand_uint32_max(48);        /* 16-64 */

        uint8_t block[LTP_MAX_BLOCK_SIZE];
        rand_bytes(block, block_len);

        /* Segment the block */
        ltp_engine_t send_eng;
        ltp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.segment_mtu = mtu;
        cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
        cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
        cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
        ltp_engine_init(&send_eng, "dtn://sender", &cfg);

        seg_collector_t col;
        memset(&col, 0, sizeof(col));
        uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
        int seg_count = ltp_segment_block(&send_eng, remote_eid, block,
                                          block_len, collect_segment, &col);
        if (seg_count < 2) continue; /* Need at least 2 segments for gaps */

        /* Simulate partial reception: pick a subset that creates gaps.
         * Receive some segments, skip others. Limit subset to avoid
         * recv_map overflow. */
        int recv_count = 1 + (rand() % (col.count - 1)); /* at least 1, not all */
        if (recv_count >= col.count) recv_count = col.count - 1;
        if (recv_count > 10) recv_count = 10;

        int order[256];
        for (int i = 0; i < col.count; i++) order[i] = i;
        shuffle_ints(order, col.count);

        /* Track which segments were received */
        int received[256];
        memset(received, 0, sizeof(received));
        for (int i = 0; i < recv_count; i++)
            received[order[i]] = 1;

        /* Build a report with claims matching received segments */
        /* First, compute received byte ranges */
        ltp_recv_map_t recv_map;
        memset(&recv_map, 0, sizeof(recv_map));
        for (int i = 0; i < col.count; i++) {
            if (received[i]) {
                ltp_recv_map_add_claim(&recv_map, col.segs[i].offset,
                                       col.segs[i].length);
            }
        }

        /* Build report */
        ltp_report_segment_t rpt;
        memset(&rpt, 0, sizeof(rpt));
        rpt.rpt_serial = 1;
        rpt.cp_serial = 1;
        rpt.lower_bound = 0;
        rpt.upper_bound = block_len;
        rpt.claim_count = recv_map.claim_count;
        for (uint32_t ci = 0; ci < recv_map.claim_count; ci++)
            rpt.claims[ci] = recv_map.claims[ci];

        /* Compute expected missing ranges */
        typedef struct { uint64_t start; uint64_t end; } range_t;
        range_t expected_missing[LTP_MAX_CLAIMS + 1];
        int expected_miss_count = 0;
        uint64_t cursor = 0;
        for (uint32_t ci = 0; ci < rpt.claim_count; ci++) {
            if (rpt.claims[ci].offset > cursor) {
                expected_missing[expected_miss_count].start = cursor;
                expected_missing[expected_miss_count].end = rpt.claims[ci].offset;
                expected_miss_count++;
            }
            uint64_t claim_end = rpt.claims[ci].offset + rpt.claims[ci].length;
            if (claim_end > cursor) cursor = claim_end;
        }
        if (cursor < block_len) {
            expected_missing[expected_miss_count].start = cursor;
            expected_missing[expected_miss_count].end = block_len;
            expected_miss_count++;
        }

        if (expected_miss_count == 0) continue; /* No gaps, skip */

        /* Find the export session */
        ltp_export_session_t *sess = NULL;
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
            if (send_eng.export_sessions[i].active) {
                sess = &send_eng.export_sessions[i];
                break;
            }
        }
        if (!sess) continue;

        /* Create pipe to capture retransmitted segments */
        int pipefd[2];
        if (pipe(pipefd) < 0) continue;

        /* Encode and process the report to trigger retransmission */
        rpt.hdr.version = 0;
        rpt.hdr.type = LTP_SEG_REPORT;
        rpt.hdr.sender_engine_id = remote_eid;
        rpt.hdr.session_number = sess->session_number;

        uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
        int rpt_enc_len = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
        if (rpt_enc_len < 0) {
            close(pipefd[0]); close(pipefd[1]);
            continue;
        }

        ltp_process_segment(&send_eng, pipefd[1], rpt_buf, (size_t)rpt_enc_len);

        /* Read retransmitted segments from pipe */
        uint8_t frames[64][LTP_MAX_SEGMENT_BUF];
        int frame_lens[64];
        int frame_count = read_all_kiss_frames(pipefd[0], frames,
                                               frame_lens, 64);
        close(pipefd[0]); close(pipefd[1]);

        /* First frame is the report ack, remaining are retransmitted data */
        if (frame_count < 2) {
            printf("\n    FAIL at iter %d: expected ack + data, got %d frames\n",
                   iter, frame_count);
            return 0;
        }

        /* Verify retransmitted data covers exactly the missing ranges */
        ltp_recv_map_t retx_map;
        memset(&retx_map, 0, sizeof(retx_map));

        for (int f = 1; f < frame_count; f++) { /* skip ack at index 0 */
            ltp_segment_hdr_t fhdr;
            uint8_t fbody[LTP_MAX_SEGMENT_BUF];
            size_t fbody_len = 0;
            if (ltp_decode_segment(frames[f], (size_t)frame_lens[f],
                                   &fhdr, fbody, sizeof(fbody),
                                   &fbody_len) < 0)
                continue;

            if (fhdr.type == LTP_SEG_RED_DATA ||
                fhdr.type == LTP_SEG_RED_DATA_CP ||
                fhdr.type == LTP_SEG_RED_DATA_EORP_CP) {
                ltp_data_segment_t dseg;
                memset(&dseg, 0, sizeof(dseg));
                if (ltp_decode_data_content(fbody, fbody_len, fhdr.type,
                                            &dseg) == 0) {
                    ltp_recv_map_add_claim(&retx_map, dseg.offset,
                                           dseg.length);
                    /* Verify each segment respects MTU */
                    if (dseg.length > mtu) {
                        printf("\n    FAIL at iter %d: retx seg len=%lu > mtu=%u\n",
                               iter, (unsigned long)dseg.length, mtu);
                        return 0;
                    }
                }
            }
        }

        /* Verify retransmitted ranges match expected missing ranges */
        /* Build expected missing as a recv_map for comparison */
        ltp_recv_map_t expected_map;
        memset(&expected_map, 0, sizeof(expected_map));
        for (int m = 0; m < expected_miss_count; m++) {
            ltp_recv_map_add_claim(&expected_map,
                                   expected_missing[m].start,
                                   expected_missing[m].end - expected_missing[m].start);
        }

        if (retx_map.claim_count != expected_map.claim_count) {
            printf("\n    FAIL at iter %d: retx claims=%u expected=%u\n",
                   iter, retx_map.claim_count, expected_map.claim_count);
            return 0;
        }
        for (uint32_t ci = 0; ci < retx_map.claim_count; ci++) {
            if (retx_map.claims[ci].offset != expected_map.claims[ci].offset ||
                retx_map.claims[ci].length != expected_map.claims[ci].length) {
                printf("\n    FAIL at iter %d: retx claim %u mismatch: "
                       "got (%lu,%lu) expected (%lu,%lu)\n",
                       iter, ci,
                       (unsigned long)retx_map.claims[ci].offset,
                       (unsigned long)retx_map.claims[ci].length,
                       (unsigned long)expected_map.claims[ci].offset,
                       (unsigned long)expected_map.claims[ci].length);
                return 0;
            }
        }

        /* Clean up */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++)
            send_eng.export_sessions[i].active = 0;
    }
    return 1;
}

/* ================================================================== */
/* Property 9: Session serial numbers are monotonically increasing     */
/* Feature: ltp-over-kiss, Property 9: Serial numbers monotonic        */
/* Validates: Requirements 4.6, 4.7                                    */
/* ================================================================== */

static int test_serial_numbers_monotonic(void)
{
    for (int iter = 0; iter < HEAVY_ITERATIONS; iter++) {
        uint32_t block_len = 32 + rand_uint32_max(224);
        uint32_t mtu = 16 + rand_uint32_max(48);

        uint8_t block[LTP_MAX_BLOCK_SIZE];
        rand_bytes(block, block_len);

        /* --- Test CP serial monotonicity in export session --- */
        ltp_engine_t send_eng;
        ltp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.segment_mtu = mtu;
        cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
        cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
        cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
        ltp_engine_init(&send_eng, "dtn://sender", &cfg);

        seg_collector_t col;
        memset(&col, 0, sizeof(col));
        uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
        int seg_count = ltp_segment_block(&send_eng, remote_eid, block,
                                          block_len, collect_segment, &col);
        if (seg_count < 1) continue;

        /* Find the export session */
        ltp_export_session_t *esess = NULL;
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
            if (send_eng.export_sessions[i].active) {
                esess = &send_eng.export_sessions[i];
                break;
            }
        }
        if (!esess) continue;

        /* Initial CP serial from segmentation (the EoRP+CP segment) */
        uint64_t last_cp = 0;
        for (int i = 0; i < col.count; i++) {
            if (col.segs[i].cp_serial > last_cp)
                last_cp = col.segs[i].cp_serial;
        }

        /* Simulate multiple report/retransmit cycles to generate new CPs */
        int num_cycles = 2 + (rand() % 3); /* 2-4 cycles */
        for (int c = 0; c < num_cycles; c++) {
            /* Build a partial report (claim only first half) */
            uint32_t half = block_len / 2;
            if (half == 0) half = 1;

            ltp_report_segment_t rpt;
            memset(&rpt, 0, sizeof(rpt));
            rpt.hdr.version = 0;
            rpt.hdr.type = LTP_SEG_REPORT;
            rpt.hdr.sender_engine_id = remote_eid;
            rpt.hdr.session_number = esess->session_number;
            rpt.rpt_serial = (uint64_t)(c + 1);
            rpt.cp_serial = last_cp;
            rpt.lower_bound = 0;
            rpt.upper_bound = block_len;
            rpt.claim_count = 1;
            rpt.claims[0].offset = 0;
            rpt.claims[0].length = half;

            int pipefd[2];
            if (pipe(pipefd) < 0) continue;

            uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
            int rpt_enc = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
            if (rpt_enc < 0) { close(pipefd[0]); close(pipefd[1]); continue; }

            ltp_process_segment(&send_eng, pipefd[1], rpt_buf,
                                (size_t)rpt_enc);

            /* Read retransmitted segments, find new CP serial */
            uint8_t frames[64][LTP_MAX_SEGMENT_BUF];
            int frame_lens[64];
            int fc = read_all_kiss_frames(pipefd[0], frames, frame_lens, 64);
            close(pipefd[0]); close(pipefd[1]);

            uint64_t new_cp = 0;
            for (int f = 0; f < fc; f++) {
                ltp_segment_hdr_t fhdr;
                uint8_t fbody[LTP_MAX_SEGMENT_BUF];
                size_t fbody_len = 0;
                if (ltp_decode_segment(frames[f], (size_t)frame_lens[f],
                                       &fhdr, fbody, sizeof(fbody),
                                       &fbody_len) < 0)
                    continue;
                if (fhdr.type == LTP_SEG_RED_DATA_CP ||
                    fhdr.type == LTP_SEG_RED_DATA_EORP_CP) {
                    ltp_data_segment_t dseg;
                    memset(&dseg, 0, sizeof(dseg));
                    if (ltp_decode_data_content(fbody, fbody_len, fhdr.type,
                                                &dseg) == 0) {
                        new_cp = dseg.cp_serial;
                    }
                }
            }

            if (new_cp > 0) {
                if (new_cp <= last_cp) {
                    printf("\n    FAIL at iter %d cycle %d: CP serial %lu <= %lu\n",
                           iter, c, (unsigned long)new_cp,
                           (unsigned long)last_cp);
                    return 0;
                }
                last_cp = new_cp;
            }
        }

        /* --- Test RPT serial monotonicity in import session --- */
        ltp_engine_t recv_eng;
        ltp_engine_init(&recv_eng, "dtn://receiver", &cfg);

        /* Feed some data segments to create an import session */
        uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
        for (int i = 0; i < col.count && i < 5; i++) {
            ltp_data_segment_t seg_copy = col.segs[i];
            seg_copy.hdr.type = LTP_SEG_RED_DATA;
            seg_copy.cp_serial = 0;
            seg_copy.rpt_serial = 0;
            int enc_len = ltp_encode_data_segment(&seg_copy, enc_buf,
                                                  sizeof(enc_buf));
            if (enc_len > 0)
                ltp_process_segment(&recv_eng, -1, enc_buf, (size_t)enc_len);
        }

        /* Find import session */
        ltp_import_session_t *isess = NULL;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
            if (recv_eng.import_sessions[i].active) {
                isess = &recv_eng.import_sessions[i];
                break;
            }
        }
        if (!isess) continue;

        /* Send multiple checkpoints and verify RPT serials increase */
        uint64_t last_rpt = 0;
        int num_cps = 2 + (rand() % 3);
        for (int c = 0; c < num_cps; c++) {
            ltp_data_segment_t cp_seg;
            memset(&cp_seg, 0, sizeof(cp_seg));
            cp_seg.hdr.version = 0;
            cp_seg.hdr.type = LTP_SEG_RED_DATA_CP;
            cp_seg.hdr.sender_engine_id = ltp_eid_to_engine_id("dtn://sender");
            cp_seg.hdr.session_number = isess->session_number;
            cp_seg.client_svc_id = 1;
            cp_seg.offset = 0;
            cp_seg.length = 0;
            cp_seg.cp_serial = (uint64_t)(c + 1);
            cp_seg.data = NULL;

            int pipefd[2];
            if (pipe(pipefd) < 0) continue;

            int enc_len = ltp_encode_data_segment(&cp_seg, enc_buf,
                                                  sizeof(enc_buf));
            if (enc_len < 0) { close(pipefd[0]); close(pipefd[1]); continue; }

            ltp_process_segment(&recv_eng, pipefd[1], enc_buf,
                                (size_t)enc_len);

            /* Read report */
            uint8_t rpt_payload[LTP_MAX_SEGMENT_BUF];
            int rpt_len = read_kiss_frame(pipefd[0], rpt_payload,
                                          sizeof(rpt_payload));
            close(pipefd[0]); close(pipefd[1]);

            if (rpt_len > 0) {
                ltp_segment_hdr_t rpt_hdr;
                uint8_t rpt_body[LTP_MAX_SEGMENT_BUF];
                size_t rpt_body_len = 0;
                if (ltp_decode_segment(rpt_payload, (size_t)rpt_len,
                                       &rpt_hdr, rpt_body, sizeof(rpt_body),
                                       &rpt_body_len) == 0 &&
                    rpt_hdr.type == LTP_SEG_REPORT) {
                    ltp_report_segment_t rpt;
                    memset(&rpt, 0, sizeof(rpt));
                    if (ltp_decode_report_content(rpt_body, rpt_body_len,
                                                  &rpt) == 0) {
                        if (last_rpt > 0 && rpt.rpt_serial <= last_rpt) {
                            printf("\n    FAIL at iter %d cp %d: "
                                   "RPT serial %lu <= %lu\n",
                                   iter, c, (unsigned long)rpt.rpt_serial,
                                   (unsigned long)last_rpt);
                            return 0;
                        }
                        last_rpt = rpt.rpt_serial;
                    }
                }
            }
        }

        /* Clean up */
        for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++)
            send_eng.export_sessions[i].active = 0;
        for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++)
            recv_eng.import_sessions[i].active = 0;
    }
    return 1;
}

/* ================================================================== */
/* Unit tests: Checkpoint/report exchange (Task 7.7)                   */
/* Feature: ltp-over-kiss, Task 7.7                                    */
/* Validates: Requirements 3.4, 3.5, 4.3, 4.5                         */
/* ================================================================== */

/* Test: report ack generated on report receipt */
static int test_report_ack_on_report(void)
{
    /* Set up sender engine with an export session */
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://sender", &cfg);

    /* Create an export session by sending a block */
    uint8_t block[] = "Hello, LTP!";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    int send_pipe[2];
    if (pipe(send_pipe) < 0) return 0;
    ltp_send_block(&eng, send_pipe[1], "dtn://receiver", block, block_len);
    close(send_pipe[0]); close(send_pipe[1]);

    /* Find the export session */
    ltp_export_session_t *sess = NULL;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (eng.export_sessions[i].active) {
            sess = &eng.export_sessions[i];
            break;
        }
    }
    if (!sess) { printf("\n    FAIL: no export session\n"); return 0; }

    /* Build a report segment (full coverage) */
    uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
    ltp_report_segment_t rpt;
    memset(&rpt, 0, sizeof(rpt));
    rpt.hdr.version = 0;
    rpt.hdr.type = LTP_SEG_REPORT;
    rpt.hdr.sender_engine_id = remote_eid;
    rpt.hdr.session_number = sess->session_number;
    rpt.rpt_serial = 1;
    rpt.cp_serial = 1;
    rpt.lower_bound = 0;
    rpt.upper_bound = block_len;
    rpt.claim_count = 1;
    rpt.claims[0].offset = 0;
    rpt.claims[0].length = block_len;

    /* Create pipe to capture report ack */
    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
    int rpt_enc = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
    if (rpt_enc < 0) { close(pipefd[0]); close(pipefd[1]); return 0; }

    int rc = ltp_process_segment(&eng, pipefd[1], rpt_buf, (size_t)rpt_enc);
    if (rc != 0) {
        close(pipefd[0]); close(pipefd[1]);
        printf("\n    FAIL: process_segment returned %d\n", rc);
        return 0;
    }

    /* Read the report ack from pipe */
    uint8_t ack_payload[LTP_MAX_SEGMENT_BUF];
    int ack_len = read_kiss_frame(pipefd[0], ack_payload, sizeof(ack_payload));
    close(pipefd[0]); close(pipefd[1]);

    if (ack_len < 0) {
        printf("\n    FAIL: no report ack from pipe\n");
        return 0;
    }

    /* Decode and verify it's a report ack */
    ltp_segment_hdr_t ack_hdr;
    uint8_t ack_body[LTP_MAX_SEGMENT_BUF];
    size_t ack_body_len = 0;
    if (ltp_decode_segment(ack_payload, (size_t)ack_len, &ack_hdr,
                           ack_body, sizeof(ack_body), &ack_body_len) < 0) {
        printf("\n    FAIL: decode ack segment failed\n");
        return 0;
    }
    if (ack_hdr.type != LTP_SEG_REPORT_ACK) {
        printf("\n    FAIL: expected type 9, got %d\n", ack_hdr.type);
        return 0;
    }

    ltp_report_ack_segment_t ack;
    memset(&ack, 0, sizeof(ack));
    if (ltp_decode_report_ack_content(ack_body, ack_body_len, &ack) < 0) {
        printf("\n    FAIL: decode ack content failed\n");
        return 0;
    }
    if (ack.rpt_serial != 1) {
        printf("\n    FAIL: ack rpt_serial=%lu expected 1\n",
               (unsigned long)ack.rpt_serial);
        return 0;
    }

    return 1;
}

/* Test: export session closed after full report */
static int test_export_session_closed_after_full_report(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://sender", &cfg);

    uint8_t block[] = "Test block data";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    int send_pipe[2];
    if (pipe(send_pipe) < 0) return 0;
    ltp_send_block(&eng, send_pipe[1], "dtn://receiver", block, block_len);
    close(send_pipe[0]); close(send_pipe[1]);

    ltp_export_session_t *sess = NULL;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (eng.export_sessions[i].active) {
            sess = &eng.export_sessions[i];
            break;
        }
    }
    if (!sess) { printf("\n    FAIL: no export session\n"); return 0; }

    /* Verify session is active before report */
    if (sess->completed) {
        printf("\n    FAIL: session already completed\n");
        return 0;
    }

    /* Send full report */
    uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
    ltp_report_segment_t rpt;
    memset(&rpt, 0, sizeof(rpt));
    rpt.hdr.version = 0;
    rpt.hdr.type = LTP_SEG_REPORT;
    rpt.hdr.sender_engine_id = remote_eid;
    rpt.hdr.session_number = sess->session_number;
    rpt.rpt_serial = 1;
    rpt.cp_serial = 1;
    rpt.lower_bound = 0;
    rpt.upper_bound = block_len;
    rpt.claim_count = 1;
    rpt.claims[0].offset = 0;
    rpt.claims[0].length = block_len;

    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    uint8_t rpt_buf[LTP_MAX_SEGMENT_BUF];
    int rpt_enc = ltp_encode_report(&rpt, rpt_buf, sizeof(rpt_buf));
    ltp_process_segment(&eng, pipefd[1], rpt_buf, (size_t)rpt_enc);
    close(pipefd[0]); close(pipefd[1]);

    /* Verify session is now completed */
    if (!sess->completed) {
        printf("\n    FAIL: session not completed after full report\n");
        return 0;
    }

    return 1;
}

/* Test: import session delivers block and closes */
static int test_import_session_delivers_and_closes(void)
{
    ltp_engine_t send_eng, recv_eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&send_eng, "dtn://sender", &cfg);
    ltp_engine_init(&recv_eng, "dtn://receiver", &cfg);

    recv_eng.on_block_received = NULL;

    uint8_t block[] = "Import session test data!";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    /* Segment the block */
    seg_collector_t col;
    memset(&col, 0, sizeof(col));
    uint64_t remote_eid = ltp_eid_to_engine_id("dtn://receiver");
    ltp_segment_block(&send_eng, remote_eid, block, block_len,
                      collect_segment, &col);

    /* Feed all segments to receiver. The last one (EoRP+CP) triggers
     * report generation and block delivery. */
    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
    for (int i = 0; i < col.count; i++) {
        int enc_len = ltp_encode_data_segment(&col.segs[i], enc_buf,
                                              sizeof(enc_buf));
        if (enc_len > 0)
            ltp_process_segment(&recv_eng, pipefd[1], enc_buf,
                                (size_t)enc_len);
    }
    close(pipefd[0]); close(pipefd[1]);

    /* Verify block was delivered (session closed) */
    int found_delivered = 0;
    for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
        if (recv_eng.import_sessions[i].delivered) {
            found_delivered = 1;
            /* Verify data matches */
            if (recv_eng.import_sessions[i].block_len != block_len) {
                printf("\n    FAIL: delivered len=%u expected=%u\n",
                       recv_eng.import_sessions[i].block_len, block_len);
                return 0;
            }
            if (memcmp(recv_eng.import_sessions[i].block_data, block,
                       block_len) != 0) {
                printf("\n    FAIL: delivered data mismatch\n");
                return 0;
            }
            /* Verify session is closed */
            if (recv_eng.import_sessions[i].active) {
                printf("\n    FAIL: session still active after delivery\n");
                return 0;
            }
            break;
        }
    }

    if (!found_delivered) {
        printf("\n    FAIL: no delivered block found\n");
        return 0;
    }

    /* Verify blocks_delivered counter */
    if (recv_eng.blocks_delivered != 1) {
        printf("\n    FAIL: blocks_delivered=%u expected 1\n",
               recv_eng.blocks_delivered);
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* Timer management unit tests                                         */
/* Requirements: 5.1, 5.5, 5.6                                        */
/* ================================================================== */

/* Test: timer duration = 2*OWLT + 200ms (with OWLT=1500, timer ~3200ms) */
static int test_timer_duration_2owlt_plus_200(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = 1500;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://timer-test", &cfg);

    /* Send a small block to create an export session and start a timer */
    uint8_t block[] = "timer test";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    ltp_send_block(&eng, pipefd[1], "dtn://remote", block, block_len);

    /* Find the active timer */
    int found = 0;
    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (eng.timers[i].active && eng.timers[i].type == 0) {
            /* Verify expiry is approximately now + 3200ms */
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);

            int64_t diff_sec = (int64_t)eng.timers[i].expiry.tv_sec - (int64_t)now.tv_sec;
            int64_t diff_ns  = (int64_t)eng.timers[i].expiry.tv_nsec - (int64_t)now.tv_nsec;
            int64_t diff_ms  = diff_sec * 1000 + diff_ns / 1000000;

            /* Expected: 3200ms. Allow tolerance of ±100ms for execution time */
            if (diff_ms < 3100 || diff_ms > 3300) {
                printf("\n    FAIL: timer duration=%ld ms, expected ~3200ms\n",
                       (long)diff_ms);
                close(pipefd[0]); close(pipefd[1]);
                return 0;
            }
            found = 1;
            break;
        }
    }

    close(pipefd[0]); close(pipefd[1]);

    if (!found) {
        printf("\n    FAIL: no active checkpoint timer found\n");
        return 0;
    }

    /* Also verify ltp_get_next_timeout_ms returns ~3200 */
    int timeout = ltp_get_next_timeout_ms(&eng);
    if (timeout < 3100 || timeout > 3300) {
        printf("\n    FAIL: get_next_timeout_ms=%d, expected ~3200\n", timeout);
        return 0;
    }

    return 1;
}

/* Test: session cancelled after max retries */
static int test_session_cancelled_after_max_retries(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = 1500;
    cfg.max_retries = 1;  /* Only 1 retry allowed */
    ltp_engine_init(&eng, "dtn://retry-test", &cfg);

    /* Send a small block to create an export session and start a timer */
    uint8_t block[] = "retry test";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    ltp_send_block(&eng, pipefd[1], "dtn://remote", block, block_len);

    /* Find the export session */
    uint64_t session_num = 0;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (eng.export_sessions[i].active) {
            session_num = eng.export_sessions[i].session_number;
            break;
        }
    }
    if (session_num == 0) {
        printf("\n    FAIL: no active export session found\n");
        close(pipefd[0]); close(pipefd[1]);
        return 0;
    }

    /* Set the timer expiry to the past so it fires immediately */
    for (int i = 0; i < LTP_MAX_TIMERS; i++) {
        if (eng.timers[i].active && eng.timers[i].type == 0) {
            eng.timers[i].expiry.tv_sec = 0;
            eng.timers[i].expiry.tv_nsec = 0;
            break;
        }
    }

    /* First fire: should retransmit (retries goes from 0 to 1) */
    int fired = ltp_fire_expired_timers(&eng, pipefd[1]);
    if (fired < 1) {
        printf("\n    FAIL: first fire returned %d, expected >= 1\n", fired);
        close(pipefd[0]); close(pipefd[1]);
        return 0;
    }

    /* Session should still be active after first fire (retries=1, max=1,
     * but the check is retries >= max_retries, so 1 >= 1 means cancel).
     * Actually with max_retries=1: first fire increments retries to 1,
     * 1 >= 1 is true, so session should be cancelled on first fire. */
    int session_active = 0;
    int session_cancelled = 0;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (eng.export_sessions[i].session_number == session_num) {
            session_active = eng.export_sessions[i].active;
            session_cancelled = eng.export_sessions[i].cancelled;
            break;
        }
    }

    close(pipefd[0]); close(pipefd[1]);

    if (session_active) {
        printf("\n    FAIL: session still active after max retries exceeded\n");
        return 0;
    }
    if (!session_cancelled) {
        printf("\n    FAIL: session not marked as cancelled\n");
        return 0;
    }
    if (eng.sessions_cancelled < 1) {
        printf("\n    FAIL: sessions_cancelled=%u, expected >= 1\n",
               eng.sessions_cancelled);
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* Unit tests: Session cancellation (Task 9.2)                         */
/* Feature: ltp-over-kiss, Task 9.2                                    */
/* Validates: Requirements 7.2, 7.4, 8.5                               */
/* ================================================================== */

/* Test: cancel-by-sender triggers cancel ack (type 13) */
static int test_cancel_by_sender_triggers_ack(void)
{
    /* Set up a receiver engine with an import session */
    ltp_engine_t recv_eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&recv_eng, "dtn://receiver", &cfg);

    /* Create an import session by feeding a data segment */
    uint64_t sender_eid = ltp_eid_to_engine_id("dtn://sender");
    uint64_t session_num = 42;
    uint8_t payload[] = { 0xDE, 0xAD };

    ltp_data_segment_t dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.hdr.version = 0;
    dseg.hdr.type = LTP_SEG_RED_DATA;
    dseg.hdr.sender_engine_id = sender_eid;
    dseg.hdr.session_number = session_num;
    dseg.client_svc_id = 1;
    dseg.offset = 0;
    dseg.length = 2;
    dseg.data = payload;

    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_data_segment(&dseg, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) { printf("\n    FAIL: encode data seg\n"); return 0; }
    ltp_process_segment(&recv_eng, -1, enc_buf, (size_t)enc_len);

    /* Verify import session exists */
    int has_import = 0;
    for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
        if (recv_eng.import_sessions[i].active &&
            recv_eng.import_sessions[i].session_number == session_num) {
            has_import = 1;
            break;
        }
    }
    if (!has_import) { printf("\n    FAIL: no import session\n"); return 0; }

    /* Send cancel-by-sender (type 12) to the receiver */
    ltp_cancel_segment_t cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.hdr.version = 0;
    cancel.hdr.type = LTP_SEG_CANCEL_BY_SENDER;
    cancel.hdr.sender_engine_id = sender_eid;
    cancel.hdr.session_number = session_num;
    cancel.reason = 0;

    enc_len = ltp_encode_cancel(&cancel, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) { printf("\n    FAIL: encode cancel\n"); return 0; }

    int pipefd[2];
    if (pipe(pipefd) < 0) { printf("\n    FAIL: pipe()\n"); return 0; }

    int rc = ltp_process_segment(&recv_eng, pipefd[1], enc_buf, (size_t)enc_len);
    if (rc != 0) {
        close(pipefd[0]); close(pipefd[1]);
        printf("\n    FAIL: process cancel returned %d\n", rc);
        return 0;
    }

    /* Read the cancel ack from pipe */
    uint8_t ack_payload[LTP_MAX_SEGMENT_BUF];
    int ack_len = read_kiss_frame(pipefd[0], ack_payload, sizeof(ack_payload));
    close(pipefd[0]); close(pipefd[1]);

    if (ack_len < 0) {
        printf("\n    FAIL: no cancel ack from pipe\n");
        return 0;
    }

    /* Decode and verify it's a cancel-ack-to-sender (type 13) */
    ltp_segment_hdr_t ack_hdr;
    uint8_t ack_body[LTP_MAX_SEGMENT_BUF];
    size_t ack_body_len = 0;
    if (ltp_decode_segment(ack_payload, (size_t)ack_len, &ack_hdr,
                           ack_body, sizeof(ack_body), &ack_body_len) < 0) {
        printf("\n    FAIL: decode cancel ack failed\n");
        return 0;
    }
    if (ack_hdr.type != LTP_SEG_CANCEL_ACK_SENDER) {
        printf("\n    FAIL: expected type 13, got %d\n", ack_hdr.type);
        return 0;
    }
    if (ack_hdr.session_number != session_num) {
        printf("\n    FAIL: ack session_number mismatch\n");
        return 0;
    }

    /* Verify import session was closed */
    int still_active = 0;
    for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
        if (recv_eng.import_sessions[i].active &&
            recv_eng.import_sessions[i].session_number == session_num) {
            still_active = 1;
            break;
        }
    }
    if (still_active) {
        printf("\n    FAIL: import session still active after cancel\n");
        return 0;
    }

    return 1;
}

/* Test: cancel-by-receiver triggers cancel ack (type 15) */
static int test_cancel_by_receiver_triggers_ack(void)
{
    /* Set up a sender engine with an export session */
    ltp_engine_t send_eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&send_eng, "dtn://sender", &cfg);

    /* Create an export session by sending a block */
    uint8_t block[] = "cancel test";
    uint32_t block_len = (uint32_t)strlen((char *)block);

    int send_pipe[2];
    if (pipe(send_pipe) < 0) { printf("\n    FAIL: pipe()\n"); return 0; }
    ltp_send_block(&send_eng, send_pipe[1], "dtn://receiver", block, block_len);
    close(send_pipe[0]); close(send_pipe[1]);

    /* Find the export session */
    ltp_export_session_t *sess = NULL;
    for (int i = 0; i < LTP_MAX_EXPORT_SESSIONS; i++) {
        if (send_eng.export_sessions[i].active) {
            sess = &send_eng.export_sessions[i];
            break;
        }
    }
    if (!sess) { printf("\n    FAIL: no export session\n"); return 0; }

    uint64_t session_num = sess->session_number;

    /* Send cancel-by-receiver (type 14) to the sender */
    uint64_t receiver_eid = ltp_eid_to_engine_id("dtn://receiver");
    ltp_cancel_segment_t cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.hdr.version = 0;
    cancel.hdr.type = LTP_SEG_CANCEL_BY_RECVR;
    cancel.hdr.sender_engine_id = receiver_eid;
    cancel.hdr.session_number = session_num;
    cancel.reason = 0;

    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_cancel(&cancel, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) { printf("\n    FAIL: encode cancel\n"); return 0; }

    int pipefd[2];
    if (pipe(pipefd) < 0) { printf("\n    FAIL: pipe()\n"); return 0; }

    int rc = ltp_process_segment(&send_eng, pipefd[1], enc_buf, (size_t)enc_len);
    if (rc != 0) {
        close(pipefd[0]); close(pipefd[1]);
        printf("\n    FAIL: process cancel returned %d\n", rc);
        return 0;
    }

    /* Read the cancel ack from pipe */
    uint8_t ack_payload[LTP_MAX_SEGMENT_BUF];
    int ack_len = read_kiss_frame(pipefd[0], ack_payload, sizeof(ack_payload));
    close(pipefd[0]); close(pipefd[1]);

    if (ack_len < 0) {
        printf("\n    FAIL: no cancel ack from pipe\n");
        return 0;
    }

    /* Decode and verify it's a cancel-ack-to-receiver (type 15) */
    ltp_segment_hdr_t ack_hdr;
    uint8_t ack_body[LTP_MAX_SEGMENT_BUF];
    size_t ack_body_len = 0;
    if (ltp_decode_segment(ack_payload, (size_t)ack_len, &ack_hdr,
                           ack_body, sizeof(ack_body), &ack_body_len) < 0) {
        printf("\n    FAIL: decode cancel ack failed\n");
        return 0;
    }
    if (ack_hdr.type != LTP_SEG_CANCEL_ACK_RECVR) {
        printf("\n    FAIL: expected type 15, got %d\n", ack_hdr.type);
        return 0;
    }
    if (ack_hdr.session_number != session_num) {
        printf("\n    FAIL: ack session_number mismatch\n");
        return 0;
    }

    /* Verify export session was closed */
    if (sess->active) {
        printf("\n    FAIL: export session still active after cancel\n");
        return 0;
    }
    if (!sess->cancelled) {
        printf("\n    FAIL: export session not marked cancelled\n");
        return 0;
    }

    return 1;
}

/* Test: cancel for unknown session still sends cancel ack */
static int test_cancel_unknown_session_sends_ack(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://test", &cfg);

    /* Send cancel-by-sender for a session that doesn't exist */
    ltp_cancel_segment_t cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.hdr.version = 0;
    cancel.hdr.type = LTP_SEG_CANCEL_BY_SENDER;
    cancel.hdr.sender_engine_id = 99999;
    cancel.hdr.session_number = 77777;
    cancel.reason = 0;

    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_cancel(&cancel, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) { printf("\n    FAIL: encode cancel\n"); return 0; }

    int pipefd[2];
    if (pipe(pipefd) < 0) { printf("\n    FAIL: pipe()\n"); return 0; }

    int rc = ltp_process_segment(&eng, pipefd[1], enc_buf, (size_t)enc_len);
    if (rc != 0) {
        close(pipefd[0]); close(pipefd[1]);
        printf("\n    FAIL: process cancel returned %d\n", rc);
        return 0;
    }

    /* Should still get a cancel ack (type 13) */
    uint8_t ack_payload[LTP_MAX_SEGMENT_BUF];
    int ack_len = read_kiss_frame(pipefd[0], ack_payload, sizeof(ack_payload));
    close(pipefd[0]); close(pipefd[1]);

    if (ack_len < 0) {
        printf("\n    FAIL: no cancel ack for unknown session\n");
        return 0;
    }

    ltp_segment_hdr_t ack_hdr;
    uint8_t ack_body[LTP_MAX_SEGMENT_BUF];
    size_t ack_body_len = 0;
    if (ltp_decode_segment(ack_payload, (size_t)ack_len, &ack_hdr,
                           ack_body, sizeof(ack_body), &ack_body_len) < 0) {
        printf("\n    FAIL: decode cancel ack failed\n");
        return 0;
    }
    if (ack_hdr.type != LTP_SEG_CANCEL_ACK_SENDER) {
        printf("\n    FAIL: expected type 13, got %d\n", ack_hdr.type);
        return 0;
    }

    return 1;
}

/* Test: segment with non-matching engine ID is still processed
 * (Req 8.5 — we don't filter by engine ID on receive, only on send) */
static int test_segment_with_different_engine_id_processed(void)
{
    ltp_engine_t eng;
    ltp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.segment_mtu = 64;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.owlt_ms = LTP_DEFAULT_OWLT_MS;
    cfg.max_retries = LTP_DEFAULT_MAX_RETRIES;
    ltp_engine_init(&eng, "dtn://local", &cfg);

    /* Send a data segment with an engine ID that doesn't match any endpoint */
    uint64_t foreign_eid = 0xDEADBEEF;
    uint8_t payload[] = { 0x42 };

    ltp_data_segment_t dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.hdr.version = 0;
    dseg.hdr.type = LTP_SEG_RED_DATA;
    dseg.hdr.sender_engine_id = foreign_eid;
    dseg.hdr.session_number = 1;
    dseg.client_svc_id = 1;
    dseg.offset = 0;
    dseg.length = 1;
    dseg.data = payload;

    uint8_t enc_buf[LTP_MAX_SEGMENT_BUF];
    int enc_len = ltp_encode_data_segment(&dseg, enc_buf, sizeof(enc_buf));
    if (enc_len < 0) { printf("\n    FAIL: encode data seg\n"); return 0; }

    int rc = ltp_process_segment(&eng, -1, enc_buf, (size_t)enc_len);
    if (rc != 0) {
        printf("\n    FAIL: process_segment returned %d, expected 0\n", rc);
        return 0;
    }

    /* Verify an import session was created */
    int found = 0;
    for (int i = 0; i < LTP_MAX_IMPORT_SESSIONS; i++) {
        if (eng.import_sessions[i].active &&
            eng.import_sessions[i].engine_id == foreign_eid) {
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("\n    FAIL: no import session for foreign engine ID\n");
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* Property 11: KISS frame size limit for LTP segments                 */
/* Feature: ltp-over-kiss, Property 11: KISS frame size limit          */
/* Validates: Requirements 9.5                                         */
/* ================================================================== */

static int test_kiss_frame_size_limit(void)
{
    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Generate random LTP data segment with payload = 64 bytes (default MTU) */
        uint8_t payload[LTP_DEFAULT_SEGMENT_MTU];
        rand_bytes(payload, sizeof(payload));

        uint64_t engine_id = rand_uint64_max((uint64_t)UINT32_MAX);
        uint64_t session_num = rand_uint64_max(0xFFFFFF);
        uint64_t offset = rand_uint64_max(1023);
        uint64_t cp_serial = rand_uint64_max(0xFFFF);
        uint64_t rpt_serial = rand_uint64_max(0xFFFF);

        /* Use checkpoint type (type 2) for worst-case header size */
        ltp_data_segment_t seg;
        memset(&seg, 0, sizeof(seg));
        seg.hdr.version = 0;
        seg.hdr.type = LTP_SEG_RED_DATA_EORP_CP;
        seg.hdr.sender_engine_id = engine_id;
        seg.hdr.session_number = session_num;
        seg.hdr.hdr_ext_count = 0;
        seg.hdr.trailer_ext_count = 0;
        seg.client_svc_id = 1;
        seg.offset = offset;
        seg.length = LTP_DEFAULT_SEGMENT_MTU;
        seg.cp_serial = cp_serial;
        seg.rpt_serial = rpt_serial;
        seg.data = payload;

        /* Encode LTP segment */
        uint8_t ltp_buf[LTP_MAX_SEGMENT_BUF];
        int enc_len = ltp_encode_data_segment(&seg, ltp_buf, sizeof(ltp_buf));
        if (enc_len < 0) {
            printf("\n    FAIL at iter %d: ltp_encode_data_segment returned %d\n",
                   iter, enc_len);
            return 0;
        }

        /* KISS-encode the LTP segment */
        uint8_t kiss_buf[LTP_MAX_SEGMENT_BUF * 2 + 3];
        int kiss_len = kiss_encode(ltp_buf, (size_t)enc_len,
                                   kiss_buf, sizeof(kiss_buf));
        if (kiss_len < 0) {
            printf("\n    FAIL at iter %d: kiss_encode returned %d\n",
                   iter, kiss_len);
            return 0;
        }

        /* Verify KISS frame does not exceed 512 bytes */
        if (kiss_len > 512) {
            printf("\n    FAIL at iter %d: KISS frame = %d bytes > 512 "
                   "(engine_id=%lu session=%lu offset=%lu)\n",
                   iter, kiss_len,
                   (unsigned long)engine_id,
                   (unsigned long)session_num,
                   (unsigned long)offset);
            return 0;
        }
    }
    return 1;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(void)
{
    srand((unsigned)time(NULL));

    printf("LTP segment encoding/decoding tests\n");
    printf("====================================\n\n");

    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(ltp_data_segment_roundtrip);
    TEST(ltp_control_segment_roundtrip);
    TEST(engine_id_determinism);
    TEST(block_segmentation_completeness);
    TEST(block_segmentation_reassembly_roundtrip);
    TEST(reception_report_claims_accuracy);
    TEST(retransmission_targets_missing);
    TEST(serial_numbers_monotonic);
    TEST(kiss_frame_size_limit);

    printf("\nUnit tests:\n");
    TEST(decode_invalid_type_5);
    TEST(type0_red_data_no_checkpoint);
    TEST(type2_eorp_checkpoint);
    TEST(max_export_sessions);
    TEST(max_import_sessions);
    TEST(block_1024_accepted);
    TEST(block_1025_rejected);
    TEST(report_ack_on_report);
    TEST(export_session_closed_after_full_report);
    TEST(import_session_delivers_and_closes);

    printf("\nTimer management tests:\n");
    TEST(timer_duration_2owlt_plus_200);
    TEST(session_cancelled_after_max_retries);

    printf("\nCancellation tests:\n");
    TEST(cancel_by_sender_triggers_ack);
    TEST(cancel_by_receiver_triggers_ack);
    TEST(cancel_unknown_session_sends_ack);
    TEST(segment_with_different_engine_id_processed);

    printf("\n------------------------------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
