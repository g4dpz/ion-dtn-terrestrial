#ifndef APRS_H
#define APRS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    double lat;
    double lon;
    char   symbol_table;
    char   symbol_code;
    char   comment[128];
    int    has_position;
} aprs_position_t;

/* Returns 1 if payload is an AX.25 UI frame, 0 otherwise. */
int aprs_is_ax25_frame(const uint8_t *payload, size_t len);

/* Parse APRS position from info field. Returns 0 on success, -1 if not a position. */
int aprs_decode_position(const uint8_t *info, size_t info_len,
                         aprs_position_t *pos);

/* Log a received AX.25/APRS packet to stdout. */
void aprs_log_packet(const uint8_t *ax25_frame, size_t frame_len,
                     int verbose);

#endif
