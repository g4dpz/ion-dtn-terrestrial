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
    CMD_MODE_ECHO
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

    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
