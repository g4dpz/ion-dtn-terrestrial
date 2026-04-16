/*
 * ping.c — Ping payload construction, parsing, and timestamp helper
 *
 * Payload layout (14 bytes):
 *   [0..3]  ASCII "PING"
 *   [4..5]  uint16_t sequence number, network byte order
 *   [6..13] int64_t  timestamp in microseconds, network byte order
 */

#define _POSIX_C_SOURCE 200809L

#include "ping.h"

#include <string.h>
#include <time.h>
#include <arpa/inet.h>  /* htons, ntohs */

/* ------------------------------------------------------------------ */
/* Big-endian helpers for 64-bit values (no standard htonll)           */
/* ------------------------------------------------------------------ */

static void put_be64(uint8_t *out, int64_t val)
{
    uint64_t u = (uint64_t)val;
    for (int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(u & 0xFF);
        u >>= 8;
    }
}

static int64_t get_be64(const uint8_t *in)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u = (u << 8) | in[i];
    return (int64_t)u;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int ping_build_payload(uint16_t seq, int64_t tx_us,
                       uint8_t *out, size_t out_size)
{
    if (out_size < PING_PAYLOAD_LEN)
        return -1;

    /* Magic */
    memcpy(out, PING_MAGIC, PING_MAGIC_LEN);

    /* Sequence number — network byte order */
    uint16_t net_seq = htons(seq);
    memcpy(out + 4, &net_seq, 2);

    /* Timestamp — network byte order */
    put_be64(out + 6, tx_us);

    return 0;
}

int ping_parse_payload(const uint8_t *data, size_t len,
                       uint16_t *seq, int64_t *tx_us)
{
    if (len < PING_PAYLOAD_LEN)
        return -1;

    /* Verify magic */
    if (memcmp(data, PING_MAGIC, PING_MAGIC_LEN) != 0)
        return -1;

    /* Extract sequence number */
    uint16_t net_seq;
    memcpy(&net_seq, data + 4, 2);
    *seq = ntohs(net_seq);

    /* Extract timestamp */
    *tx_us = get_be64(data + 6);

    return 0;
}

int64_t ping_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
