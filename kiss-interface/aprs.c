#define _POSIX_C_SOURCE 200809L

#include "aprs.h"
#include "ax25.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

int aprs_is_ax25_frame(const uint8_t *payload, size_t len)
{
    if (!payload || len < 16) return 0;
    if (payload[14] != 0x03) return 0;
    if (payload[15] != 0xF0) return 0;
    return 1;
}

/* Parse DDMM.MM lat string (8 chars: "DDMM.MMH") into decimal degrees */
static int parse_lat(const char *s, double *out)
{
    if (!s || !out) return -1;
    /* DD */
    char dbuf[3] = { s[0], s[1], '\0' };
    char mbuf[6] = { s[2], s[3], s[4], s[5], s[6], '\0' };
    char hem = s[7];
    if (hem != 'N' && hem != 'S') return -1;

    int deg = atoi(dbuf);
    double min = atof(mbuf);
    if (deg < 0 || deg > 90 || min < 0.0 || min >= 60.0) return -1;

    *out = (double)deg + min / 60.0;
    if (hem == 'S') *out = -(*out);
    return 0;
}

/* Parse DDDMM.MM lon string (9 chars: "DDDMM.MMH") into decimal degrees */
static int parse_lon(const char *s, double *out)
{
    if (!s || !out) return -1;
    char dbuf[4] = { s[0], s[1], s[2], '\0' };
    char mbuf[6] = { s[3], s[4], s[5], s[6], s[7], '\0' };
    char hem = s[8];
    if (hem != 'E' && hem != 'W') return -1;

    int deg = atoi(dbuf);
    double min = atof(mbuf);
    if (deg < 0 || deg > 180 || min < 0.0 || min >= 60.0) return -1;

    *out = (double)deg + min / 60.0;
    if (hem == 'W') *out = -(*out);
    return 0;
}

/* Parse uncompressed position starting at p, with remaining length rem.
 * Format: DDMM.MMN<sym_table>DDDMM.MMW<sym_code><comment> */
static int parse_uncompressed(const char *p, size_t rem, aprs_position_t *pos)
{
    /* Need at least 19 chars: 8 lat + 1 sym_table + 9 lon + 1 sym_code */
    if (rem < 19) return -1;

    if (parse_lat(p, &pos->lat) != 0) return -1;
    pos->symbol_table = p[8];
    if (parse_lon(p + 9, &pos->lon) != 0) return -1;
    pos->symbol_code = p[18];
    pos->has_position = 1;

    /* Comment is everything after byte 19 */
    size_t clen = (rem > 19) ? rem - 19 : 0;
    if (clen >= sizeof(pos->comment)) clen = sizeof(pos->comment) - 1;
    if (clen > 0) memcpy(pos->comment, p + 19, clen);
    pos->comment[clen] = '\0';

    return 0;
}

int aprs_decode_position(const uint8_t *info, size_t info_len,
                         aprs_position_t *pos)
{
    if (!info || !pos || info_len < 1) return -1;

    memset(pos, 0, sizeof(*pos));

    char dtype = (char)info[0];
    const char *p = (const char *)info;

    if (dtype == '!' || dtype == '=') {
        /* Position without timestamp: data starts at byte 1 */
        return parse_uncompressed(p + 1, info_len - 1, pos);
    }

    if (dtype == '/' || dtype == '@') {
        /* Position with timestamp: 7-byte timestamp then position */
        if (info_len < 8) return -1;
        return parse_uncompressed(p + 8, info_len - 8, pos);
    }

    return -1; /* Unsupported type */
}

void aprs_log_packet(const uint8_t *ax25_frame, size_t frame_len,
                     int verbose)
{
    char src[16], dst[16];
    const uint8_t *info = NULL;

    int info_len = ax25_strip_frame(ax25_frame, frame_len, src, dst, &info);
    if (info_len < 0) return;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    aprs_position_t pos;
    if (info_len > 0 && aprs_decode_position(info, (size_t)info_len, &pos) == 0
        && pos.has_position) {
        printf("[%s] APRS %s > %s: %.4f, %.4f %s\n",
               ts, src, dst, pos.lat, pos.lon, pos.comment);
    } else {
        printf("[%s] AX25 %s > %s: ", ts, src, dst);
        if (info_len > 0) fwrite(info, 1, (size_t)info_len, stdout);
        printf("\n");
    }

    if (verbose && info_len > 0) {
        printf("  Raw (%d bytes):", info_len);
        for (int i = 0; i < info_len; i++) printf(" %02X", info[i]);
        printf("\n");
    }

    fflush(stdout);
}
