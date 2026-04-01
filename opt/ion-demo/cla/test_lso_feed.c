/*
 * test_lso_feed.c - Feed test LTP segments to seriallso via stdout
 *
 * Simulates what ION's ltpclo does: writes 4-byte big-endian length
 * prefix followed by the LTP segment data to stdout.
 *
 * Usage: ./test_lso_feed | ./seriallso /dev/ttyUSB0:9600
 *    or: ./test_lso_feed <message> <count> <interval>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

static int write_segment(const uint8_t *data, uint32_t len)
{
    /* 4-byte big-endian length prefix */
    uint8_t hdr[4];
    hdr[0] = (len >> 24) & 0xFF;
    hdr[1] = (len >> 16) & 0xFF;
    hdr[2] = (len >> 8)  & 0xFF;
    hdr[3] = len & 0xFF;

    if (fwrite(hdr, 1, 4, stdout) != 4) return -1;
    if (fwrite(data, 1, len, stdout) != len) return -1;
    fflush(stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *message = (argc > 1) ? argv[1] : "Test LTP segment";
    int count = (argc > 2) ? atoi(argv[2]) : 5;
    int interval = (argc > 3) ? atoi(argv[3]) : 3;

    fprintf(stderr, "test_lso_feed: sending %d segments, interval %ds\n", count, interval);
    fprintf(stderr, "test_lso_feed: message = \"%s\" (%zu bytes)\n", message, strlen(message));

    for (int i = 0; i < count; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[%d] %s", i + 1, message);
        uint32_t len = (uint32_t)strlen(buf);

        if (write_segment((uint8_t *)buf, len) < 0) {
            fprintf(stderr, "test_lso_feed: write failed\n");
            return 1;
        }

        fprintf(stderr, "test_lso_feed: sent segment %d/%d (%u bytes)\n", i + 1, count, len);

        if (i < count - 1) sleep(interval);
    }

    fprintf(stderr, "test_lso_feed: done\n");
    return 0;
}
