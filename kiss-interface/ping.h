/*
 * ping.h — Ping payload construction and parsing
 *
 * Fixed 14-byte payload: 4-byte magic "PING" + 2-byte sequence (NBO)
 * + 8-byte timestamp (NBO, microseconds from CLOCK_MONOTONIC).
 */

#ifndef PING_H
#define PING_H

#include <stdint.h>
#include <stddef.h>

#define PING_MAGIC      "PING"
#define PING_MAGIC_LEN  4
#define PING_PAYLOAD_LEN 14  /* 4 magic + 2 seq + 8 timestamp */

/* Build a ping payload into out (must be >= PING_PAYLOAD_LEN bytes).
 * seq: sequence number (network byte order in output).
 * tx_us: transmit timestamp in microseconds (from clock_gettime CLOCK_MONOTONIC).
 * Returns 0 on success, -1 on error. */
int ping_build_payload(uint16_t seq, int64_t tx_us,
                       uint8_t *out, size_t out_size);

/* Parse a ping payload from a buffer.
 * Returns 0 on success, -1 if magic mismatch or buffer too small.
 * Sets *seq and *tx_us on success. */
int ping_parse_payload(const uint8_t *data, size_t len,
                       uint16_t *seq, int64_t *tx_us);

/* Get current monotonic time in microseconds. */
int64_t ping_now_us(void);

#endif
