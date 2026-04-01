#ifndef BP_H
#define BP_H

#include <stdint.h>
#include <stddef.h>

/* Bundle Protocol Version 7 (RFC 9171) */
#define BP_VERSION 7

/* Block Types */
#define BP_BLOCK_PRIMARY  0
#define BP_BLOCK_PAYLOAD  1

/* CRC Types */
#define BP_CRC_NONE  0
#define BP_CRC_16    1
#define BP_CRC_32    2

/* Bundle Processing Control Flags */
#define BP_FLAG_FRAGMENT        0x0001
#define BP_FLAG_ADMIN_RECORD    0x0002
#define BP_FLAG_NO_FRAGMENT     0x0004
#define BP_FLAG_APP_ACK         0x0020

/* Simple BPv7 Bundle structure (minimal implementation) */
typedef struct {
    uint8_t  version;
    uint32_t flags;
    uint32_t crc_type;
    char     dest_eid[128];     /* dtn:// URI */
    char     src_eid[128];      /* dtn:// URI */
    char     report_eid[128];
    uint64_t creation_time;     /* DTN time (ms since 2000-01-01) */
    uint64_t creation_seq;
    uint64_t lifetime;          /* milliseconds */
    uint8_t  payload[512];
    size_t   payload_len;
} bp_bundle_t;

/* Encode a BPv7 bundle into CBOR wire format */
int encode_bp_bundle(const bp_bundle_t *bundle, uint8_t *output, size_t output_size);

/* Decode a BPv7 bundle from CBOR wire format */
int decode_bp_bundle(const uint8_t *data, size_t data_len, bp_bundle_t *bundle);

/* Create a simple BPv7 bundle with payload */
void create_simple_bundle(bp_bundle_t *bundle, const char *dest, const char *src,
                          const uint8_t *payload, size_t payload_len);

#endif /* BP_H */
