/*
 * beacon.c — APRS position beacon module
 *
 * Implements APRS position formatting, beacon state management,
 * and periodic transmission scheduling. Uses static allocation only.
 */

#define _POSIX_C_SOURCE 199309L

#include "beacon.h"
#include "ax25.h"
#include "kiss.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* beacon_format_lat                                                   */
/* ------------------------------------------------------------------ */
int beacon_format_lat(double lat, char *out, size_t out_size)
{
    if (!out || out_size < 9)
        return -1;
    if (lat < -90.0 || lat > 90.0)
        return -1;

    char hemisphere = (lat >= 0.0) ? 'N' : 'S';
    double abs_lat = fabs(lat);
    int degrees = (int)abs_lat;
    double minutes = (abs_lat - (double)degrees) * 60.0;

    /* Clamp minutes to avoid 60.00 from floating-point rounding */
    if (minutes >= 59.995) minutes = 59.99;

    snprintf(out, out_size, "%02d%05.2f%c", degrees, minutes, hemisphere);
    return 0;
}

/* ------------------------------------------------------------------ */
/* beacon_format_lon                                                   */
/* ------------------------------------------------------------------ */
int beacon_format_lon(double lon, char *out, size_t out_size)
{
    if (!out || out_size < 10)
        return -1;
    if (lon < -180.0 || lon > 180.0)
        return -1;

    char hemisphere = (lon >= 0.0) ? 'E' : 'W';
    double abs_lon = fabs(lon);
    int degrees = (int)abs_lon;
    double minutes = (abs_lon - (double)degrees) * 60.0;

    /* Clamp minutes to avoid 60.00 from floating-point rounding */
    if (minutes >= 59.995) minutes = 59.99;

    snprintf(out, out_size, "%03d%05.2f%c", degrees, minutes, hemisphere);
    return 0;
}

/* ------------------------------------------------------------------ */
/* beacon_build_position                                               */
/* ------------------------------------------------------------------ */
int beacon_build_position(double lat, double lon, const char *comment,
                          char *out, size_t out_size)
{
    if (!out || out_size < 21)  /* minimum: "!" + 8 lat + "/" + 9 lon + "-" + NUL */
        return -1;

    char lat_str[16], lon_str[16];

    if (beacon_format_lat(lat, lat_str, sizeof(lat_str)) != 0)
        return -1;
    if (beacon_format_lon(lon, lon_str, sizeof(lon_str)) != 0)
        return -1;

    int len;
    if (comment && comment[0] != '\0') {
        len = snprintf(out, out_size, "!%s/%s-%s",
                       lat_str, lon_str, comment);
    } else {
        len = snprintf(out, out_size, "!%s/%s-",
                       lat_str, lon_str);
    }

    if (len < 0 || (size_t)len >= out_size)
        return -1;

    return len;
}

/* ------------------------------------------------------------------ */
/* beacon_init                                                         */
/* ------------------------------------------------------------------ */
int beacon_init(beacon_state_t *state,
                const char *callsign, double lat, double lon,
                const char *comment, int interval_sec)
{
    if (!state || !callsign)
        return -1;

    /* ---- Validate callsign ---- */
    /* Parse base call (before '-') and optional SSID */
    const char *dash = NULL;
    size_t base_len = 0;
    for (size_t i = 0; callsign[i] != '\0'; i++) {
        if (callsign[i] == '-') {
            dash = &callsign[i];
            base_len = i;
            break;
        }
    }
    if (!dash)
        base_len = strlen(callsign);

    /* Base call must be 1-6 alphanumeric characters */
    if (base_len == 0 || base_len > 6)
        return -1;
    for (size_t i = 0; i < base_len; i++) {
        unsigned char c = (unsigned char)callsign[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9')))
            return -1;
    }

    /* SSID (if present) must be 0-15 */
    if (dash) {
        const char *ssid_str = dash + 1;
        if (ssid_str[0] == '\0')
            return -1;  /* trailing dash with no SSID */
        /* Parse SSID digits */
        int ssid = 0;
        for (size_t i = 0; ssid_str[i] != '\0'; i++) {
            if (ssid_str[i] < '0' || ssid_str[i] > '9')
                return -1;
            ssid = ssid * 10 + (ssid_str[i] - '0');
            if (ssid > 15)
                return -1;
        }
    }

    /* ---- Validate lat/lon ---- */
    if (lat < -90.0 || lat > 90.0)
        return -1;
    if (lon < -180.0 || lon > 180.0)
        return -1;

    /* ---- Validate interval ---- */
    if (interval_sec < BEACON_MIN_INTERVAL || interval_sec > BEACON_MAX_INTERVAL)
        return -1;

    /* ---- Zero out state ---- */
    memset(state, 0, sizeof(*state));

    /* ---- Store configuration ---- */
    /* Copy full callsign-SSID string */
    size_t cs_len = strlen(callsign);
    if (cs_len >= sizeof(state->callsign))
        return -1;
    memcpy(state->callsign, callsign, cs_len + 1);

    state->lat = lat;
    state->lon = lon;
    state->interval_sec = interval_sec;

    /* Copy comment (truncate if needed) */
    if (comment && comment[0] != '\0') {
        size_t clen = strlen(comment);
        if (clen >= sizeof(state->comment))
            clen = sizeof(state->comment) - 1;
        memcpy(state->comment, comment, clen);
        state->comment[clen] = '\0';
    } else {
        state->comment[0] = '\0';
    }

    /* ---- Build APRS position info field ---- */
    int info_len = beacon_build_position(lat, lon,
                                         (comment && comment[0] != '\0') ? comment : "",
                                         state->position_info,
                                         sizeof(state->position_info));
    if (info_len < 0)
        return -1;
    state->position_info_len = (size_t)info_len;

    /* ---- Build AX.25 UI frame ---- */
    uint8_t ax25_frame[512];
    int ax25_len = ax25_build_frame(BEACON_TOCALL, callsign,
                                    (const uint8_t *)state->position_info,
                                    state->position_info_len,
                                    ax25_frame, sizeof(ax25_frame));
    if (ax25_len < 0)
        return -1;

    /* ---- KISS-encode the AX.25 frame ---- */
    int kiss_len = kiss_encode(ax25_frame, (size_t)ax25_len,
                               state->kiss_frame, sizeof(state->kiss_frame));
    if (kiss_len < 0)
        return -1;
    state->kiss_frame_len = kiss_len;

    /* ---- Mark initialized ---- */
    state->initialized = 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/* beacon_transmit                                                     */
/* ------------------------------------------------------------------ */
int beacon_transmit(beacon_state_t *state, int fd)
{
    if (!state || !state->initialized)
        return -1;

    ssize_t written = write(fd, state->kiss_frame, (size_t)state->kiss_frame_len);
    if (written < 0) {
        fprintf(stderr, "error: beacon write failed: %s\n", strerror(errno));
        return -1;
    }
    if (written != state->kiss_frame_len) {
        fprintf(stderr, "error: beacon short write (%zd of %d bytes)\n",
                written, state->kiss_frame_len);
        return -1;
    }
    tcdrain(fd);

    /* Record timestamp */
    clock_gettime(CLOCK_MONOTONIC, &state->last_tx);

    /* Log */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    printf("[%s] Beacon: %s\n", ts, state->callsign);
    fflush(stdout);

    return 0;
}

/* ------------------------------------------------------------------ */
/* beacon_get_timeout_ms                                               */
/* ------------------------------------------------------------------ */
int beacon_get_timeout_ms(const beacon_state_t *state)
{
    if (!state || !state->initialized)
        return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* next_tx = last_tx + interval */
    int64_t last_ms = (int64_t)state->last_tx.tv_sec * 1000 +
                      state->last_tx.tv_nsec / 1000000;
    int64_t now_ms  = (int64_t)now.tv_sec * 1000 +
                      now.tv_nsec / 1000000;
    int64_t next_ms = last_ms + (int64_t)state->interval_sec * 1000;
    int64_t diff    = next_ms - now_ms;

    if (diff <= 0) return 0;
    return (int)diff;
}

/* ------------------------------------------------------------------ */
/* beacon_is_due                                                       */
/* ------------------------------------------------------------------ */
int beacon_is_due(const beacon_state_t *state)
{
    if (!state || !state->initialized)
        return -1;

    return (beacon_get_timeout_ms(state) == 0) ? 1 : 0;
}
