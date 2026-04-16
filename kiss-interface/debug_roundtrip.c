#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#include "beacon.h"
#include "ax25.h"
#include "kiss.h"

static double rand_double(double lo, double hi)
{
    return lo + ((double)rand() / (double)RAND_MAX) * (hi - lo);
}

static void rand_comment(char *buf, size_t buf_size, int max_len)
{
    int len = rand() % (max_len + 1);
    if ((size_t)len >= buf_size) len = (int)buf_size - 1;
    for (int i = 0; i < len; i++)
        buf[i] = (char)(32 + (rand() % 95));
    buf[len] = '\0';
}

static void rand_callsign(char *buf, size_t buf_size)
{
    static const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int base_len = 1 + (rand() % 6);
    int i;
    for (i = 0; i < base_len; i++)
        buf[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

    int ssid = rand() % 16;
    if (ssid > 0 || (rand() % 2)) {
        int written = snprintf(buf + base_len, buf_size - (size_t)base_len,
                               "-%d", ssid);
        buf[base_len + written] = '\0';
    } else {
        buf[base_len] = '\0';
    }
}

int main(void)
{
    srand((unsigned)time(NULL));

    char callsign[10];
    rand_callsign(callsign, sizeof(callsign));

    double lat = rand_double(-90.0, 90.0);
    double lon = rand_double(-180.0, 180.0);

    char comment[64];
    rand_comment(comment, sizeof(comment), 50);

    printf("callsign='%s' lat=%f lon=%f comment='%s'\n", callsign, lat, lon, comment);

    beacon_state_t state;
    int rc = beacon_init(&state, callsign, lat, lon, comment, 120);
    printf("beacon_init returned %d\n", rc);
    printf("kiss_frame_len=%d\n", state.kiss_frame_len);

    /* KISS-decode */
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);
    uint8_t ax25_buf[512];
    size_t ax25_len = 0;
    int got_frame = 0;
    for (int i = 0; i < state.kiss_frame_len; i++) {
        int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                  ax25_buf, sizeof(ax25_buf), &ax25_len);
        if (r == 1) { got_frame = 1; break; }
    }
    printf("got_frame=%d ax25_len=%zu\n", got_frame, ax25_len);

    if (got_frame) {
        printf("AX.25 frame hex: ");
        for (size_t i = 0; i < ax25_len && i < 32; i++)
            printf("%02X ", ax25_buf[i]);
        printf("\n");

        char src_call[AX25_MAX_CALLSIGN];
        char dst_call[AX25_MAX_CALLSIGN];
        memset(src_call, 0, sizeof(src_call));
        memset(dst_call, 0, sizeof(dst_call));
        const uint8_t *info = NULL;
        int info_len = ax25_strip_frame(ax25_buf, ax25_len,
                                        src_call, dst_call, &info);
        printf("strip_frame returned %d\n", info_len);
        printf("src_call='%s' dst_call='%s'\n", src_call, dst_call);
    }

    return 0;
}
