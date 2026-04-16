/*
 * main.c — CLI parsing, mode dispatch, and signal handling
 *
 * Parses command-line arguments for send/receive/echo modes,
 * validates required arguments, installs signal handlers, and
 * dispatches to the appropriate mode function.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>

#include "kiss.h"
#include "ax25.h"
#include "serial.h"
#include "ping.h"

/* ------------------------------------------------------------------ */
/* Global signal flag                                                  */
/* ------------------------------------------------------------------ */
volatile sig_atomic_t g_running = 1;

/* ------------------------------------------------------------------ */
/* CLI argument structure                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    CMD_MODE_NONE,
    CMD_MODE_SEND,
    CMD_MODE_RECEIVE,
    CMD_MODE_ECHO,
    CMD_MODE_PING
} cmd_mode_t;

typedef struct {
    const char *device;      /* Serial device path */
    int         baud;        /* Baud rate (default 9600) */
    const char *src_call;    /* Source callsign */
    const char *dst_call;    /* Destination callsign */
    const char *payload;     /* Payload string (send mode only) */
    int         txdelay_ms;  /* TX-delay in ms (default 500) */
    int         txtail_ms;   /* TX-tail in ms (default 300) */
    int         delay_ms;    /* Echo delay in ms (default 1000) */
    int         verbose;     /* Verbose/debug output flag */
    int         count;       /* Ping count (default 4) */
    int         timeout_ms;  /* Per-ping timeout in ms (default 5000) */
    int         interval_ms; /* Inter-ping delay in ms (default 1000) */
    cmd_mode_t  mode;
} cli_args_t;

/* ------------------------------------------------------------------ */
/* print_usage                                                         */
/* ------------------------------------------------------------------ */
void print_usage(const char *prog)
{
    printf("Usage: %s <command> [options]\n", prog);
    printf("\n");
    printf("Commands:\n");
    printf("  send      Send a single packet\n");
    printf("  receive   Continuously receive and display packets\n");
    printf("  echo      Receive packets and retransmit with swapped callsigns\n");
    printf("  ping      Send ping packets and measure round-trip time\n");
    printf("\n");
    printf("Options:\n");
    printf("  --device <path[:baud]>  Serial device (required)\n");
    printf("  --src <callsign>        Source callsign (required for send/echo)\n");
    printf("  --dst <callsign>        Destination callsign (required for send/echo)\n");
    printf("  --txdelay <ms>          TX-delay in milliseconds (default: 500)\n");
    printf("  --txtail <ms>           TX-tail in milliseconds (default: 300)\n");
    printf("  --delay <ms>            Echo retransmit delay in milliseconds (default: 1000)\n");
    printf("  --count <n>             Number of ping packets to send (default: 4)\n");
    printf("  --timeout <ms>          Per-ping reply timeout in milliseconds (default: 5000)\n");
    printf("  --interval <ms>         Delay between pings in milliseconds (default: 1000)\n");
    printf("  --verbose               Enable verbose/debug output\n");
    printf("  --help                  Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s send --device /dev/ttyACM0 --src N0CALL --dst CQ \"Hello World\"\n", prog);
    printf("  %s receive --device /dev/ttyACM0:9600 --verbose\n", prog);
    printf("  %s echo --device /dev/ttyACM0 --src N0CALL --dst CQ --delay 2000\n", prog);
    printf("  %s ping --device /dev/ttyACM0 --src N0CALL --dst CQ --count 10 --timeout 3000\n", prog);
}

/* ------------------------------------------------------------------ */
/* signal_handler                                                      */
/* ------------------------------------------------------------------ */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/* install_signal_handlers                                             */
/* ------------------------------------------------------------------ */
static int install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* Do NOT set SA_RESTART — let blocking reads return EINTR */

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction(SIGINT)");
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("sigaction(SIGTERM)");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* parse_args                                                          */
/* ------------------------------------------------------------------ */
int parse_args(int argc, char *argv[], cli_args_t *args)
{
    if (!args)
        return -1;

    /* Set defaults */
    memset(args, 0, sizeof(*args));
    args->baud       = 9600;
    args->txdelay_ms = 500;
    args->txtail_ms  = 300;
    args->delay_ms   = 1000;
    args->verbose    = 0;
    args->count      = 4;
    args->timeout_ms = 5000;
    args->interval_ms = 1000;
    args->mode       = CMD_MODE_NONE;

    if (argc < 2)
        return 0;  /* No subcommand — caller will handle */

    /* First positional arg is the subcommand */
    const char *cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        exit(0);
    }

    if (strcmp(cmd, "send") == 0)
        args->mode = CMD_MODE_SEND;
    else if (strcmp(cmd, "receive") == 0)
        args->mode = CMD_MODE_RECEIVE;
    else if (strcmp(cmd, "echo") == 0)
        args->mode = CMD_MODE_ECHO;
    else if (strcmp(cmd, "ping") == 0)
        args->mode = CMD_MODE_PING;
    else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        return -1;
    }

    /* Parse remaining options with getopt_long */
    static struct option long_opts[] = {
        { "device",   required_argument, NULL, 'd' },
        { "src",      required_argument, NULL, 's' },
        { "dst",      required_argument, NULL, 'D' },
        { "txdelay",  required_argument, NULL, 't' },
        { "txtail",   required_argument, NULL, 'T' },
        { "delay",    required_argument, NULL, 'l' },
        { "count",    required_argument, NULL, 'c' },
        { "timeout",  required_argument, NULL, 'o' },
        { "interval", required_argument, NULL, 'i' },
        { "verbose",  no_argument,       NULL, 'v' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* Reset getopt for parsing from argv[1] onward */
    optind = 2;

    int opt;
    while ((opt = getopt_long(argc, argv, "d:s:D:t:T:l:c:o:i:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd':
            args->device = optarg;
            break;
        case 's':
            args->src_call = optarg;
            break;
        case 'D':
            args->dst_call = optarg;
            break;
        case 't':
            args->txdelay_ms = atoi(optarg);
            break;
        case 'T':
            args->txtail_ms = atoi(optarg);
            break;
        case 'l':
            args->delay_ms = atoi(optarg);
            break;
        case 'c':
            args->count = atoi(optarg);
            break;
        case 'o':
            args->timeout_ms = atoi(optarg);
            break;
        case 'i':
            args->interval_ms = atoi(optarg);
            break;
        case 'v':
            args->verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            return -1;
        }
    }

    /* For send mode, remaining positional arg is the payload */
    if (args->mode == CMD_MODE_SEND && optind < argc) {
        args->payload = argv[optind];
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* validate_args                                                       */
/* ------------------------------------------------------------------ */
int validate_args(const cli_args_t *args)
{
    if (!args)
        return -1;

    if (args->mode == CMD_MODE_NONE) {
        fprintf(stderr, "error: no command specified\n");
        return -1;
    }

    /* Device is always required */
    if (!args->device) {
        fprintf(stderr, "error: --device is required\n");
        return -1;
    }

    /* src/dst required for send, echo, and ping */
    if (args->mode == CMD_MODE_SEND || args->mode == CMD_MODE_ECHO ||
        args->mode == CMD_MODE_PING) {
        const char *mode_name = "unknown";
        switch (args->mode) {
        case CMD_MODE_SEND: mode_name = "send"; break;
        case CMD_MODE_ECHO: mode_name = "echo"; break;
        case CMD_MODE_PING: mode_name = "ping"; break;
        default: break;
        }
        if (!args->src_call) {
            fprintf(stderr, "error: --src is required for %s mode\n", mode_name);
            return -1;
        }
        if (!args->dst_call) {
            fprintf(stderr, "error: --dst is required for %s mode\n", mode_name);
            return -1;
        }
    }

    /* Payload required for send */
    if (args->mode == CMD_MODE_SEND && !args->payload) {
        fprintf(stderr, "error: payload argument is required for send mode\n");
        return -1;
    }

    /* Ping-specific validation */
    if (args->mode == CMD_MODE_PING) {
        if (args->count <= 0) {
            fprintf(stderr, "error: --count must be greater than 0\n");
            return -1;
        }
        if (args->timeout_ms <= 0) {
            fprintf(stderr, "error: --timeout must be greater than 0\n");
            return -1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* hex_dump — print hex dump of raw data (16 bytes per line)           */
/* ------------------------------------------------------------------ */
static void hex_dump(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && (i % 16) == 0)
            printf("\n");
        printf("  %02X", data[i]);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* cmd_send — build AX.25 frame, KISS-encode, write to serial         */
/* Requirements: 5.1, 5.2, 5.4                                        */
/* ------------------------------------------------------------------ */
int cmd_send(int fd, const char *src, const char *dst,
             const char *payload, int verbose)
{
    size_t plen = strlen(payload);

    /* Check payload size limit */
    if (plen > AX25_MAX_INFO) {
        fprintf(stderr, "error: payload too large (%zu bytes, max %d)\n",
                plen, AX25_MAX_INFO);
        return -1;
    }

    /* Build AX.25 UI frame */
    uint8_t ax25_frame[AX25_HDR_LEN + AX25_MAX_INFO];
    int frame_len = ax25_build_frame(dst, src,
                                     (const uint8_t *)payload, plen,
                                     ax25_frame, sizeof(ax25_frame));
    if (frame_len < 0) {
        fprintf(stderr, "error: failed to build AX.25 frame\n");
        return -1;
    }

    /* KISS-encode the AX.25 frame */
    uint8_t kiss_frame[3 + (size_t)frame_len * 2];
    int kiss_len = kiss_encode(ax25_frame, (size_t)frame_len,
                               kiss_frame, sizeof(kiss_frame));
    if (kiss_len < 0) {
        fprintf(stderr, "error: failed to KISS-encode frame\n");
        return -1;
    }

    if (verbose) {
        printf("Sending %zu bytes payload from %s to %s\n", plen, src, dst);
        printf("AX.25 frame: %d bytes, KISS frame: %d bytes\n",
               frame_len, kiss_len);
    }

    /* Write to serial */
    ssize_t written = write(fd, kiss_frame, (size_t)kiss_len);
    if (written < 0) {
        fprintf(stderr, "error: serial write failed: %s\n", strerror(errno));
        return -1;
    }
    if (written != kiss_len) {
        fprintf(stderr, "error: short write (%zd of %d bytes)\n",
                written, kiss_len);
        return -1;
    }

    /* Drain to ensure TNC receives complete frame */
    tcdrain(fd);

    printf("Sent %zu bytes from %s to %s\n", plen, src, dst);
    return 0;
}

/* ------------------------------------------------------------------ */
/* cmd_receive — blocking read loop, decode KISS, strip AX.25, print  */
/* Requirements: 6.1, 6.2, 6.3, 6.4                                   */
/* ------------------------------------------------------------------ */
int cmd_receive(int fd, int verbose)
{
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t read_buf[256];
    uint8_t kiss_payload[KISS_MAX_PAYLOAD];
    size_t  kiss_payload_len = 0;

    printf("Listening for packets... (Ctrl-C to stop)\n");

    while (g_running) {
        ssize_t n = read(fd, read_buf, sizeof(read_buf));
        if (n < 0) {
            if (errno == EINTR) {
                /* Signal received — check g_running and continue */
                continue;
            }
            fprintf(stderr, "error: serial read failed: %s\n", strerror(errno));
            return -1;
        }
        if (n == 0)
            continue;

        /* Feed each byte to the KISS decoder */
        for (ssize_t i = 0; i < n && g_running; i++) {
            int rc = kiss_decoder_feed(&dec, read_buf[i],
                                       kiss_payload, sizeof(kiss_payload),
                                       &kiss_payload_len);
            if (rc == 1) {
                /* Complete KISS frame decoded — strip AX.25 */
                char src_call[AX25_MAX_CALLSIGN];
                char dst_call[AX25_MAX_CALLSIGN];
                const uint8_t *info = NULL;

                int info_len = ax25_strip_frame(kiss_payload, kiss_payload_len,
                                                src_call, dst_call, &info);
                if (info_len < 0) {
                    fprintf(stderr, "warning: invalid AX.25 frame (%zu bytes), skipping\n",
                            kiss_payload_len);
                    continue;
                }

                /* Timestamp */
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                char ts[32];
                strftime(ts, sizeof(ts), "%H:%M:%S", tm);

                /* Print received packet */
                printf("[%s] %s > %s: ", ts, src_call, dst_call);
                if (info_len > 0)
                    fwrite(info, 1, (size_t)info_len, stdout);
                printf("\n");

                /* Hex dump in verbose mode */
                if (verbose) {
                    printf("  Raw frame (%zu bytes):\n", kiss_payload_len);
                    hex_dump(kiss_payload, kiss_payload_len);
                }

                fflush(stdout);
            }
            /* rc == 0: need more bytes, rc == -1: discard (logged internally) */
        }
    }

    printf("\nReceive stopped.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* cmd_echo — receive, swap callsigns, retransmit with delay           */
/* Requirements: 7.1, 7.2, 7.3, 7.4                                   */
/* ------------------------------------------------------------------ */
int cmd_echo(int fd, const char *src, const char *dst,
             int delay_ms, int verbose)
{
    (void)src;
    (void)dst;

    kiss_decoder_t dec;
    kiss_decoder_init(&dec);

    uint8_t read_buf[256];
    uint8_t kiss_payload[KISS_MAX_PAYLOAD];
    size_t  kiss_payload_len = 0;

    printf("Echo mode active (delay=%d ms)... (Ctrl-C to stop)\n", delay_ms);

    while (g_running) {
        ssize_t n = read(fd, read_buf, sizeof(read_buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "error: serial read failed: %s\n", strerror(errno));
            return -1;
        }
        if (n == 0)
            continue;

        for (ssize_t i = 0; i < n && g_running; i++) {
            int rc = kiss_decoder_feed(&dec, read_buf[i],
                                       kiss_payload, sizeof(kiss_payload),
                                       &kiss_payload_len);
            if (rc == 1) {
                /* Strip AX.25 frame */
                char orig_src[AX25_MAX_CALLSIGN];
                char orig_dst[AX25_MAX_CALLSIGN];
                const uint8_t *info = NULL;

                int info_len = ax25_strip_frame(kiss_payload, kiss_payload_len,
                                                orig_src, orig_dst, &info);
                if (info_len < 0) {
                    fprintf(stderr, "warning: invalid AX.25 frame, skipping\n");
                    continue;
                }

                /* Swap callsigns: rebuild with src=orig_dst, dst=orig_src */
                uint8_t echo_frame[AX25_HDR_LEN + AX25_MAX_INFO];
                int echo_frame_len = ax25_build_frame(
                    orig_src,   /* new destination = original source */
                    orig_dst,   /* new source = original destination */
                    info, (size_t)info_len,
                    echo_frame, sizeof(echo_frame));
                if (echo_frame_len < 0) {
                    fprintf(stderr, "warning: failed to rebuild echo frame\n");
                    continue;
                }

                /* KISS-encode */
                uint8_t echo_kiss[3 + (size_t)echo_frame_len * 2];
                int echo_kiss_len = kiss_encode(echo_frame, (size_t)echo_frame_len,
                                                echo_kiss, sizeof(echo_kiss));
                if (echo_kiss_len < 0) {
                    fprintf(stderr, "warning: failed to KISS-encode echo frame\n");
                    continue;
                }

                /* Configurable delay before retransmit */
                if (delay_ms > 0)
                    usleep((useconds_t)delay_ms * 1000);

                /* Check g_running after delay */
                if (!g_running)
                    break;

                /* Retransmit */
                ssize_t written = write(fd, echo_kiss, (size_t)echo_kiss_len);
                if (written < 0) {
                    fprintf(stderr, "error: serial write failed: %s\n",
                            strerror(errno));
                    return -1;
                }
                tcdrain(fd);

                /* Log echo info */
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                char ts[32];
                strftime(ts, sizeof(ts), "%H:%M:%S", tm);

                printf("[%s] Echo: %s -> %s (%d bytes payload)\n",
                       ts, orig_src, orig_dst, info_len);

                if (verbose) {
                    printf("  Original frame: %zu bytes, echo frame: %d bytes\n",
                           kiss_payload_len, echo_frame_len);
                }

                fflush(stdout);
            }
        }
    }

    printf("\nEcho mode stopped.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* ping_stats_t — accumulator for ping session statistics              */
/* ------------------------------------------------------------------ */
typedef struct {
    int     sent;
    int     received;
    double  rtt_min;
    double  rtt_max;
    double  rtt_sum;
} ping_stats_t;

/* ------------------------------------------------------------------ */
/* cmd_ping — send pings, wait for echo replies, measure RTT           */
/* Requirements: 2.1, 2.2, 2.3, 3.1–3.6, 4.1–4.3, 5.1–5.3,          */
/*               6.1–6.3, 7.1–7.3                                     */
/* ------------------------------------------------------------------ */
int cmd_ping(int fd, const char *src, const char *dst,
             int count, int timeout_ms, int interval_ms, int verbose)
{
    /* --- 4.1: Header and stats init --- */
    printf("PING %s from %s: %d packets\n", dst, src, count);

    ping_stats_t stats;
    stats.sent     = 0;
    stats.received = 0;
    stats.rtt_min  = 1e9;
    stats.rtt_max  = 0;
    stats.rtt_sum  = 0;

    kiss_decoder_t dec;
    uint8_t payload_buf[PING_PAYLOAD_LEN];
    uint8_t ax25_frame[AX25_HDR_LEN + AX25_MAX_INFO];
    uint8_t kiss_frame[3 + (AX25_HDR_LEN + AX25_MAX_INFO) * 2];
    uint8_t read_buf[256];
    uint8_t kiss_payload[KISS_MAX_PAYLOAD];
    size_t  kiss_payload_len = 0;

    /* --- 4.2 / 4.3: Main ping loop --- */
    for (int seq = 1; seq <= count && g_running; seq++) {
        /* Build ping payload */
        int64_t tx_us = ping_now_us();
        if (ping_build_payload((uint16_t)seq, tx_us, payload_buf, sizeof(payload_buf)) != 0) {
            fprintf(stderr, "error: failed to build ping payload\n");
            break;
        }

        /* Build AX.25 frame */
        int frame_len = ax25_build_frame(dst, src, payload_buf, PING_PAYLOAD_LEN,
                                         ax25_frame, sizeof(ax25_frame));
        if (frame_len < 0) {
            fprintf(stderr, "error: failed to build AX.25 frame\n");
            break;
        }

        /* KISS-encode */
        int kiss_len = kiss_encode(ax25_frame, (size_t)frame_len,
                                   kiss_frame, sizeof(kiss_frame));
        if (kiss_len < 0) {
            fprintf(stderr, "error: failed to KISS-encode frame\n");
            break;
        }

        /* Write to serial */
        ssize_t written = write(fd, kiss_frame, (size_t)kiss_len);
        if (written < 0) {
            if (errno == EINTR && !g_running)
                break;
            fprintf(stderr, "error: serial write failed: %s\n", strerror(errno));
            break;
        }
        tcdrain(fd);
        stats.sent++;

        /* --- 4.3: Reply wait logic with poll() --- */
        kiss_decoder_init(&dec);
        int got_reply = 0;
        int64_t wait_start = ping_now_us();
        int remaining_ms = timeout_ms;

        while (remaining_ms > 0 && g_running) {
            struct pollfd pfd;
            pfd.fd     = fd;
            pfd.events = POLLIN;

            int pret = poll(&pfd, 1, remaining_ms);
            if (pret < 0) {
                if (errno == EINTR) {
                    if (!g_running) break;
                    /* Recalculate remaining time and continue */
                    int64_t elapsed_us = ping_now_us() - wait_start;
                    remaining_ms = timeout_ms - (int)(elapsed_us / 1000);
                    if (remaining_ms <= 0) break;
                    continue;
                }
                fprintf(stderr, "error: poll failed: %s\n", strerror(errno));
                break;
            }

            if (pret == 0) {
                /* Timeout */
                break;
            }

            /* POLLIN — read available bytes */
            if (pfd.revents & POLLIN) {
                ssize_t n = read(fd, read_buf, sizeof(read_buf));
                if (n < 0) {
                    if (errno == EINTR) {
                        if (!g_running) break;
                        int64_t elapsed_us = ping_now_us() - wait_start;
                        remaining_ms = timeout_ms - (int)(elapsed_us / 1000);
                        if (remaining_ms <= 0) break;
                        continue;
                    }
                    fprintf(stderr, "error: serial read failed: %s\n", strerror(errno));
                    break;
                }

                /* Feed bytes to KISS decoder */
                for (ssize_t i = 0; i < n && !got_reply; i++) {
                    int rc = kiss_decoder_feed(&dec, read_buf[i],
                                               kiss_payload, sizeof(kiss_payload),
                                               &kiss_payload_len);
                    if (rc == 1) {
                        /* Complete KISS frame — strip AX.25 */
                        char src_call[AX25_MAX_CALLSIGN];
                        char dst_call[AX25_MAX_CALLSIGN];
                        const uint8_t *info = NULL;

                        int info_len = ax25_strip_frame(kiss_payload, kiss_payload_len,
                                                        src_call, dst_call, &info);
                        if (info_len < 0) {
                            /* Invalid AX.25 frame — discard, keep waiting */
                            continue;
                        }

                        /* Verbose hex dump */
                        if (verbose) {
                            printf("  Received frame (%zu bytes):\n", kiss_payload_len);
                            hex_dump(kiss_payload, kiss_payload_len);
                        }

                        /* Parse ping payload */
                        uint16_t reply_seq;
                        int64_t  reply_tx_us;
                        if (ping_parse_payload(info, (size_t)info_len,
                                               &reply_seq, &reply_tx_us) != 0) {
                            /* Not a ping payload — discard, keep waiting */
                            continue;
                        }

                        if (reply_seq != (uint16_t)seq) {
                            /* Sequence mismatch — discard, keep waiting */
                            continue;
                        }

                        /* Matching reply — compute RTT */
                        double rtt = (double)(ping_now_us() - tx_us) / 1000.0;
                        printf("%d bytes from %s: seq=%d time=%.3f ms\n",
                               info_len, src_call, seq, rtt);

                        /* Update stats */
                        if (rtt < stats.rtt_min) stats.rtt_min = rtt;
                        if (rtt > stats.rtt_max) stats.rtt_max = rtt;
                        stats.rtt_sum += rtt;
                        stats.received++;
                        got_reply = 1;
                    }
                    /* rc == 0: need more bytes, rc == -1: discard */
                }

                if (got_reply)
                    break;
            }

            /* Recalculate remaining timeout */
            int64_t elapsed_us = ping_now_us() - wait_start;
            remaining_ms = timeout_ms - (int)(elapsed_us / 1000);
        }

        if (!got_reply && g_running) {
            printf("Request timeout for seq %d\n", seq);
        }

        /* Inter-ping delay (skip after last ping or if interrupted) */
        if (seq < count && g_running && interval_ms > 0) {
            usleep((useconds_t)interval_ms * 1000);
        }
    }

    /* --- 4.4: Summary statistics --- */
    printf("\n--- %s ping statistics ---\n", dst);
    double loss = (stats.sent > 0)
        ? 100.0 * (1.0 - (double)stats.received / (double)stats.sent)
        : 0.0;
    printf("%d packets transmitted, %d received, %.0f%% packet loss\n",
           stats.sent, stats.received, loss);
    if (stats.received > 0) {
        double avg = stats.rtt_sum / (double)stats.received;
        printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
               stats.rtt_min, avg, stats.rtt_max);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* main — parse CLI, open serial, configure TNC, dispatch, cleanup     */
/* Requirements: 1.1, 1.2, 1.3, 8.1, 8.5                              */
/* ------------------------------------------------------------------ */
#ifndef TEST_CLI_MODE
int main(int argc, char *argv[])
{
    cli_args_t args;

    /* Parse command-line arguments */
    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    /* No subcommand — print usage and exit 0 */
    if (args.mode == CMD_MODE_NONE) {
        print_usage(argv[0]);
        return 0;
    }

    /* Validate required arguments per mode */
    if (validate_args(&args) != 0)
        return 1;

    /* Install signal handlers for clean shutdown */
    if (install_signal_handlers() != 0)
        return 1;

    /* Parse device string to extract path and baud rate */
    char device_path[256];
    int  baud = 9600;
    if (serial_parse_device(args.device, device_path, sizeof(device_path), &baud) != 0) {
        fprintf(stderr, "error: invalid device string '%s'\n", args.device);
        return 1;
    }

    /* Print parsed configuration in verbose mode */
    const char *mode_str = "unknown";
    switch (args.mode) {
    case CMD_MODE_SEND:    mode_str = "send";    break;
    case CMD_MODE_RECEIVE: mode_str = "receive"; break;
    case CMD_MODE_ECHO:    mode_str = "echo";    break;
    case CMD_MODE_PING:    mode_str = "ping";    break;
    default: break;
    }

    if (args.verbose) {
        printf("Mode:     %s\n", mode_str);
        printf("Device:   %s\n", device_path);
        printf("Baud:     %d\n", baud);
        if (args.src_call) printf("Source:   %s\n", args.src_call);
        if (args.dst_call) printf("Dest:     %s\n", args.dst_call);
        printf("TX-delay: %d ms\n", args.txdelay_ms);
        printf("TX-tail:  %d ms\n", args.txtail_ms);
        if (args.mode == CMD_MODE_ECHO)
            printf("Delay:    %d ms\n", args.delay_ms);
        if (args.mode == CMD_MODE_PING) {
            printf("Count:    %d\n", args.count);
            printf("Timeout:  %d ms\n", args.timeout_ms);
            printf("Interval: %d ms\n", args.interval_ms);
        }
        if (args.payload) printf("Payload:  %s\n", args.payload);
    }

    /* Open serial port */
    int fd = serial_open(device_path, baud);
    if (fd < 0) {
        fprintf(stderr, "error: failed to open serial port '%s' at %d baud\n",
                device_path, baud);
        return 1;
    }

    /* Configure TNC parameters (TX-delay, TX-tail) */
    if (serial_configure_tnc(fd, args.txdelay_ms, args.txtail_ms) != 0) {
        fprintf(stderr, "error: failed to configure TNC parameters\n");
        serial_close(fd);
        return 1;
    }

    /* Dispatch to mode function */
    int rc = 0;
    switch (args.mode) {
    case CMD_MODE_SEND:
        rc = cmd_send(fd, args.src_call, args.dst_call,
                      args.payload, args.verbose);
        break;
    case CMD_MODE_RECEIVE:
        rc = cmd_receive(fd, args.verbose);
        break;
    case CMD_MODE_ECHO:
        rc = cmd_echo(fd, args.src_call, args.dst_call,
                      args.delay_ms, args.verbose);
        break;
    case CMD_MODE_PING:
        rc = cmd_ping(fd, args.src_call, args.dst_call,
                      args.count, args.timeout_ms, args.interval_ms,
                      args.verbose);
        break;
    default:
        fprintf(stderr, "error: unknown mode\n");
        rc = -1;
        break;
    }

    /* Clean up */
    serial_close(fd);

    return (rc == 0) ? 0 : 1;
}
#endif /* TEST_CLI_MODE */
