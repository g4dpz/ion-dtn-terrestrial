/*
 * test_lsi_recv.c - Receive LTP segments forwarded by seriallsi
 *
 * Listens on a UDP port and prints received segments.
 * Simulates what ION's ltpcli would do on the receiving end.
 *
 * Usage: ./test_lsi_recv [port]
 *        Default port: 1113
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_SEGMENT 2048

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : 1113;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    fprintf(stderr, "test_lsi_recv: listening on localhost:%d\n", port);

    uint8_t buf[MAX_SEGMENT];
    uint64_t count = 0;

    while (running) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        count++;
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", tm);

        printf("[%s] Segment #%llu (%zd bytes)\n", ts, (unsigned long long)count, n);
        printf("  Hex: ");
        for (ssize_t i = 0; i < n && i < 64; i++) printf("%02X ", buf[i]);
        if (n > 64) printf("...");
        printf("\n");

        /* Try to print as text if it looks printable */
        int printable = 1;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] < 0x20 && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t') {
                printable = 0;
                break;
            }
        }
        if (printable && n > 0) {
            buf[n] = '\0';
            printf("  Text: \"%s\"\n", buf);
        }
        printf("\n");
    }

    close(sock);
    fprintf(stderr, "test_lsi_recv: received %llu segments\n", (unsigned long long)count);
    return 0;
}
