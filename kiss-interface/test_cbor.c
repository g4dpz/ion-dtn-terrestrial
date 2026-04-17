#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cbor.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else printf("[FAIL]\n"); } while(0)
#define ITERATIONS 1000

/* Property 1: uint round-trip */
static int test_cbor_uint_roundtrip(void)
{
    uint8_t buf[16];
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t val = 0;
        for (int j = 0; j < 8; j++) val = (val << 8) | (rand() & 0xFF);
        int n = cbor_encode_uint(val, buf, sizeof(buf));
        if (n < 0) { printf("\n    FAIL encode at %d\n", i); return 0; }
        uint64_t out;
        int d = cbor_decode_uint(buf, (size_t)n, &out);
        if (d != n || out != val) {
            printf("\n    FAIL at %d: val=%lu out=%lu\n", i, (unsigned long)val, (unsigned long)out);
            return 0;
        }
    }
    return 1;
}

/* Property 2: byte string round-trip */
static int test_cbor_bstr_roundtrip(void)
{
    uint8_t data[900], buf[920];
    for (int i = 0; i < ITERATIONS; i++) {
        size_t len = (size_t)(rand() % 901);
        for (size_t j = 0; j < len; j++) data[j] = (uint8_t)(rand() & 0xFF);
        int n = cbor_encode_bstr(data, len, buf, sizeof(buf));
        if (n < 0) { printf("\n    FAIL encode at %d len=%zu\n", i, len); return 0; }
        const uint8_t *out; size_t out_len;
        int d = cbor_decode_bstr(buf, (size_t)n, &out, &out_len);
        if (d != n || out_len != len) {
            printf("\n    FAIL at %d: len=%zu out_len=%zu\n", i, len, out_len); return 0;
        }
        if (len > 0 && memcmp(data, out, len) != 0) {
            printf("\n    FAIL data mismatch at %d\n", i); return 0;
        }
    }
    return 1;
}

/* Property 3: text string round-trip */
static int test_cbor_tstr_roundtrip(void)
{
    char str[257]; uint8_t buf[270];
    for (int i = 0; i < ITERATIONS; i++) {
        int len = rand() % 257;
        for (int j = 0; j < len; j++) str[j] = (char)(32 + (rand() % 95));
        str[len] = '\0';
        int n = cbor_encode_tstr(str, buf, sizeof(buf));
        if (n < 0) { printf("\n    FAIL encode at %d\n", i); return 0; }
        const char *out; size_t out_len;
        int d = cbor_decode_tstr(buf, (size_t)n, &out, &out_len);
        if (d != n || out_len != (size_t)len) {
            printf("\n    FAIL at %d\n", i); return 0;
        }
        if (len > 0 && memcmp(str, out, (size_t)len) != 0) {
            printf("\n    FAIL data mismatch at %d\n", i); return 0;
        }
    }
    return 1;
}

/* Unit: known uint encodings */
static int test_uint_known_values(void)
{
    uint8_t buf[16]; int n;
    n = cbor_encode_uint(0, buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x00) { printf("\n    FAIL: 0\n"); return 0; }
    n = cbor_encode_uint(23, buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x17) { printf("\n    FAIL: 23\n"); return 0; }
    n = cbor_encode_uint(24, buf, sizeof(buf));
    if (n != 2 || buf[0] != 0x18 || buf[1] != 0x18) { printf("\n    FAIL: 24\n"); return 0; }
    n = cbor_encode_uint(255, buf, sizeof(buf));
    if (n != 2 || buf[0] != 0x18 || buf[1] != 0xFF) { printf("\n    FAIL: 255\n"); return 0; }
    n = cbor_encode_uint(256, buf, sizeof(buf));
    if (n != 3 || buf[0] != 0x19 || buf[1] != 0x01 || buf[2] != 0x00) { printf("\n    FAIL: 256\n"); return 0; }
    n = cbor_encode_uint(65535, buf, sizeof(buf));
    if (n != 3 || buf[0] != 0x19) { printf("\n    FAIL: 65535\n"); return 0; }
    n = cbor_encode_uint(65536, buf, sizeof(buf));
    if (n != 5 || buf[0] != 0x1A) { printf("\n    FAIL: 65536\n"); return 0; }
    n = cbor_encode_uint(0xFFFFFFFFULL, buf, sizeof(buf));
    if (n != 5) { printf("\n    FAIL: 2^32-1\n"); return 0; }
    n = cbor_encode_uint(0x100000000ULL, buf, sizeof(buf));
    if (n != 9) { printf("\n    FAIL: 2^32\n"); return 0; }
    return 1;
}

/* Unit: decode truncated */
static int test_decode_truncated(void)
{
    uint64_t val;
    uint8_t buf[] = { 0x19, 0x01 }; /* 2-byte uint, missing second byte */
    if (cbor_decode_uint(buf, 1, &val) != -1) { /* only 1 byte given for 2-byte header */
        printf("\n    FAIL: should return -1\n"); return 0;
    }
    return 1;
}

/* Unit: empty strings */
static int test_empty_strings(void)
{
    uint8_t buf[16];
    int n = cbor_encode_bstr(NULL, 0, buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x40) { printf("\n    FAIL: empty bstr\n"); return 0; }
    n = cbor_encode_tstr("", buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x60) { printf("\n    FAIL: empty tstr\n"); return 0; }
    return 1;
}

/* Unit: array and indef */
static int test_array_encoding(void)
{
    uint8_t buf[16];
    int n = cbor_encode_array(4, buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x84) { printf("\n    FAIL: array(4)\n"); return 0; }
    n = cbor_encode_indef_array_start(buf, sizeof(buf));
    if (n != 1 || buf[0] != 0x9F) { printf("\n    FAIL: indef\n"); return 0; }
    if (!cbor_is_indef_array(buf, 1)) { printf("\n    FAIL: is_indef\n"); return 0; }
    n = cbor_encode_break(buf, sizeof(buf));
    if (n != 1 || buf[0] != 0xFF) { printf("\n    FAIL: break\n"); return 0; }
    if (!cbor_is_break(buf, 1)) { printf("\n    FAIL: is_break\n"); return 0; }
    return 1;
}

int main(void)
{
    srand((unsigned)time(NULL));
    printf("CBOR module tests\n");
    printf("=================\n\n");
    printf("Property tests (%d iterations each):\n", ITERATIONS);
    TEST(cbor_uint_roundtrip);
    TEST(cbor_bstr_roundtrip);
    TEST(cbor_tstr_roundtrip);
    printf("\nUnit tests:\n");
    TEST(uint_known_values);
    TEST(decode_truncated);
    TEST(empty_strings);
    TEST(array_encoding);
    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
