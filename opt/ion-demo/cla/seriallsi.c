/*
 * seriallsi.c - Serial AX.25/KISS Link Service Input for ION-DTN LTP
 *
 * Reads KISS frames from a serial device (Mobilinkd TNC3), strips
 * the AX.25 UI frame header (extracting callsigns for logging),
 * and forwards the LTP segment payload to ltpcli via UDP loopback.
 *
 * Usage: seriallsi <device>:<baud> [<ltpcli_port>]
 * Example: seriallsi /dev/ttyUSB0:9600 1113
 *
 * On-air frame structure received:
 *   KISS [ AX.25 UI frame [ LTP segment ] ]
 *
 * This program runs as a daemon alongside ION.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ── KISS constants ── */
#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define MAX_SEGMENT_SIZE 2048
#define AX25_HEADER_SIZE 16
#define SERIAL_READ_BUF  4096
#define DEFAULT_LTPCLI_PORT 1113

static volatile int running = 1;

static void handle_signal(int sig) { (void)sig; running = 0; }

/* ── AX.25 callsign decoding ── */

static void decode_ax25_addr(const uint8_t *addr, char *callsign)
{
    char call[7];
    for (int i = 0; i < 6; i++)
        call[i] = addr[i] >> 1;
    call[6] = '\0';

    /* Trim trailing spaces */
    for (int i = 5; i >= 0; i--) {
        if (call[i] == ' ') call[i] = '\0';
        else break;
    }

    uint8_t ssid = (addr[6] >> 1) & 0x0F;
    if (ssid > 0)
        sprintf(callsign, "%s-%d", call, ssid);
    else
        strcpy(callsign, call);
}

/*
 * Parse AX.25 UI frame. Returns pointer to payload and its length.
 * Logs source and destination callsigns.
 * Returns payload length, or -1 if not a valid AX.25 UI frame.
 */
static int strip_ax25(const uint8_t *frame, size_t frame_len,
                      const uint8_t **payload,
                      char *src_call, char *dest_call)
{
    if (frame_len < AX25_HEADER_SIZE) return -1;

    decode_ax25_addr(frame, dest_call);
    decode_ax25_addr(frame + 7, src_call);

    /* Verify UI frame: control=0x03, PID=0xF0 */
    if (frame[14] != 0x03 || frame[15] != 0xF0) return -1;

    *payload = frame + AX25_HEADER_SIZE;
    return (int)(frame_len - AX25_HEADER_SIZE);
}

/* ── Serial port ── */

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

static int open_serial(const char *device, int baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "seriallsi: open(%s): %s\n", device, strerror(errno)); return -1; }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }

    speed_t speed = baud_to_speed(baud);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ── UDP sender ── */

static int open_udp_sender(int port, struct sockaddr_in *dest_addr)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    memset(dest_addr, 0, sizeof(*dest_addr));
    dest_addr->sin_family = AF_INET;
    dest_addr->sin_port = htons(port);
    dest_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sock;
}

/* ── Argument parsing ── */

static int parse_device(const char *arg, char *device, size_t dev_size, int *baud)
{
    const char *colon = strrchr(arg, ':');
    if (!colon) {
        strncpy(device, arg, dev_size - 1);
        device[dev_size - 1] = '\0';
        *baud = 9600;
        return 0;
    }
    size_t dev_len = colon - arg;
    if (dev_len >= dev_size) dev_len = dev_size - 1;
    memcpy(device, arg, dev_len);
    device[dev_len] = '\0';
    *baud = atoi(colon + 1);
    if (*baud <= 0) *baud = 9600;
    return 0;
}

/* ── KISS decoder state machine ── */

typedef struct {
    uint8_t buf[MAX_SEGMENT_SIZE + AX25_HEADER_SIZE];
    size_t  len;
    int     in_frame;
    int     escape;
} kiss_decoder_t;

static void kiss_decoder_init(kiss_decoder_t *dec)
{
    dec->len = 0;
    dec->in_frame = 0;
    dec->escape = 0;
}

typedef struct {
    int udp_sock;
    struct sockaddr_in dest_addr;
    uint64_t seg_count;
} forward_ctx_t;

static void on_kiss_frame(const uint8_t *frame_data, size_t frame_len, void *ctx)
{
    forward_ctx_t *fctx = (forward_ctx_t *)ctx;

    /* Strip AX.25 header, extract LTP payload */
    char src_call[16], dest_call[16];
    const uint8_t *ltp_payload;
    int ltp_len = strip_ax25(frame_data, frame_len, &ltp_payload, src_call, dest_call);

    if (ltp_len <= 0) {
        /* Not a valid AX.25 UI frame — try forwarding raw (backward compat) */
        ltp_payload = frame_data;
        ltp_len = (int)frame_len;
        strcpy(src_call, "???");
        strcpy(dest_call, "???");
    }

    /* Forward LTP segment to ltpcli via UDP */
    ssize_t n = sendto(fctx->udp_sock, ltp_payload, ltp_len, 0,
                       (struct sockaddr *)&fctx->dest_addr,
                       sizeof(fctx->dest_addr));
    if (n < 0) {
        fprintf(stderr, "seriallsi: sendto: %s\n", strerror(errno));
        return;
    }

    fctx->seg_count++;
    fprintf(stderr, "seriallsi: #%llu from %s to %s, AX.25(%zu) -> LTP(%d)\n",
            (unsigned long long)fctx->seg_count, src_call, dest_call,
            frame_len, ltp_len);
}

static int kiss_decoder_feed(kiss_decoder_t *dec,
                             const uint8_t *data, size_t data_len,
                             void (*on_frame)(const uint8_t *, size_t, void *),
                             void *ctx)
{
    int frames = 0;
    for (size_t i = 0; i < data_len; i++) {
        uint8_t byte = data[i];

        if (byte == FEND) {
            if (dec->in_frame && dec->len > 0) {
                if ((dec->buf[0] & 0x0F) == 0x00 && dec->len > 1) {
                    on_frame(dec->buf + 1, dec->len - 1, ctx);
                    frames++;
                }
            }
            dec->len = 0;
            dec->in_frame = 1;
            dec->escape = 0;
            continue;
        }
        if (!dec->in_frame) continue;

        if (dec->escape) {
            dec->escape = 0;
            if (byte == TFEND) byte = FEND;
            else if (byte == TFESC) byte = FESC;
        } else if (byte == FESC) {
            dec->escape = 1;
            continue;
        }

        if (dec->len < sizeof(dec->buf))
            dec->buf[dec->len++] = byte;
    }
    return frames;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: seriallsi <device>[:<baud>] [<ltpcli_port>]\n");
        return 1;
    }

    char device[256];
    int baud;
    parse_device(argv[1], device, sizeof(device), &baud);

    int ltpcli_port = DEFAULT_LTPCLI_PORT;
    if (argc >= 3) {
        ltpcli_port = atoi(argv[2]);
        if (ltpcli_port <= 0) ltpcli_port = DEFAULT_LTPCLI_PORT;
    }

    fprintf(stderr, "seriallsi: device=%s baud=%d ltpcli_port=%d\n",
            device, baud, ltpcli_port);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    int serial_fd = open_serial(device, baud);
    if (serial_fd < 0) return 1;

    forward_ctx_t fctx;
    fctx.seg_count = 0;
    fctx.udp_sock = open_udp_sender(ltpcli_port, &fctx.dest_addr);
    if (fctx.udp_sock < 0) { close(serial_fd); return 1; }

    fprintf(stderr, "seriallsi: listening, forwarding LTP to localhost:%d\n", ltpcli_port);

    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    uint8_t readbuf[SERIAL_READ_BUF];

    while (running) {
        ssize_t n = read(serial_fd, readbuf, sizeof(readbuf));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) continue;
        kiss_decoder_feed(&decoder, readbuf, n, on_kiss_frame, &fctx);
    }

    close(fctx.udp_sock);
    close(serial_fd);
    fprintf(stderr, "seriallsi: exiting, forwarded %llu segments\n",
            (unsigned long long)fctx.seg_count);
    return 0;
}
