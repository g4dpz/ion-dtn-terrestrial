#ifndef BP_H
#define BP_H

#include <stdint.h>
#include <stddef.h>

#define BP_VERSION 7
#define BP_DTN_EPOCH 946684800ULL
#define BP_MAX_EID_LEN 64
#define BP_MAX_PAYLOAD 65535
#define BP_MAX_BUNDLE_BUF 1024
#define BP_MAX_FRAGMENTS 64
#define BP_DEFAULT_LIFETIME_SEC 3600
#define BP_DEFAULT_FRAGMENT_SIZE 900

#define BP_FLAG_FRAGMENT    0x0001
#define BP_FLAG_NO_FRAGMENT 0x0004

typedef struct { char uri[BP_MAX_EID_LEN]; } bp_eid_t;
typedef struct { uint64_t time; uint64_t seq; } bp_timestamp_t;

typedef struct {
    uint64_t       flags;
    bp_eid_t       dst;
    bp_eid_t       src;
    bp_eid_t       report_to;
    bp_timestamp_t timestamp;
    uint64_t       lifetime_ms;
    uint64_t       fragment_offset;
    uint64_t       total_adu_len;
    uint8_t        crc_type;
} bp_primary_t;

typedef struct {
    bp_primary_t   primary;
    const uint8_t *payload;
    size_t         payload_len;
} bp_bundle_t;

typedef struct {
    int            active;
    bp_eid_t       src;
    bp_timestamp_t timestamp;
    uint64_t       total_adu_len;
    uint8_t        data[BP_MAX_PAYLOAD];
    uint8_t        received[BP_MAX_PAYLOAD];
    size_t         bytes_received;
    uint64_t       lifetime_ms;
} bp_reassembly_t;

uint64_t bp_dtn_time_now(void);
uint64_t bp_dtn_to_unix(uint64_t dtn_ms);
uint16_t bp_crc16(const uint8_t *data, size_t len);

int bp_eid_encode(const bp_eid_t *eid, uint8_t *out, size_t out_size);
int bp_eid_decode(const uint8_t *buf, size_t len, bp_eid_t *eid);

int bp_encode_bundle(const bp_eid_t *src, const bp_eid_t *dst,
                     const uint8_t *payload, size_t payload_len,
                     uint64_t lifetime_ms, uint64_t seq,
                     uint8_t *out, size_t out_size);

int bp_encode_fragment(const bp_eid_t *src, const bp_eid_t *dst,
                       const uint8_t *payload, size_t payload_len,
                       uint64_t lifetime_ms, uint64_t seq,
                       uint64_t fragment_offset, uint64_t total_adu_len,
                       uint8_t *out, size_t out_size);

int bp_decode_bundle(const uint8_t *buf, size_t len, bp_bundle_t *bundle);

int bp_fragment_count(size_t payload_len, size_t fragment_payload_size);

void bp_reassembly_init(bp_reassembly_t *ctx);
int  bp_reassembly_add(bp_reassembly_t *ctx, const bp_bundle_t *fragment);

#endif
