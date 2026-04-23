/*
 * test_cli.c — Unit tests for CLI argument parsing and validation
 *
 * Tests parse_args and validate_args from main.c.
 * Compiled with -DTEST_CLI_MODE to exclude main()'s main().
 *
 * Requirements tested: 8.1, 8.2, 8.3, 8.4, 8.6
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* Forward declarations for types and functions from main.c */
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
    CMD_MODE_BP_RECV,
    CMD_MODE_SDR_BEACON,
    CMD_MODE_SDR_RECV
} cmd_mode_t;

typedef struct {
    const char *device;
    int         baud;
    const char *src_call;
    const char *dst_call;
    const char *payload;
    int         txdelay_ms;
    int         txtail_ms;
    int         delay_ms;
    int         verbose;
    int         count;
    int         timeout_ms;
    int         interval_ms;
    const char *local_eid;
    const char *remote_eid;
    int         mtu;
    int         owlt_ms;
    int         retries;
    const char *beacon_callsign;
    double      beacon_lat;
    double      beacon_lon;
    const char *beacon_comment;
    int         beacon_interval;
    int         beacon_enabled;
    const char *file_path;
    const char *outdir;
    int         lifetime_sec;
    double      sdr_freq;
    int         sdr_gain;
    int         sdr_sample_rate;
    double      sdr_deviation;
    cmd_mode_t  mode;
} cli_args_t;

extern int  parse_args(int argc, char *argv[], cli_args_t *args);
extern int  validate_args(const cli_args_t *args);
extern void print_usage(const char *prog);

/* ================================================================== */
/* Test infrastructure                                                 */
/* ================================================================== */

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else { printf("[FAIL]\n"); } \
} while (0)

/* ================================================================== */
/* Test: parses send/receive/echo subcommands (Req 8.1)                */
/* ================================================================== */

static int test_parse_send_subcommand(void)
{
    cli_args_t args;
    optind = 1;  /* Reset getopt state */
    char *argv[] = { "kiss_interface", "send", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", "Hello", NULL };
    int argc = 9;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for send\n");
        return 0;
    }
    if (args.mode != CMD_MODE_SEND) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_SEND (%d)\n",
               args.mode, CMD_MODE_SEND);
        return 0;
    }
    return 1;
}

static int test_parse_receive_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "receive", "--device", "/dev/ttyUSB0", NULL };
    int argc = 4;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for receive\n");
        return 0;
    }
    if (args.mode != CMD_MODE_RECEIVE) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_RECEIVE (%d)\n",
               args.mode, CMD_MODE_RECEIVE);
        return 0;
    }
    return 1;
}

static int test_parse_echo_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "echo", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", NULL };
    int argc = 8;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for echo\n");
        return 0;
    }
    if (args.mode != CMD_MODE_ECHO) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_ECHO (%d)\n",
               args.mode, CMD_MODE_ECHO);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: parses --device, --src, --dst, --verbose (Req 8.2)            */
/* ================================================================== */

static int test_parse_common_options(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "send", "--device", "/dev/ttyACM0",
                     "--src", "G4DPZ-1", "--dst", "G4DPZ-2",
                     "--verbose", "test payload", NULL };
    int argc = 10;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (strcmp(args.device, "/dev/ttyACM0") != 0) {
        printf("\n    FAIL: device = '%s', expected '/dev/ttyACM0'\n", args.device);
        return 0;
    }
    if (strcmp(args.src_call, "G4DPZ-1") != 0) {
        printf("\n    FAIL: src = '%s', expected 'G4DPZ-1'\n", args.src_call);
        return 0;
    }
    if (strcmp(args.dst_call, "G4DPZ-2") != 0) {
        printf("\n    FAIL: dst = '%s', expected 'G4DPZ-2'\n", args.dst_call);
        return 0;
    }
    if (!args.verbose) {
        printf("\n    FAIL: verbose not set\n");
        return 0;
    }
    if (strcmp(args.payload, "test payload") != 0) {
        printf("\n    FAIL: payload = '%s', expected 'test payload'\n", args.payload);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: parses --txdelay, --txtail (Req 8.3)                          */
/* ================================================================== */

static int test_parse_txdelay_txtail(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "send", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ",
                     "--txdelay", "250", "--txtail", "150",
                     "payload", NULL };
    int argc = 13;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.txdelay_ms != 250) {
        printf("\n    FAIL: txdelay = %d, expected 250\n", args.txdelay_ms);
        return 0;
    }
    if (args.txtail_ms != 150) {
        printf("\n    FAIL: txtail = %d, expected 150\n", args.txtail_ms);
        return 0;
    }
    return 1;
}

/* Test default values for txdelay and txtail */
static int test_parse_txdelay_txtail_defaults(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "send", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", "payload", NULL };
    int argc = 9;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.txdelay_ms != 500) {
        printf("\n    FAIL: txdelay default = %d, expected 500\n", args.txdelay_ms);
        return 0;
    }
    if (args.txtail_ms != 300) {
        printf("\n    FAIL: txtail default = %d, expected 300\n", args.txtail_ms);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: parses echo --delay (Req 8.4)                                 */
/* ================================================================== */

static int test_parse_echo_delay(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "echo", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ",
                     "--delay", "2000", NULL };
    int argc = 10;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.delay_ms != 2000) {
        printf("\n    FAIL: delay = %d, expected 2000\n", args.delay_ms);
        return 0;
    }
    return 1;
}

/* Test default delay value */
static int test_parse_echo_delay_default(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "echo", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", NULL };
    int argc = 8;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.delay_ms != 1000) {
        printf("\n    FAIL: delay default = %d, expected 1000\n", args.delay_ms);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: missing required arg — validate_args (Req 8.6)                */
/* ================================================================== */

static int test_validate_missing_device(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_SEND;
    args.src_call = "N0CALL";
    args.dst_call = "CQ";
    args.payload = "test";
    /* device is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing device\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_src_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_SEND;
    args.device = "/dev/ttyUSB0";
    args.dst_call = "CQ";
    args.payload = "test";
    /* src_call is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing src in send\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_dst_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_SEND;
    args.device = "/dev/ttyUSB0";
    args.src_call = "N0CALL";
    args.payload = "test";
    /* dst_call is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing dst in send\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_payload_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_SEND;
    args.device = "/dev/ttyUSB0";
    args.src_call = "N0CALL";
    args.dst_call = "CQ";
    /* payload is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing payload in send\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_src_echo(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_ECHO;
    args.device = "/dev/ttyUSB0";
    args.dst_call = "CQ";
    /* src_call is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing src in echo\n");
        return 0;
    }
    return 1;
}

static int test_validate_receive_no_callsigns(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_RECEIVE;
    args.device = "/dev/ttyUSB0";
    /* No src/dst needed for receive */

    if (validate_args(&args) != 0) {
        printf("\n    FAIL: validate_args should pass for receive without callsigns\n");
        return 0;
    }
    return 1;
}

static int test_validate_mode_none(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_NONE;

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with MODE_NONE\n");
        return 0;
    }
    return 1;
}

/* Test: valid complete args pass validation */
static int test_validate_valid_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_SEND;
    args.device = "/dev/ttyUSB0";
    args.src_call = "N0CALL";
    args.dst_call = "CQ";
    args.payload = "Hello World";

    if (validate_args(&args) != 0) {
        printf("\n    FAIL: validate_args should pass for valid send args\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: ping subcommand and options (Req 1.1, 1.2, 1.3, 1.4, 1.5)    */
/* ================================================================== */

static int test_parse_ping_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ping", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", NULL };
    int argc = 8;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for ping\n");
        return 0;
    }
    if (args.mode != CMD_MODE_PING) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_PING (%d)\n",
               args.mode, CMD_MODE_PING);
        return 0;
    }
    return 1;
}

static int test_parse_ping_options(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ping", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ",
                     "--count", "10", "--timeout", "3000",
                     "--interval", "500", NULL };
    int argc = 14;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.count != 10) {
        printf("\n    FAIL: count = %d, expected 10\n", args.count);
        return 0;
    }
    if (args.timeout_ms != 3000) {
        printf("\n    FAIL: timeout_ms = %d, expected 3000\n", args.timeout_ms);
        return 0;
    }
    if (args.interval_ms != 500) {
        printf("\n    FAIL: interval_ms = %d, expected 500\n", args.interval_ms);
        return 0;
    }
    return 1;
}

static int test_parse_ping_defaults(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ping", "--device", "/dev/ttyUSB0",
                     "--src", "N0CALL", "--dst", "CQ", NULL };
    int argc = 8;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.count != 4) {
        printf("\n    FAIL: count default = %d, expected 4\n", args.count);
        return 0;
    }
    if (args.timeout_ms != 5000) {
        printf("\n    FAIL: timeout_ms default = %d, expected 5000\n", args.timeout_ms);
        return 0;
    }
    if (args.interval_ms != 1000) {
        printf("\n    FAIL: interval_ms default = %d, expected 1000\n", args.interval_ms);
        return 0;
    }
    return 1;
}

static int test_validate_missing_device_ping(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_PING;
    args.src_call = "N0CALL";
    args.dst_call = "CQ";
    args.count = 4;
    args.timeout_ms = 5000;
    /* device is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing device for ping\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_src_ping(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_PING;
    args.device = "/dev/ttyUSB0";
    args.dst_call = "CQ";
    args.count = 4;
    args.timeout_ms = 5000;
    /* src_call is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing src for ping\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_dst_ping(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_PING;
    args.device = "/dev/ttyUSB0";
    args.src_call = "N0CALL";
    args.count = 4;
    args.timeout_ms = 5000;
    /* dst_call is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing dst for ping\n");
        return 0;
    }
    return 1;
}

/* Test: unknown subcommand returns error */
static int test_parse_unknown_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "foobar", NULL };
    int argc = 2;

    if (parse_args(argc, argv, &args) == 0) {
        printf("\n    FAIL: parse_args should fail for unknown subcommand\n");
        return 0;
    }
    return 1;
}

/* Test: no arguments yields MODE_NONE */
static int test_parse_no_args(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", NULL };
    int argc = 1;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args should succeed with no args\n");
        return 0;
    }
    if (args.mode != CMD_MODE_NONE) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_NONE\n", args.mode);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: ltp-send subcommand parsing (Req 10.1, 10.2)                  */
/* ================================================================== */

static int test_parse_ltp_send_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ltp-send", "--device", "/dev/ttyACM0",
                     "--local", "dtn://g4dpz-1", "--remote", "dtn://g4dpz-2",
                     "--mtu", "128", "--owlt", "2000", "--retries", "5",
                     "Hello LTP", NULL };
    int argc = 15;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for ltp-send\n");
        return 0;
    }
    if (args.mode != CMD_MODE_LTP_SEND) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_LTP_SEND (%d)\n",
               args.mode, CMD_MODE_LTP_SEND);
        return 0;
    }
    if (strcmp(args.local_eid, "dtn://g4dpz-1") != 0) {
        printf("\n    FAIL: local_eid = '%s'\n", args.local_eid);
        return 0;
    }
    if (strcmp(args.remote_eid, "dtn://g4dpz-2") != 0) {
        printf("\n    FAIL: remote_eid = '%s'\n", args.remote_eid);
        return 0;
    }
    if (args.mtu != 128) {
        printf("\n    FAIL: mtu = %d, expected 128\n", args.mtu);
        return 0;
    }
    if (args.owlt_ms != 2000) {
        printf("\n    FAIL: owlt_ms = %d, expected 2000\n", args.owlt_ms);
        return 0;
    }
    if (args.retries != 5) {
        printf("\n    FAIL: retries = %d, expected 5\n", args.retries);
        return 0;
    }
    if (strcmp(args.payload, "Hello LTP") != 0) {
        printf("\n    FAIL: payload = '%s'\n", args.payload);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: ltp-recv subcommand parsing (Req 11.1, 11.2)                  */
/* ================================================================== */

static int test_parse_ltp_recv_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ltp-recv", "--device", "/dev/ttyACM0",
                     "--local", "dtn://g4dpz-2", "--verbose", NULL };
    int argc = 7;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for ltp-recv\n");
        return 0;
    }
    if (args.mode != CMD_MODE_LTP_RECV) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_LTP_RECV (%d)\n",
               args.mode, CMD_MODE_LTP_RECV);
        return 0;
    }
    if (strcmp(args.local_eid, "dtn://g4dpz-2") != 0) {
        printf("\n    FAIL: local_eid = '%s'\n", args.local_eid);
        return 0;
    }
    if (!args.verbose) {
        printf("\n    FAIL: verbose not set\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: LTP defaults (Req 10.2, 11.2)                                 */
/* ================================================================== */

static int test_parse_ltp_defaults(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "ltp-send", "--device", "/dev/ttyACM0",
                     "--local", "dtn://test", "--remote", "dtn://remote",
                     "payload", NULL };
    int argc = 9;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.mtu != 64) {
        printf("\n    FAIL: mtu default = %d, expected 64\n", args.mtu);
        return 0;
    }
    if (args.owlt_ms != 30000) {
        printf("\n    FAIL: owlt_ms default = %d, expected 30000\n", args.owlt_ms);
        return 0;
    }
    if (args.retries != 7) {
        printf("\n    FAIL: retries default = %d, expected 7\n", args.retries);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: missing --local for ltp-send (Req 10.1)                       */
/* ================================================================== */

static int test_validate_missing_local_ltp_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_LTP_SEND;
    args.device = "/dev/ttyUSB0";
    args.remote_eid = "dtn://remote";
    args.payload = "test";
    args.mtu = 64;
    args.owlt_ms = 1500;
    args.retries = 7;
    /* local_eid is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing --local for ltp-send\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: missing --remote for ltp-send                                 */
/* ================================================================== */

static int test_validate_missing_remote_ltp_send(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_LTP_SEND;
    args.device = "/dev/ttyUSB0";
    args.local_eid = "dtn://local";
    args.payload = "test";
    args.mtu = 64;
    args.owlt_ms = 1500;
    args.retries = 7;
    /* remote_eid is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing --remote for ltp-send\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Test: missing --local for ltp-recv                                  */
/* ================================================================== */

static int test_validate_missing_local_ltp_recv(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_LTP_RECV;
    args.device = "/dev/ttyUSB0";
    args.mtu = 64;
    args.owlt_ms = 1500;
    args.retries = 7;
    /* local_eid is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing --local for ltp-recv\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Beacon CLI tests                                                    */
/* ================================================================== */

static int test_parse_beacon_subcommand(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "beacon", "--device", "/dev/ttyACM0",
                     "--callsign", "G4DPZ-1", "--lat", "52.467",
                     "--lon", "-2.022", NULL };
    int argc = 10;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error for beacon\n");
        return 0;
    }
    if (args.mode != CMD_MODE_BEACON) {
        printf("\n    FAIL: mode = %d, expected CMD_MODE_BEACON\n", args.mode);
        return 0;
    }
    if (!args.beacon_callsign || strcmp(args.beacon_callsign, "G4DPZ-1") != 0) {
        printf("\n    FAIL: callsign mismatch\n");
        return 0;
    }
    return 1;
}

static int test_parse_beacon_defaults(void)
{
    cli_args_t args;
    optind = 1;
    char *argv[] = { "kiss_interface", "beacon", "--device", "/dev/ttyACM0",
                     "--callsign", "G4DPZ-1", "--lat", "52.467",
                     "--lon", "-2.022", NULL };
    int argc = 10;

    if (parse_args(argc, argv, &args) != 0) {
        printf("\n    FAIL: parse_args returned error\n");
        return 0;
    }
    if (args.beacon_interval != 120) {
        printf("\n    FAIL: beacon_interval=%d, expected 120\n", args.beacon_interval);
        return 0;
    }
    if (args.beacon_comment != NULL) {
        printf("\n    FAIL: beacon_comment should be NULL (resolved at dispatch)\n");
        return 0;
    }
    return 1;
}

static int test_validate_missing_callsign_beacon(void)
{
    cli_args_t args;
    memset(&args, 0, sizeof(args));
    args.mode = CMD_MODE_BEACON;
    args.device = "/dev/ttyUSB0";
    /* beacon_callsign is NULL */

    if (validate_args(&args) == 0) {
        printf("\n    FAIL: validate_args should fail with missing --callsign for beacon\n");
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(void)
{
    printf("CLI parsing tests\n");
    printf("=================\n\n");

    printf("Subcommand parsing (Req 8.1):\n");
    TEST(parse_send_subcommand);
    TEST(parse_receive_subcommand);
    TEST(parse_echo_subcommand);
    TEST(parse_unknown_subcommand);
    TEST(parse_no_args);

    printf("\nCommon options (Req 8.2):\n");
    TEST(parse_common_options);

    printf("\nTX-delay/TX-tail options (Req 8.3):\n");
    TEST(parse_txdelay_txtail);
    TEST(parse_txdelay_txtail_defaults);

    printf("\nEcho delay option (Req 8.4):\n");
    TEST(parse_echo_delay);
    TEST(parse_echo_delay_default);

    printf("\nArgument validation (Req 8.6):\n");
    TEST(validate_missing_device);
    TEST(validate_missing_src_send);
    TEST(validate_missing_dst_send);
    TEST(validate_missing_payload_send);
    TEST(validate_missing_src_echo);
    TEST(validate_receive_no_callsigns);
    TEST(validate_mode_none);
    TEST(validate_valid_send);

    printf("\nPing subcommand and options (Req 1.1-1.6):\n");
    TEST(parse_ping_subcommand);
    TEST(parse_ping_options);
    TEST(parse_ping_defaults);
    TEST(validate_missing_device_ping);
    TEST(validate_missing_src_ping);
    TEST(validate_missing_dst_ping);

    printf("\nLTP subcommands and options (Req 10.1, 10.2, 11.1, 11.2, 14.1):\n");
    TEST(parse_ltp_send_subcommand);
    TEST(parse_ltp_recv_subcommand);
    TEST(parse_ltp_defaults);
    TEST(validate_missing_local_ltp_send);
    TEST(validate_missing_remote_ltp_send);
    TEST(validate_missing_local_ltp_recv);

    printf("\nBeacon subcommand (Req 6.1-6.6):\n");
    TEST(parse_beacon_subcommand);
    TEST(parse_beacon_defaults);
    TEST(validate_missing_callsign_beacon);

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
