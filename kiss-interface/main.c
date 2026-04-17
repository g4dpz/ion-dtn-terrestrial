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
#include "ltp.h"
#include "beacon.h"
#include "aprs.h"
#include "bp.h"
#include "cbor.h"

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
    CMD_MODE_PING,
    CMD_MODE_LTP_SEND,
    CMD_MODE_LTP_RECV,
    CMD_MODE_BEACON,
    CMD_MODE_BP_SEND,
    CMD_MODE_BP_RECV
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
    const char *local_eid;   /* DTN endpoint, e.g. "dtn://g4dpz-1" */
    const char *remote_eid;  /* Remote DTN endpoint */
    int         mtu;         /* LTP segment MTU (default 64) */
    int         owlt_ms;     /* One-way light time (default 1500) */
    int         retries;     /* Max retransmission attempts (default 7) */
    const char *beacon_callsign;  /* Beacon source callsign */
    double      beacon_lat;       /* Beacon latitude (decimal degrees) */
    double      beacon_lon;       /* Beacon longitude (decimal degrees) */
    const char *beacon_comment;   /* Beacon comment text */
    int         beacon_interval;  /* Beacon interval in seconds (default 120) */
    int         beacon_enabled;   /* --beacon flag for ltp-recv */
    const char *file_path;        /* --file for bp-send */
    const char *outdir;           /* --outdir for bp-recv */
    int         lifetime_sec;     /* --lifetime (default 3600) */
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
    printf("  ltp-send  Send a data block reliably using LTP\n");
    printf("  ltp-recv  Receive data blocks reliably using LTP\n");
    printf("  beacon    Transmit periodic APRS position beacons\n");
    printf("  bp-send   Send a BPv7 bundle over LTP\n");
    printf("  bp-recv   Receive BPv7 bundles over LTP\n");
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
    printf("  --local <eid>           Local DTN endpoint (required for ltp-send/ltp-recv)\n");
    printf("  --remote <eid>          Remote DTN endpoint (required for ltp-send)\n");
    printf("  --mtu <bytes>           LTP segment MTU (default: 64)\n");
    printf("  --owlt <ms>             One-way light time estimate (default: 1500)\n");
    printf("  --retries <n>           Max retransmission attempts (default: 7)\n");
    printf("  --callsign <call>       Beacon source callsign (required for beacon)\n");
    printf("  --lat <degrees>         Beacon latitude in decimal degrees\n");
    printf("  --lon <degrees>         Beacon longitude in decimal degrees\n");
    printf("  --comment <text>        Beacon comment (default: repo URL)\n");
    printf("  --beacon-interval <s>   Beacon interval in seconds (default: 120)\n");
    printf("  --beacon                Enable beaconing in ltp-recv mode\n");
    printf("  --file <path>           Read payload from file (bp-send)\n");
    printf("  --outdir <dir>          Write received bundles to directory (bp-recv)\n");
    printf("  --lifetime <seconds>    Bundle lifetime (default: 3600)\n");
    printf("  --verbose               Enable verbose/debug output\n");
    printf("  --help                  Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s send --device /dev/ttyACM0 --src N0CALL --dst CQ \"Hello World\"\n", prog);
    printf("  %s receive --device /dev/ttyACM0:9600 --verbose\n", prog);
    printf("  %s echo --device /dev/ttyACM0 --src N0CALL --dst CQ --delay 2000\n", prog);
    printf("  %s ping --device /dev/ttyACM0 --src N0CALL --dst CQ --count 10 --timeout 3000\n", prog);
    printf("  %s ltp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 \"Hello\"\n", prog);
    printf("  %s ltp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2 --verbose\n", prog);
    printf("  %s beacon --device /dev/ttyACM0 --callsign G4DPZ-1 --lat 52.467 --lon -2.022\n", prog);
    printf("  %s bp-send --device /dev/ttyACM0 --local dtn://g4dpz-1 --remote dtn://g4dpz-2 \"Hello BPv7\"\n", prog);
    printf("  %s bp-recv --device /dev/ttyACM0 --local dtn://g4dpz-2\n", prog);
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
    args->local_eid  = NULL;
    args->remote_eid = NULL;
    args->mtu        = 64;
    args->owlt_ms    = 1500;
    args->retries    = 7;
    args->beacon_callsign = NULL;
    args->beacon_lat = 0.0;
    args->beacon_lon = 0.0;
    args->beacon_comment = NULL;
    args->beacon_interval = 120;
    args->beacon_enabled = 0;
    args->file_path = NULL;
    args->outdir = NULL;
    args->lifetime_sec = 3600;

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
    else if (strcmp(cmd, "ltp-send") == 0)
        args->mode = CMD_MODE_LTP_SEND;
    else if (strcmp(cmd, "ltp-recv") == 0)
        args->mode = CMD_MODE_LTP_RECV;
    else if (strcmp(cmd, "beacon") == 0)
        args->mode = CMD_MODE_BEACON;
    else if (strcmp(cmd, "bp-send") == 0)
        args->mode = CMD_MODE_BP_SEND;
    else if (strcmp(cmd, "bp-recv") == 0)
        args->mode = CMD_MODE_BP_RECV;
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
        { "local",    required_argument, NULL, 'L' },
        { "remote",   required_argument, NULL, 'R' },
        { "mtu",      required_argument, NULL, 'm' },
        { "owlt",     required_argument, NULL, 'w' },
        { "retries",  required_argument, NULL, 'r' },
        { "callsign", required_argument, NULL, 'C' },
        { "lat",      required_argument, NULL, 'A' },
        { "lon",      required_argument, NULL, 'O' },
        { "comment",  required_argument, NULL, 'M' },
        { "beacon-interval", required_argument, NULL, 'B' },
        { "beacon",   no_argument,       NULL, 'b' },
        { "file",     required_argument, NULL, 'F' },
        { "outdir",   required_argument, NULL, 'P' },
        { "lifetime", required_argument, NULL, 'E' },
        { "verbose",  no_argument,       NULL, 'v' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* Reset getopt for parsing from argv[1] onward */
    optind = 2;

    int opt;
    while ((opt = getopt_long(argc, argv, "d:s:D:t:T:l:c:o:i:L:R:m:w:r:C:A:O:M:B:bF:P:E:vh", long_opts, NULL)) != -1) {
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
        case 'L':
            args->local_eid = optarg;
            break;
        case 'R':
            args->remote_eid = optarg;
            break;
        case 'm':
            args->mtu = atoi(optarg);
            break;
        case 'w':
            args->owlt_ms = atoi(optarg);
            break;
        case 'r':
            args->retries = atoi(optarg);
            break;
        case 'C':
            args->beacon_callsign = optarg;
            break;
        case 'A':
            args->beacon_lat = strtod(optarg, NULL);
            break;
        case 'O':
            args->beacon_lon = strtod(optarg, NULL);
            break;
        case 'M':
            args->beacon_comment = optarg;
            break;
        case 'B':
            args->beacon_interval = atoi(optarg);
            break;
        case 'b':
            args->beacon_enabled = 1;
            break;
        case 'F':
            args->file_path = optarg;
            break;
        case 'P':
            args->outdir = optarg;
            break;
        case 'E':
            args->lifetime_sec = atoi(optarg);
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

    /* For send and ltp-send modes, remaining positional arg is the payload */
    if ((args->mode == CMD_MODE_SEND || args->mode == CMD_MODE_LTP_SEND ||
         args->mode == CMD_MODE_BP_SEND) && optind < argc) {
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

    /* LTP-specific validation */
    if (args->mode == CMD_MODE_LTP_SEND || args->mode == CMD_MODE_LTP_RECV ||
        args->mode == CMD_MODE_BP_SEND || args->mode == CMD_MODE_BP_RECV) {
        if (!args->local_eid) {
            fprintf(stderr, "error: --local is required for %s mode\n",
                    args->mode == CMD_MODE_LTP_SEND ? "ltp-send" : "ltp-recv");
            return -1;
        }
    }
    if (args->mode == CMD_MODE_LTP_SEND) {
        if (!args->remote_eid) {
            fprintf(stderr, "error: --remote is required for ltp-send mode\n");
            return -1;
        }
        if (!args->payload) {
            fprintf(stderr, "error: payload argument is required for ltp-send mode\n");
            return -1;
        }
    }

    if (args->mode == CMD_MODE_BP_SEND) {
        if (!args->remote_eid) {
            fprintf(stderr, "error: --remote is required for bp-send mode\n");
            return -1;
        }
        if (!args->payload && !args->file_path) {
            fprintf(stderr, "error: payload or --file is required for bp-send mode\n");
            return -1;
        }
    }

    /* Beacon-specific validation */
    if (args->mode == CMD_MODE_BEACON) {
        if (!args->beacon_callsign) {
            fprintf(stderr, "error: --callsign is required for beacon mode\n");
            return -1;
        }
        if (args->beacon_lat == 0.0 && args->beacon_lon == 0.0) {
            /* Allow 0,0 but require explicit --lat and --lon */
        }
    }

    /* ltp-recv or ltp-send with --beacon requires beacon options */
    if ((args->mode == CMD_MODE_LTP_RECV || args->mode == CMD_MODE_LTP_SEND) &&
        args->beacon_enabled) {
        if (!args->beacon_callsign) {
            fprintf(stderr, "error: --callsign is required when --beacon is used\n");
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
/* cmd_ltp_send — send a data block reliably using LTP                 */
/* Requirements: 10.3, 10.4, 10.5, 10.6                               */
/* ------------------------------------------------------------------ */
int cmd_ltp_send(int fd, const char *local_eid, const char *remote_eid,
                 const char *payload, int mtu, int owlt_ms, int retries, int verbose)
{
    ltp_config_t cfg;
    cfg.segment_mtu = (uint32_t)mtu;
    cfg.owlt_ms = (uint32_t)owlt_ms;
    cfg.max_retries = (uint32_t)retries;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.verbose = verbose;

    ltp_engine_t eng;
    if (ltp_engine_init(&eng, local_eid, &cfg) != 0) {
        fprintf(stderr, "error: failed to initialize LTP engine\n");
        return -1;
    }

    size_t plen = strlen(payload);
    if (plen > LTP_MAX_BLOCK_SIZE) {
        fprintf(stderr, "error: payload too large (%zu bytes, max %d)\n",
                plen, LTP_MAX_BLOCK_SIZE);
        return -1;
    }

    printf("LTP send to %s: %zu bytes, MTU=%d, OWLT=%dms\n",
           remote_eid, plen, mtu, owlt_ms);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (ltp_send_block(&eng, fd, remote_eid,
                       (const uint8_t *)payload, (uint32_t)plen) != 0) {
        fprintf(stderr, "error: failed to send block\n");
        return -1;
    }

    int rc = ltp_engine_run(&eng, fd, 1); /* send mode */

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    if (rc == 0) {
        printf("Block delivered: %zu bytes, %u segments, %.3f seconds\n",
               plen, eng.segments_sent, elapsed);
    } else {
        fprintf(stderr, "error: transfer failed (%u segments sent, %u cancelled)\n",
                eng.segments_sent, eng.sessions_cancelled);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* ltp_recv_callback — print received block with timestamp             */
/* ------------------------------------------------------------------ */
static void ltp_recv_callback(const uint8_t *data, uint32_t len,
                               uint64_t remote_engine_id, void *ctx)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    /* Try to resolve engine ID to DTN endpoint */
    const char *eid = NULL;
    if (ctx) eid = ltp_engine_id_to_eid((const ltp_engine_t *)ctx, remote_engine_id);

    if (eid)
        printf("[%s] Block from %s: ", ts, eid);
    else
        printf("[%s] Block from engine %lu: ", ts, (unsigned long)remote_engine_id);

    fwrite(data, 1, len, stdout);
    printf("\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* cmd_ltp_recv — receive data blocks reliably using LTP               */
/* Requirements: 11.3, 11.4, 11.5, 11.6                               */
/* ------------------------------------------------------------------ */
int cmd_ltp_recv(int fd, const char *local_eid,
                 int mtu, int owlt_ms, int retries, int verbose)
{
    /* Note: beacon integration handled by caller checking beacon_enabled
     * and running a custom event loop. This function is the non-beacon path. */
    ltp_config_t cfg;
    cfg.segment_mtu = (uint32_t)mtu;
    cfg.owlt_ms = (uint32_t)owlt_ms;
    cfg.max_retries = (uint32_t)retries;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.verbose = verbose;

    ltp_engine_t eng;
    if (ltp_engine_init(&eng, local_eid, &cfg) != 0) {
        fprintf(stderr, "error: failed to initialize LTP engine\n");
        return -1;
    }

    eng.on_block_received = ltp_recv_callback;
    eng.cb_ctx = &eng;

    printf("LTP recv on %s (MTU=%d, OWLT=%dms)... (Ctrl-C to stop)\n",
           local_eid, mtu, owlt_ms);

    int rc = ltp_engine_run(&eng, fd, 0); /* recv mode */

    printf("\nLTP recv stopped: %u blocks delivered, %u sessions cancelled\n",
           eng.blocks_delivered, eng.sessions_cancelled);
    return rc;
}

/* ------------------------------------------------------------------ */
/* cmd_beacon — standalone periodic APRS beacon mode                   */
/* ------------------------------------------------------------------ */
int cmd_beacon(int fd, const char *callsign, double lat, double lon,
               const char *comment, int interval_sec, int verbose)
{
    (void)verbose;

    const char *cmt = (comment && comment[0] != '\0') ? comment : BEACON_DEFAULT_COMMENT;

    beacon_state_t beacon;
    if (beacon_init(&beacon, callsign, lat, lon, cmt, interval_sec) != 0) {
        fprintf(stderr, "error: failed to initialize beacon\n");
        return -1;
    }

    printf("Beacon mode: %s every %d seconds (Ctrl-C to stop)\n",
           callsign, interval_sec);

    /* Transmit initial beacon immediately */
    if (beacon_transmit(&beacon, fd) != 0) {
        fprintf(stderr, "error: initial beacon transmit failed\n");
        return -1;
    }

    /* Beacon loop */
    while (g_running) {
        int timeout = beacon_get_timeout_ms(&beacon);
        if (timeout < 0) timeout = interval_sec * 1000;

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        int pret = poll(&pfd, 1, timeout);
        if (pret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        /* Ignore any incoming data in standalone beacon mode */

        if (beacon_is_due(&beacon) == 1) {
            beacon_transmit(&beacon, fd);
        }
    }

    printf("\nBeacon stopped.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* bp_recv_callback — decode and display received bundles              */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *outdir;
    int verbose;
    uint32_t bundles_received;
    bp_reassembly_t reasm;
} bp_recv_ctx_t;

static void bp_recv_block_cb(const uint8_t *data, uint32_t len,
                              uint64_t remote_engine_id, void *ctx)
{
    (void)remote_engine_id;
    bp_recv_ctx_t *rc = (bp_recv_ctx_t *)ctx;
    if (!rc) return;

    bp_bundle_t b;
    if (bp_decode_bundle(data, len, &b) < 0) {
        fprintf(stderr, "warning: received LTP block is not a valid bundle\n");
        return;
    }

    /* Handle fragments */
    if (b.primary.flags & BP_FLAG_FRAGMENT) {
        int result = bp_reassembly_add(&rc->reasm, &b);
        if (result == 0) {
            printf("  Fragment: offset=%lu, len=%zu, total=%lu\n",
                   (unsigned long)b.primary.fragment_offset,
                   b.payload_len,
                   (unsigned long)b.primary.total_adu_len);
            return; /* More fragments needed */
        }
        if (result < 0) {
            fprintf(stderr, "warning: fragment reassembly error\n");
            return;
        }
        /* Complete — use reassembled data */
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", tm);

        printf("[%s] Bundle from %s: %lu bytes (reassembled from fragments)\n",
               ts, b.primary.src.uri, (unsigned long)rc->reasm.total_adu_len);

        if (rc->outdir) {
            char fname[256];
            snprintf(fname, sizeof(fname), "%s/%ld_%s.bin",
                     rc->outdir, (long)time(NULL), b.primary.src.uri + 6);
            /* Sanitize filename */
            for (char *p = fname + strlen(rc->outdir) + 1; *p; p++)
                if (*p == '/' || *p == ':') *p = '_';
            FILE *f = fopen(fname, "wb");
            if (f) {
                fwrite(rc->reasm.data, 1, (size_t)rc->reasm.total_adu_len, f);
                fclose(f);
                printf("  Saved to %s\n", fname);
            }
        } else {
            fwrite(rc->reasm.data, 1, (size_t)rc->reasm.total_adu_len, stdout);
            printf("\n");
        }

        rc->bundles_received++;
        bp_reassembly_init(&rc->reasm); /* Reset for next bundle */
        fflush(stdout);
        return;
    }

    /* Non-fragment bundle */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    printf("[%s] Bundle from %s: ", ts, b.primary.src.uri);

    if (rc->outdir) {
        char fname[256];
        snprintf(fname, sizeof(fname), "%s/%ld_%s.bin",
                 rc->outdir, (long)time(NULL), b.primary.src.uri + 6);
        for (char *p = fname + strlen(rc->outdir) + 1; *p; p++)
            if (*p == '/' || *p == ':') *p = '_';
        FILE *f = fopen(fname, "wb");
        if (f) {
            fwrite(b.payload, 1, b.payload_len, f);
            fclose(f);
            printf("%zu bytes saved to %s\n", b.payload_len, fname);
        }
    } else {
        fwrite(b.payload, 1, b.payload_len, stdout);
        printf("\n");
    }

    if (rc->verbose) {
        printf("  CBOR (%u bytes):", len);
        for (uint32_t i = 0; i < len && i < 64; i++) printf(" %02X", data[i]);
        if (len > 64) printf(" ...");
        printf("\n");
    }

    rc->bundles_received++;
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* cmd_bp_send — send a BPv7 bundle over LTP                          */
/* ------------------------------------------------------------------ */
int cmd_bp_send(int fd, const char *local_eid, const char *remote_eid,
                const uint8_t *payload, size_t payload_len,
                int lifetime_sec, int mtu, int owlt_ms, int retries, int verbose)
{
    uint64_t lifetime_ms = (uint64_t)lifetime_sec * 1000;
    static uint64_t bp_seq = 0;
    bp_seq++;

    bp_eid_t src, dst;
    snprintf(src.uri, sizeof(src.uri), "%s", local_eid);
    snprintf(dst.uri, sizeof(dst.uri), "%s", remote_eid);

    /* Try encoding as a single bundle first */
    uint8_t bundle_buf[BP_MAX_BUNDLE_BUF];
    int enc = bp_encode_bundle(&src, &dst, payload, payload_len,
                               lifetime_ms, bp_seq, bundle_buf, sizeof(bundle_buf));

    ltp_config_t cfg;
    cfg.segment_mtu = (uint32_t)mtu;
    cfg.owlt_ms = (uint32_t)owlt_ms;
    cfg.max_retries = (uint32_t)retries;
    cfg.max_block_size = LTP_MAX_BLOCK_SIZE;
    cfg.verbose = verbose;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (enc > 0) {
        /* Single bundle fits */
        printf("BP send to %s: %zu bytes, 1 bundle\n", remote_eid, payload_len);

        ltp_engine_t eng;
        ltp_engine_init(&eng, local_eid, &cfg);
        if (ltp_send_block(&eng, fd, remote_eid,
                           bundle_buf, (uint32_t)enc) != 0) {
            fprintf(stderr, "error: LTP send failed\n");
            return -1;
        }
        int rc = ltp_engine_run(&eng, fd, 1);

        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        if (rc == 0)
            printf("Bundle delivered: %zu bytes, %.3f seconds\n", payload_len, elapsed);
        else
            fprintf(stderr, "error: bundle delivery failed\n");
        return rc;
    }

    /* Need fragmentation */
    int nfrags = bp_fragment_count(payload_len, BP_DEFAULT_FRAGMENT_SIZE);
    if (nfrags < 0 || nfrags > BP_MAX_FRAGMENTS) {
        fprintf(stderr, "error: payload too large for fragmentation\n");
        return -1;
    }

    printf("BP send to %s: %zu bytes, %d fragments\n", remote_eid, payload_len, nfrags);

    ltp_engine_t eng;
    ltp_engine_init(&eng, local_eid, &cfg);

    for (int f = 0; f < nfrags; f++) {
        uint64_t off = (uint64_t)f * BP_DEFAULT_FRAGMENT_SIZE;
        size_t flen = payload_len - (size_t)off;
        if (flen > BP_DEFAULT_FRAGMENT_SIZE) flen = BP_DEFAULT_FRAGMENT_SIZE;

        uint8_t fbuf[BP_MAX_BUNDLE_BUF];
        int fenc = bp_encode_fragment(&src, &dst, payload + off, flen,
                                      lifetime_ms, bp_seq, off, (uint64_t)payload_len,
                                      fbuf, sizeof(fbuf));
        if (fenc < 0) {
            fprintf(stderr, "error: fragment %d encode failed\n", f);
            return -1;
        }

        printf("  Fragment %d/%d: offset=%lu, len=%zu\n", f + 1, nfrags,
               (unsigned long)off, flen);

        if (ltp_send_block(&eng, fd, remote_eid, fbuf, (uint32_t)fenc) != 0) {
            fprintf(stderr, "error: LTP send failed for fragment %d\n", f);
            return -1;
        }
        if (ltp_engine_run(&eng, fd, 1) != 0) {
            fprintf(stderr, "error: fragment %d delivery failed\n", f);
            return -1;
        }
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Bundle delivered: %zu bytes, %d fragments, %.3f seconds\n",
           payload_len, nfrags, elapsed);
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
    case CMD_MODE_SEND:     mode_str = "send";     break;
    case CMD_MODE_RECEIVE:  mode_str = "receive";  break;
    case CMD_MODE_ECHO:     mode_str = "echo";     break;
    case CMD_MODE_PING:     mode_str = "ping";     break;
    case CMD_MODE_LTP_SEND: mode_str = "ltp-send"; break;
    case CMD_MODE_LTP_RECV: mode_str = "ltp-recv"; break;
    case CMD_MODE_BEACON:   mode_str = "beacon";   break;
    case CMD_MODE_BP_SEND:  mode_str = "bp-send";  break;
    case CMD_MODE_BP_RECV:  mode_str = "bp-recv";  break;
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
        if (args.mode == CMD_MODE_LTP_SEND || args.mode == CMD_MODE_LTP_RECV) {
            if (args.local_eid) printf("Local:    %s\n", args.local_eid);
            if (args.remote_eid) printf("Remote:   %s\n", args.remote_eid);
            printf("MTU:      %d\n", args.mtu);
            printf("OWLT:     %d ms\n", args.owlt_ms);
            printf("Retries:  %d\n", args.retries);
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
    case CMD_MODE_LTP_SEND:
        if (args.beacon_enabled) {
            /* LTP send with beacon — transmit beacon before and after transfer */
            const char *scmt = (args.beacon_comment && args.beacon_comment[0])
                               ? args.beacon_comment : BEACON_DEFAULT_COMMENT;
            beacon_state_t sbcn;
            if (beacon_init(&sbcn, args.beacon_callsign,
                            args.beacon_lat, args.beacon_lon,
                            scmt, args.beacon_interval) != 0) {
                fprintf(stderr, "error: failed to initialize beacon\n");
                serial_close(fd);
                return 1;
            }

            /* Beacon before sending */
            beacon_transmit(&sbcn, fd);

            /* Custom send with beacon-aware event loop */
            ltp_config_t scfg;
            scfg.segment_mtu = (uint32_t)args.mtu;
            scfg.owlt_ms = (uint32_t)args.owlt_ms;
            scfg.max_retries = (uint32_t)args.retries;
            scfg.max_block_size = LTP_MAX_BLOCK_SIZE;
            scfg.verbose = args.verbose;

            ltp_engine_t seng;
            if (ltp_engine_init(&seng, args.local_eid, &scfg) != 0) {
                fprintf(stderr, "error: failed to initialize LTP engine\n");
                serial_close(fd);
                return 1;
            }

            size_t splen = strlen(args.payload);
            printf("LTP send to %s with beacon: %zu bytes\n",
                   args.remote_eid, splen);

            struct timespec sstart;
            clock_gettime(CLOCK_MONOTONIC, &sstart);

            if (ltp_send_block(&seng, fd, args.remote_eid,
                               (const uint8_t *)args.payload,
                               (uint32_t)splen) != 0) {
                fprintf(stderr, "error: failed to send block\n");
                serial_close(fd);
                return 1;
            }

            uint64_t ssess = seng.next_session_number - 1;
            kiss_decoder_t sdec;
            kiss_decoder_init(&sdec);
            uint8_t srbuf[256], skpay[KISS_MAX_PAYLOAD];
            size_t sklen = 0;

            while (g_running) {
                int slt = ltp_get_next_timeout_ms(&seng);
                int sbt = beacon_get_timeout_ms(&sbcn);
                int stout = sbt;
                if (slt >= 0 && (stout < 0 || slt < stout)) stout = slt;
                if (stout < 0) stout = 1000;

                struct pollfd spf;
                spf.fd = fd; spf.events = POLLIN;
                int spr = poll(&spf, 1, stout);
                if (spr < 0) { if (errno == EINTR) continue; break; }

                if (spf.revents & POLLIN) {
                    ssize_t sn = read(fd, srbuf, sizeof(srbuf));
                    if (sn > 0) {
                        for (ssize_t si = 0; si < sn; si++) {
                            if (kiss_decoder_feed(&sdec, srbuf[si], skpay,
                                                  sizeof(skpay), &sklen) == 1) {
                                if (aprs_is_ax25_frame(skpay, sklen))
                                    aprs_log_packet(skpay, sklen, args.verbose);
                                else
                                    ltp_process_segment(&seng, fd, skpay, sklen);
                            }
                        }
                    }
                }

                ltp_fire_expired_timers(&seng, fd);

                if (beacon_is_due(&sbcn) == 1)
                    beacon_transmit(&sbcn, fd);

                /* Check session completion */
                for (int si = 0; si < LTP_MAX_EXPORT_SESSIONS; si++) {
                    if (seng.export_sessions[si].session_number == ssess) {
                        if (seng.export_sessions[si].completed) {
                            struct timespec send;
                            clock_gettime(CLOCK_MONOTONIC, &send);
                            double elapsed = (send.tv_sec - sstart.tv_sec) +
                                             (send.tv_nsec - sstart.tv_nsec) / 1e9;
                            printf("Block delivered: %zu bytes, %.3f seconds\n",
                                   splen, elapsed);
                            beacon_transmit(&sbcn, fd);
                            rc = 0;
                            goto ltp_send_done;
                        }
                        if (seng.export_sessions[si].cancelled) {
                            fprintf(stderr, "error: transfer cancelled\n");
                            rc = -1;
                            goto ltp_send_done;
                        }
                        break;
                    }
                }
            }
            ltp_send_done:
            (void)0;
        } else {
            rc = cmd_ltp_send(fd, args.local_eid, args.remote_eid,
                              args.payload, args.mtu, args.owlt_ms,
                              args.retries, args.verbose);
        }
        break;
    case CMD_MODE_LTP_RECV:
        if (args.beacon_enabled) {
            /* LTP recv with beacon integration */
            ltp_config_t lcfg;
            lcfg.segment_mtu = (uint32_t)args.mtu;
            lcfg.owlt_ms = (uint32_t)args.owlt_ms;
            lcfg.max_retries = (uint32_t)args.retries;
            lcfg.max_block_size = LTP_MAX_BLOCK_SIZE;
            lcfg.verbose = args.verbose;

            ltp_engine_t eng;
            if (ltp_engine_init(&eng, args.local_eid, &lcfg) != 0) {
                fprintf(stderr, "error: failed to initialize LTP engine\n");
                serial_close(fd);
                return 1;
            }
            eng.on_block_received = ltp_recv_callback;

            const char *bcmt = (args.beacon_comment && args.beacon_comment[0])
                               ? args.beacon_comment : BEACON_DEFAULT_COMMENT;
            beacon_state_t bcn;
            if (beacon_init(&bcn, args.beacon_callsign,
                            args.beacon_lat, args.beacon_lon,
                            bcmt, args.beacon_interval) != 0) {
                fprintf(stderr, "error: failed to initialize beacon\n");
                serial_close(fd);
                return 1;
            }

            printf("LTP recv on %s with beacon %s every %ds (Ctrl-C to stop)\n",
                   args.local_eid, args.beacon_callsign, args.beacon_interval);
            beacon_transmit(&bcn, fd);

            kiss_decoder_t bdec;
            kiss_decoder_init(&bdec);
            uint8_t brbuf[256];
            uint8_t bkpay[KISS_MAX_PAYLOAD];
            size_t bklen = 0;

            while (g_running) {
                int lt = ltp_get_next_timeout_ms(&eng);
                int bt = beacon_get_timeout_ms(&bcn);
                int tout = bt;
                if (lt >= 0 && (tout < 0 || lt < tout)) tout = lt;
                if (tout < 0) tout = 1000;

                struct pollfd pf;
                pf.fd = fd; pf.events = POLLIN;
                int pr = poll(&pf, 1, tout);
                if (pr < 0) { if (errno == EINTR) continue; break; }

                if (pf.revents & POLLIN) {
                    ssize_t n = read(fd, brbuf, sizeof(brbuf));
                    if (n > 0) {
                        for (ssize_t i = 0; i < n; i++) {
                            if (kiss_decoder_feed(&bdec, brbuf[i], bkpay,
                                                  sizeof(bkpay), &bklen) == 1) {
                                if (aprs_is_ax25_frame(bkpay, bklen))
                                    aprs_log_packet(bkpay, bklen, args.verbose);
                                else
                                    ltp_process_segment(&eng, fd, bkpay, bklen);
                            }
                        }
                    }
                }

                ltp_fire_expired_timers(&eng, fd);

                if (beacon_is_due(&bcn) == 1)
                    beacon_transmit(&bcn, fd);
            }

            printf("\nLTP recv stopped: %u blocks, beacon %s\n",
                   eng.blocks_delivered, args.beacon_callsign);
            rc = 0;
        } else {
            rc = cmd_ltp_recv(fd, args.local_eid,
                              args.mtu, args.owlt_ms,
                              args.retries, args.verbose);
        }
        break;
    case CMD_MODE_BEACON:
        rc = cmd_beacon(fd, args.beacon_callsign,
                        args.beacon_lat, args.beacon_lon,
                        args.beacon_comment, args.beacon_interval,
                        args.verbose);
        break;
    case CMD_MODE_BP_SEND:
        {
            const uint8_t *bp_payload = NULL;
            size_t bp_plen = 0;
            uint8_t *file_data = NULL;

            if (args.file_path) {
                FILE *f = fopen(args.file_path, "rb");
                if (!f) {
                    fprintf(stderr, "error: cannot open file '%s'\n", args.file_path);
                    serial_close(fd);
                    return 1;
                }
                fseek(f, 0, SEEK_END);
                long fsize = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (fsize <= 0 || fsize > BP_MAX_PAYLOAD) {
                    fprintf(stderr, "error: file too large (%ld bytes, max %d)\n",
                            fsize, BP_MAX_PAYLOAD);
                    fclose(f);
                    serial_close(fd);
                    return 1;
                }
                file_data = (uint8_t *)malloc((size_t)fsize);
                if (!file_data) { fclose(f); serial_close(fd); return 1; }
                if (fread(file_data, 1, (size_t)fsize, f) != (size_t)fsize) {
                    fprintf(stderr, "error: failed to read file\n");
                    free(file_data); fclose(f); serial_close(fd); return 1;
                }
                fclose(f);
                bp_payload = file_data;
                bp_plen = (size_t)fsize;
            } else {
                bp_payload = (const uint8_t *)args.payload;
                bp_plen = strlen(args.payload);
            }

            rc = cmd_bp_send(fd, args.local_eid, args.remote_eid,
                             bp_payload, bp_plen,
                             args.lifetime_sec, args.mtu, args.owlt_ms,
                             args.retries, args.verbose);
            if (file_data) free(file_data);
        }
        break;
    case CMD_MODE_BP_RECV:
        {
            ltp_config_t bpcfg;
            bpcfg.segment_mtu = (uint32_t)args.mtu;
            bpcfg.owlt_ms = (uint32_t)args.owlt_ms;
            bpcfg.max_retries = (uint32_t)args.retries;
            bpcfg.max_block_size = LTP_MAX_BLOCK_SIZE;
            bpcfg.verbose = args.verbose;

            ltp_engine_t bpeng;
            ltp_engine_init(&bpeng, args.local_eid, &bpcfg);

            bp_recv_ctx_t bpctx;
            memset(&bpctx, 0, sizeof(bpctx));
            bpctx.outdir = args.outdir;
            bpctx.verbose = args.verbose;
            bp_reassembly_init(&bpctx.reasm);

            bpeng.on_block_received = bp_recv_block_cb;
            bpeng.cb_ctx = &bpctx;

            printf("BP recv on %s... (Ctrl-C to stop)\n", args.local_eid);
            rc = ltp_engine_run(&bpeng, fd, 0);
            printf("\nBP recv stopped: %u bundles received\n", bpctx.bundles_received);
        }
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
