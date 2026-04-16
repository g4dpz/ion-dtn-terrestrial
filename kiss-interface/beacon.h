/*
 * beacon.h — APRS position beacon module
 *
 * Constructs APRS position packets (AX.25 UI frames with uncompressed
 * position reports) and manages periodic beacon transmission scheduling.
 * Uses static allocation only; reuses ax25_build_frame and kiss_encode.
 */

#ifndef BEACON_H
#define BEACON_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Maximum APRS position + comment string length */
#define BEACON_MAX_COMMENT    128
#define BEACON_MAX_POSITION   256  /* "!DDMM.MMN/DDDMM.MMW-" + comment */
#define BEACON_TOCALL         "APZ001"
#define BEACON_DEFAULT_COMMENT "github.com/g4dpz/ion-dtn-terrestrial"
#define BEACON_DEFAULT_INTERVAL 120
#define BEACON_MIN_INTERVAL   10
#define BEACON_MAX_INTERVAL   3600

/* Pre-built beacon frame (AX.25 + KISS encoded, ready to write) */
typedef struct {
    /* Configuration */
    char     callsign[10];           /* "CALL-SSID\0" */
    double   lat;                    /* Decimal degrees, -90 to +90 */
    double   lon;                    /* Decimal degrees, -180 to +180 */
    char     comment[BEACON_MAX_COMMENT];
    int      interval_sec;           /* Beacon interval in seconds */

    /* Pre-built APRS position info field */
    char     position_info[BEACON_MAX_POSITION];
    size_t   position_info_len;

    /* Pre-built KISS frame (ready to write to serial) */
    uint8_t  kiss_frame[600];        /* Generous: AX25_HDR + position + KISS overhead */
    int      kiss_frame_len;

    /* Timing */
    struct timespec last_tx;         /* CLOCK_MONOTONIC time of last beacon */
    int      initialized;            /* 1 if init succeeded */
} beacon_state_t;

/* Format latitude (decimal degrees) to APRS DDMM.MMN format.
 * out must be at least 9 bytes ("DDMM.MMN\0").
 * Returns 0 on success, -1 on error (out of range). */
int beacon_format_lat(double lat, char *out, size_t out_size);

/* Format longitude (decimal degrees) to APRS DDDMM.MMW format.
 * out must be at least 10 bytes ("DDDMM.MMW\0").
 * Returns 0 on success, -1 on error (out of range). */
int beacon_format_lon(double lon, char *out, size_t out_size);

/* Build the APRS position info field string.
 * Format: "!DDMM.MMN/DDDMM.MMW-<comment>"
 * out must be at least BEACON_MAX_POSITION bytes.
 * Returns length of info string, or -1 on error. */
int beacon_build_position(double lat, double lon, const char *comment,
                          char *out, size_t out_size);

/* Initialize beacon state: validate inputs, format position, pre-build
 * the AX.25 + KISS frame. Returns 0 on success, -1 on error. */
int beacon_init(beacon_state_t *state,
                const char *callsign, double lat, double lon,
                const char *comment, int interval_sec);

/* Transmit the pre-built beacon frame on fd.
 * Updates last_tx timestamp. Logs to stdout.
 * Returns 0 on success, -1 on write error. */
int beacon_transmit(beacon_state_t *state, int fd);

/* Get milliseconds until next beacon is due.
 * Returns 0 if beacon is due now, positive ms otherwise.
 * Returns -1 if not initialized. */
int beacon_get_timeout_ms(const beacon_state_t *state);

/* Check if the beacon interval has elapsed.
 * Returns 1 if due, 0 if not, -1 if not initialized. */
int beacon_is_due(const beacon_state_t *state);

#endif
