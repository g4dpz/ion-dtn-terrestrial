/*
 * seriallso.c - UDP-to-Serial AX.25/KISS bridge for ION-DTN LTP
 *
 * Receives LTP segments via UDP (from ION's udplso), wraps each in
 * an AX.25 UI frame with callsign identification, KISS-encodes, and
 * writes to a serial device (Mobilinkd TNC3) for RF transmission.
 *
 * Usage: seriallso <listen_port> <device>:<baud> <src_call> <dest_call>
 * Example: seriallso 1114 /dev/ttyUSB0:9600 G4DPZ-1 G4DPZ-2
 *
 * ION ltprc configuration:
 *   a span 2 128 128 1400 10000 1 'udplso localhost:1114 2'
 *   s 'udplsi 0.0.0.0:1113'
 *
 * Then run seriallso separately:
 *   seriallso 1114 /dev/ttyUSB0:9600 G4DPZ-1 G4DPZ-2 &
 *
 * Data flow:
 *   ION ltpclo → udplso → UDP:1114 → seriallso → AX.25/KISS → serial → TNC → RF
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* KISS constants */
#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define MAX_SEGMENT_SIZE 2048
#define AX25_HEADER_SIZE 16
#define MAX_AX25_FRAME   (AX25_HEADER_SIZE + MAX_SEGMENT_SIZE)
#define MAX_KISS_FRAME   (MAX_AX25_FRAME * 2 + 4)

static volatile int running = 1;
static void handle_signal(int sig) { (void)sig; running = 0; }

/* AX.25 callsign encoding */
static void encode_ax25_addr(const char *callsign, uint8_t *out, int last)
{
    char call[7] = "      ";
    uint8_t ssid = 0;
    const char *dash = strchr(callsign, '-');
    if (dash) {
        size_t len = dash - callsign;
        if (len > 6) len = 6;
        memcpy(call, callsign, len);
        ssid = atoi(dash + 1) & 0x0F;
    } else {
        size_t len = strlen(callsign);
        if (len > 6) len = 6;
        memcpy(call, callsign, len);
    }
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)(toupper(call[i])) << 1;
    out[6] = 0x60 | ((ssid & 0x0F) << 1) | (last ? 0x01 : 0x00);
}

static int build_ax25_frame(const char *dest_call, const char *src_call,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t out_size)
{
    size_t total = AX25_HEADER_SIZE + payload_len;
    if (total > out_size) return -1;
    size_t idx = 0;
    encode_ax25_addr(dest_call, out + idx, 0); idx += 7;
    encode_ax25_addr(src_call,  out + idx, 1); idx += 7;
    out[idx++] = 0x03;
    out[idx++] = 0xF0;
    memcpy(out + idx, payload, payload_len);
    return (int)(idx + payload_len);
}

/* Serial port */
static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 1200: return B1200; case 2400: return B2400;
    case 4800: return B4800; case 9600: return B9600;
    case 19200: return B19200; case 38400: return B38400;
    case 57600: return B57600; case 115200: return B115200;
    default: return B9600;
    }
}

static int open_serial(const char *device, int baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "seriallso: open(%s): %s\n", device, strerror(errno)); return -1; }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }
    cfsetospeed(&tty, baud_to_speed(baud));
    cfsetispeed(&tty, baud_to_speed(baud));
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_lflag = 0; tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0; tty.c_cc[VTIME] = 10;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* KISS encode + send */
static int kiss_send(int fd, const uint8_t *data, size_t len)
{
    uint8_t frame[MAX_KISS_FRAME];
    size_t idx = 0;
    frame[idx++] = FEND;
    frame[idx++] = 0x00;
    for (size_t i = 0; i < len; i++) {
        if (idx >= MAX_KISS_FRAME - 2) return -1;
        if (data[i] == FEND)      { frame[idx++] = FESC; frame[idx++] = TFEND; }
        else if (data[i] == FESC) { frame[idx++] = FESC; frame[idx++] = TFESC; }
        else                      { frame[idx++] = data[i]; }
    }
    frame[idx++] = FEND;
    size_t written = 0;
    while (written < idx) {
        ssize_t n = write(fd, frame + written, idx - written);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        written += n;
    }
    tcdrain(fd);
    return (int)idx;
}

static int parse_device(const char *arg, char *device, size_t dev_size, int *baud)
{
    const char *colon = strrchr(arg, ':');
    if (!colon) { strncpy(device, arg, dev_size - 1); device[dev_size-1] = '\0'; *baud = 9600; return 0; }
    size_t dev_len = colon - arg;
    if (dev_len >= dev_size) dev_len = dev_size - 1;
    memcpy(device, arg, dev_len); device[dev_len] = '\0';
    *baud = atoi(colon + 1);
    if (*baud <= 0) *baud = 9600;
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: seriallso <listen_port> <device>[:<baud>] <src_call> <dest_call> [hold_seconds] [burst:pause]\n");
        fprintf(stderr, "Example: seriallso 1114 /dev/ttyUSB0:9600 G4DPZ-1 G4DPZ-2 0 5:15\n");
        fprintf(stderr, "  hold_seconds: delay before first TX (default: 0)\n");
        fprintf(stderr, "  burst:pause:  send N segments then pause M seconds for half-duplex (default: off)\n");
        return 1;
    }

    int listen_port = atoi(argv[1]);
    char device[256]; int baud;
    parse_device(argv[2], device, sizeof(device), &baud);
    const char *src_call = argv[3];
    const char *dest_call = argv[4];
    int hold_seconds = (argc > 5) ? atoi(argv[5]) : 0;
    int burst_count = 0;   /* 0 = no pacing */
    int pause_seconds = 0;
    if (argc > 6) {
        if (sscanf(argv[6], "%d:%d", &burst_count, &pause_seconds) != 2) {
            burst_count = 0;
            pause_seconds = 0;
        }
    }
    time_t start_time = time(NULL);
    time_t tx_allowed_at = start_time + hold_seconds;

    fprintf(stderr, "seriallso: port=%d device=%s baud=%d src=%s dest=%s hold=%ds",
            listen_port, device, baud, src_call, dest_call, hold_seconds);
    if (burst_count > 0)
        fprintf(stderr, " pacing=%d:%d", burst_count, pause_seconds);
    fprintf(stderr, "\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* Open UDP listen socket */
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(udp_sock); return 1;
    }

    /* Open serial port */
    int serial_fd = open_serial(device, baud);
    if (serial_fd < 0) { close(udp_sock); return 1; }

    fprintf(stderr, "seriallso: listening on UDP:%d, sending to %s\n", listen_port, device);

    uint8_t segment[MAX_SEGMENT_SIZE];
    uint8_t ax25_frame[MAX_AX25_FRAME];
    uint64_t seg_count = 0;

    while (running) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(udp_sock, segment, sizeof(segment), 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) continue;

        /* Hold segments until contact window opens */
        if (hold_seconds > 0 && time(NULL) < tx_allowed_at) {
            time_t remaining = tx_allowed_at - time(NULL);
            fprintf(stderr, "seriallso: HOLDING segment (%zd bytes), %ld seconds until TX allowed\n",
                    n, (long)remaining);
            /* Block until TX time */
            while (time(NULL) < tx_allowed_at && running) {
                sleep(1);
            }
            if (!running) break;
            fprintf(stderr, "seriallso: Contact window OPEN, transmitting held + new segments\n");
        }

        /* Wrap in AX.25 UI frame */
        int ax25_len = build_ax25_frame(dest_call, src_call, segment, n,
                                        ax25_frame, sizeof(ax25_frame));
        if (ax25_len < 0) continue;

        /* KISS encode and send to serial */
        int kiss_len = kiss_send(serial_fd, ax25_frame, ax25_len);
        if (kiss_len < 0) continue;

        seg_count++;
        fprintf(stderr, "seriallso: #%llu LTP(%zd) -> AX.25(%d) -> KISS(%d)\n",
                (unsigned long long)seg_count, n, ax25_len, kiss_len);

        /* Half-duplex pacing: after N segments, pause to let remote side respond */
        if (burst_count > 0 && (seg_count % burst_count) == 0) {
            fprintf(stderr, "seriallso: PAUSE %ds (half-duplex, sent %d segments)\n",
                    pause_seconds, burst_count);
            sleep(pause_seconds);
        }
    }

    close(udp_sock);
    close(serial_fd);
    fprintf(stderr, "seriallso: exiting, sent %llu segments\n",
            (unsigned long long)seg_count);
    return 0;
}
