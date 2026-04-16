/*
 * ltp.h — Licklider Transmission Protocol over KISS
 *
 * Defines all LTP types, constants, and function prototypes for
 * segment encoding/decoding, session management, timers, and the
 * event loop.
 */

#ifndef LTP_H
#define LTP_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ---- Configuration constants ---- */
#define LTP_MAX_EXPORT_SESSIONS  128
#define LTP_MAX_IMPORT_SESSIONS  128
#define LTP_MAX_BLOCK_SIZE       1024
#define LTP_DEFAULT_SEGMENT_MTU  64
#define LTP_DEFAULT_OWLT_MS      1500
#define LTP_DEFAULT_MAX_RETRIES  7
#define LTP_MAX_CLAIMS           16   /* Max reception report claims */
#define LTP_MAX_TIMERS           256  /* Max concurrent timers */
#define LTP_MAX_SEGMENT_BUF      512  /* Max encoded segment size */
#define LTP_MAX_ENDPOINTS        8    /* Max endpoint mappings */

/* ---- LTP Segment Types (RFC 5326 §3.1) ---- */
typedef enum {
    LTP_SEG_RED_DATA          = 0,   /* Red data, no checkpoint */
    LTP_SEG_RED_DATA_CP       = 1,   /* Red data with checkpoint */
    LTP_SEG_RED_DATA_EORP_CP  = 2,   /* Red data, end-of-red-part + checkpoint */
    LTP_SEG_GREEN_DATA        = 3,   /* Green data */
    LTP_SEG_GREEN_DATA_EOB    = 4,   /* Green data, end-of-block */
    /* 5-7 reserved */
    LTP_SEG_REPORT            = 8,   /* Reception report */
    LTP_SEG_REPORT_ACK        = 9,   /* Report acknowledgment */
    /* 10-11 reserved */
    LTP_SEG_CANCEL_BY_SENDER  = 12,  /* Cancel from sender */
    LTP_SEG_CANCEL_ACK_SENDER = 13,  /* Cancel ack to sender */
    LTP_SEG_CANCEL_BY_RECVR   = 14,  /* Cancel from receiver */
    LTP_SEG_CANCEL_ACK_RECVR  = 15   /* Cancel ack to receiver */
} ltp_seg_type_t;

/* ---- LTP Segment Header ---- */
typedef struct {
    uint8_t        version;          /* Protocol version (0) */
    ltp_seg_type_t type;             /* Segment type (4 bits) */
    uint64_t       sender_engine_id; /* Session originator engine ID */
    uint64_t       session_number;   /* Session number */
    uint8_t        hdr_ext_count;    /* Header extension count (0) */
    uint8_t        trailer_ext_count;/* Trailer extension count (0) */
} ltp_segment_hdr_t;

/* ---- Data Segment Content ---- */
typedef struct {
    ltp_segment_hdr_t hdr;
    uint64_t          client_svc_id;   /* Client service ID */
    uint64_t          offset;          /* Data offset within block */
    uint64_t          length;          /* Data length */
    uint64_t          cp_serial;       /* Checkpoint serial (if CP) */
    uint64_t          rpt_serial;      /* Report serial (if CP) */
    const uint8_t    *data;            /* Pointer to payload data */
} ltp_data_segment_t;

/* ---- Reception Report Claim ---- */
typedef struct {
    uint64_t offset;
    uint64_t length;
} ltp_claim_t;

/* ---- Reception Report Segment ---- */
typedef struct {
    ltp_segment_hdr_t hdr;
    uint64_t          rpt_serial;
    uint64_t          cp_serial;
    uint64_t          upper_bound;
    uint64_t          lower_bound;
    uint32_t          claim_count;
    ltp_claim_t       claims[LTP_MAX_CLAIMS];
} ltp_report_segment_t;

/* ---- Report Acknowledgment Segment ---- */
typedef struct {
    ltp_segment_hdr_t hdr;
    uint64_t          rpt_serial;
} ltp_report_ack_segment_t;

/* ---- Cancel Segment ---- */
typedef struct {
    ltp_segment_hdr_t hdr;
    uint8_t           reason;  /* Cancel reason code */
} ltp_cancel_segment_t;

/* ---- Timer Entry ---- */
typedef struct {
    int      active;
    int      type;              /* 0=checkpoint, 1=report, 2=cancel */
    uint64_t session_engine_id;
    uint64_t session_number;
    uint64_t serial;            /* CP or report serial */
    int      retries;
    struct timespec expiry;
} ltp_timer_t;

/* ---- Receive Bitmap (tracks received byte ranges) ---- */
typedef struct {
    uint32_t claim_count;
    ltp_claim_t claims[LTP_MAX_CLAIMS];
} ltp_recv_map_t;

/* ---- Export Session ---- */
typedef struct {
    int      active;
    uint64_t engine_id;         /* Local engine ID (session originator) */
    uint64_t session_number;
    uint64_t remote_engine_id;
    uint8_t  block_data[LTP_MAX_BLOCK_SIZE];
    uint32_t block_len;
    uint32_t segment_mtu;
    uint64_t next_cp_serial;
    int      completed;         /* All data acknowledged */
    int      cancelled;
} ltp_export_session_t;

/* ---- Import Session ---- */
typedef struct {
    int      active;
    uint64_t engine_id;         /* Remote engine ID (session originator) */
    uint64_t session_number;
    uint8_t  block_data[LTP_MAX_BLOCK_SIZE];
    uint32_t block_len;         /* Expected total (from EoRP offset+len) */
    ltp_recv_map_t recv_map;
    uint64_t next_rpt_serial;
    int      complete;          /* All data received */
    int      delivered;
} ltp_import_session_t;

/* ---- Endpoint Mapping ---- */
typedef struct {
    char     eid[64];           /* e.g. "dtn://g4dpz-1" */
    uint64_t engine_id;
} ltp_endpoint_t;

/* ---- Engine Configuration ---- */
typedef struct {
    uint32_t segment_mtu;       /* Max data payload per segment */
    uint32_t owlt_ms;           /* One-way light time estimate (ms) */
    uint32_t max_retries;       /* Max retransmission attempts */
    uint32_t max_block_size;    /* Max block size */
    int      verbose;
} ltp_config_t;

/* ---- LTP Engine ---- */
typedef struct {
    ltp_config_t         config;
    uint64_t             local_engine_id;
    char                 local_eid[64];
    uint64_t             next_session_number;

    ltp_export_session_t export_sessions[LTP_MAX_EXPORT_SESSIONS];
    ltp_import_session_t import_sessions[LTP_MAX_IMPORT_SESSIONS];
    ltp_timer_t          timers[LTP_MAX_TIMERS];
    ltp_endpoint_t       endpoints[LTP_MAX_ENDPOINTS];
    uint32_t             endpoint_count;

    /* Callback for delivered blocks */
    void (*on_block_received)(const uint8_t *data, uint32_t len,
                              uint64_t remote_engine_id, void *ctx);
    void *cb_ctx;

    /* Statistics */
    uint32_t segments_sent;
    uint32_t segments_received;
    uint32_t blocks_delivered;
    uint32_t sessions_completed;
    uint32_t sessions_cancelled;
} ltp_engine_t;

/* ---- Engine Lifecycle ---- */
int  ltp_engine_init(ltp_engine_t *eng, const char *local_eid,
                     const ltp_config_t *config);

/* ---- Block Transmission ---- */
int  ltp_send_block(ltp_engine_t *eng, int fd,
                    const char *remote_eid,
                    const uint8_t *data, uint32_t len);

/* ---- Segment Processing (called when KISS delivers a payload) ---- */
int  ltp_process_segment(ltp_engine_t *eng, int fd,
                         const uint8_t *buf, size_t len);

/* ---- Timer Management ---- */
int  ltp_get_next_timeout_ms(const ltp_engine_t *eng);
int  ltp_fire_expired_timers(ltp_engine_t *eng, int fd);

/* ---- Event Loop (runs until session completes or signal) ---- */
int  ltp_engine_run(ltp_engine_t *eng, int fd, int send_mode);

/* ---- Session Cancellation ---- */
int  ltp_cancel_session(ltp_engine_t *eng, int fd,
                        uint64_t session_number);

/* ---- Endpoint Mapping ---- */
uint64_t ltp_eid_to_engine_id(const char *eid);
int      ltp_register_endpoint(ltp_engine_t *eng, const char *eid);

/* ---- Segment Encoding ---- */
int  ltp_encode_data_segment(const ltp_data_segment_t *seg,
                             uint8_t *out, size_t out_size);
int  ltp_encode_report(const ltp_report_segment_t *rpt,
                       uint8_t *out, size_t out_size);
int  ltp_encode_report_ack(const ltp_report_ack_segment_t *ack,
                           uint8_t *out, size_t out_size);
int  ltp_encode_cancel(const ltp_cancel_segment_t *cancel,
                       uint8_t *out, size_t out_size);

/* ---- Block Segmentation (callback-based, for testing) ---- */
typedef void (*ltp_segment_cb)(const ltp_data_segment_t *seg, void *ctx);
int  ltp_segment_block(ltp_engine_t *eng, uint64_t remote_engine_id,
                       const uint8_t *data, uint32_t len,
                       ltp_segment_cb cb, void *ctx);

/* ---- Receive Map Helpers ---- */
void ltp_recv_map_add_claim(ltp_recv_map_t *map, uint64_t offset,
                            uint64_t length);

/* ---- Segment Decoding ---- */
int  ltp_decode_segment(const uint8_t *buf, size_t len,
                        ltp_segment_hdr_t *hdr,
                        uint8_t *body, size_t body_size,
                        size_t *body_len);

/* ---- Type-specific decode helpers ---- */
int  ltp_decode_data_content(const uint8_t *body, size_t body_len,
                             ltp_seg_type_t type,
                             ltp_data_segment_t *out);
int  ltp_decode_report_content(const uint8_t *body, size_t body_len,
                               ltp_report_segment_t *out);
int  ltp_decode_report_ack_content(const uint8_t *body, size_t body_len,
                                   ltp_report_ack_segment_t *out);
int  ltp_decode_cancel_content(const uint8_t *body, size_t body_len,
                               ltp_cancel_segment_t *out);

#endif
