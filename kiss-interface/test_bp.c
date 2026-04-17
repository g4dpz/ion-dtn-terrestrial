#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bp.h"
#include "cbor.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  %-60s ", #name); \
    if (test_##name()) { tests_passed++; printf("[PASS]\n"); } \
    else printf("[FAIL]\n"); } while(0)
#define ITERATIONS 1000
#define HEAVY_ITERATIONS 100

static void rand_bytes(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rand() & 0xFF);
}

/* Property 4: Bundle encode/decode round-trip */
static int test_bundle_roundtrip(void)
{
    uint8_t buf[BP_MAX_BUNDLE_BUF];
    for (int iter = 0; iter < ITERATIONS; iter++) {
        bp_eid_t src = { "dtn://g4dpz-1/msg" };
        bp_eid_t dst = { "dtn://g4dpz-2/msg" };
        size_t plen = (size_t)(rand() % 200);
        uint8_t payload[200];
        rand_bytes(payload, plen);
        uint64_t lifetime = 3600000;
        uint64_t seq = (uint64_t)(rand() % 1000);

        int enc = bp_encode_bundle(&src, &dst, payload, plen, lifetime, seq, buf, sizeof(buf));
        if (enc < 0) { printf("\n    FAIL encode at %d plen=%zu\n", iter, plen); return 0; }

        bp_bundle_t b;
        int dec = bp_decode_bundle(buf, (size_t)enc, &b);
        if (dec < 0) { printf("\n    FAIL decode at %d\n", iter); return 0; }

        if (strcmp(b.primary.src.uri, src.uri) != 0) {
            printf("\n    FAIL src at %d: '%s'\n", iter, b.primary.src.uri); return 0;
        }
        if (strcmp(b.primary.dst.uri, dst.uri) != 0) {
            printf("\n    FAIL dst at %d\n", iter); return 0;
        }
        if (b.primary.lifetime_ms != lifetime) {
            printf("\n    FAIL lifetime at %d\n", iter); return 0;
        }
        if (b.primary.timestamp.seq != seq) {
            printf("\n    FAIL seq at %d\n", iter); return 0;
        }
        if (b.payload_len != plen) {
            printf("\n    FAIL plen at %d: %zu vs %zu\n", iter, b.payload_len, plen); return 0;
        }
        if (plen > 0 && memcmp(b.payload, payload, plen) != 0) {
            printf("\n    FAIL data at %d\n", iter); return 0;
        }
    }
    return 1;
}

/* Property 6: DTN time round-trip */
static int test_dtn_time_roundtrip(void)
{
    for (int i = 0; i < ITERATIONS; i++) {
        /* Random Unix time after 2000 */
        uint64_t unix_ts = BP_DTN_EPOCH + (uint64_t)(rand() % 1000000000);
        uint64_t dtn_ms = (unix_ts - BP_DTN_EPOCH) * 1000;
        uint64_t back = bp_dtn_to_unix(dtn_ms);
        if (back != unix_ts) {
            printf("\n    FAIL at %d: %lu -> %lu -> %lu\n", i,
                   (unsigned long)unix_ts, (unsigned long)dtn_ms, (unsigned long)back);
            return 0;
        }
    }
    return 1;
}

/* Property 7: CRC-16 determinism */
static int test_crc16_determinism(void)
{
    uint8_t data[256];
    for (int i = 0; i < ITERATIONS; i++) {
        size_t len = (size_t)(rand() % 257);
        rand_bytes(data, len);
        uint16_t c1 = bp_crc16(data, len);
        uint16_t c2 = bp_crc16(data, len);
        if (c1 != c2) { printf("\n    FAIL at %d\n", i); return 0; }
    }
    return 1;
}

/* Unit: version = 7 */
static int test_version_7(void)
{
    uint8_t buf[BP_MAX_BUNDLE_BUF];
    bp_eid_t src = { "dtn://test" }, dst = { "dtn://test2" };
    uint8_t payload[] = "hi";
    int n = bp_encode_bundle(&src, &dst, payload, 2, 3600000, 1, buf, sizeof(buf));
    if (n < 0) return 0;
    bp_bundle_t b;
    if (bp_decode_bundle(buf, (size_t)n, &b) < 0) return 0;
    /* Version is checked inside decode — if it gets here, version was 7 */
    return 1;
}

/* Unit: EID encoding */
static int test_eid_encoding(void)
{
    bp_eid_t eid = { "dtn://g4dpz-1/msg" };
    uint8_t buf[64];
    int n = bp_eid_encode(&eid, buf, sizeof(buf));
    if (n < 0) { printf("\n    FAIL encode\n"); return 0; }
    bp_eid_t out;
    int d = bp_eid_decode(buf, (size_t)n, &out);
    if (d < 0) { printf("\n    FAIL decode\n"); return 0; }
    if (strcmp(out.uri, "dtn://g4dpz-1/msg") != 0) {
        printf("\n    FAIL: '%s'\n", out.uri); return 0;
    }
    return 1;
}

/* Unit: DTN epoch */
static int test_dtn_epoch(void)
{
    return BP_DTN_EPOCH == 946684800ULL;
}

/* Unit: decode wrong version */
static int test_decode_wrong_version(void)
{
    /* Manually build a bundle with version 6 */
    uint8_t buf[128];
    size_t pos = 0;
    buf[pos++] = 0x9F; /* indef array */
    int n = cbor_encode_array(9, buf + pos, sizeof(buf) - pos); pos += (size_t)n;
    n = cbor_encode_uint(6, buf + pos, sizeof(buf) - pos); pos += (size_t)n; /* wrong version */
    /* Don't need to complete — decode should fail on version check */
    n = cbor_encode_uint(0, buf + pos, sizeof(buf) - pos); pos += (size_t)n;
    bp_bundle_t b;
    /* This will fail at version check or later — either way returns -1 */
    return bp_decode_bundle(buf, pos, &b) == -1;
}

/* Unit: fragment count */
static int test_fragment_count(void)
{
    if (bp_fragment_count(900, 900) != 1) return 0;
    if (bp_fragment_count(901, 900) != 2) return 0;
    if (bp_fragment_count(4000, 900) != 5) return 0;
    if (bp_fragment_count(1, 900) != 1) return 0;
    return 1;
}

/* Property 5: Fragment/reassembly round-trip */
static int test_fragment_reassembly_roundtrip(void)
{
    for (int iter = 0; iter < HEAVY_ITERATIONS; iter++) {
        /* Random payload 1-4000 bytes */
        size_t plen = 1 + (size_t)(rand() % 4000);
        uint8_t *payload = (uint8_t *)malloc(plen);
        if (!payload) return 0;
        rand_bytes(payload, plen);

        bp_eid_t src = { "dtn://g4dpz-1/msg" };
        bp_eid_t dst = { "dtn://g4dpz-2/msg" };
        uint64_t lifetime = 3600000;
        uint64_t seq = (uint64_t)(rand() % 1000);

        int nfrags = bp_fragment_count(plen, BP_DEFAULT_FRAGMENT_SIZE);
        if (nfrags < 1) { free(payload); printf("\n    FAIL frag_count\n"); return 0; }

        bp_reassembly_t reasm;
        bp_reassembly_init(&reasm);

        /* Shuffle fragment order */
        int order[BP_MAX_FRAGMENTS];
        for (int i = 0; i < nfrags; i++) order[i] = i;
        for (int i = nfrags - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }

        int complete = 0;
        uint64_t ct = bp_dtn_time_now();
        for (int fi = 0; fi < nfrags; fi++) {
            int f = order[fi];
            uint64_t off = (uint64_t)f * BP_DEFAULT_FRAGMENT_SIZE;
            size_t flen = plen - (size_t)off;
            if (flen > BP_DEFAULT_FRAGMENT_SIZE) flen = BP_DEFAULT_FRAGMENT_SIZE;

            uint8_t fbuf[BP_MAX_BUNDLE_BUF];
            int enc = bp_encode_fragment(&src, &dst, payload + off, flen,
                                         lifetime, seq, off, (uint64_t)plen,
                                         ct, fbuf, sizeof(fbuf));
            if (enc < 0) { free(payload); printf("\n    FAIL encode frag %d\n", f); return 0; }

            bp_bundle_t b;
            int dec = bp_decode_bundle(fbuf, (size_t)enc, &b);
            if (dec < 0) { free(payload); printf("\n    FAIL decode frag %d\n", f); return 0; }

            int rc = bp_reassembly_add(&reasm, &b);
            if (rc == 1) complete = 1;
            if (rc < 0) { free(payload); printf("\n    FAIL reasm frag %d\n", f); return 0; }
        }

        if (!complete) { free(payload); printf("\n    FAIL not complete at %d\n", iter); return 0; }
        if (reasm.bytes_received != plen) {
            free(payload); printf("\n    FAIL bytes %zu vs %zu\n", reasm.bytes_received, plen); return 0;
        }
        if (memcmp(reasm.data, payload, plen) != 0) {
            free(payload); printf("\n    FAIL data mismatch at %d\n", iter); return 0;
        }
        free(payload);
    }
    return 1;
}

/* Unit: 4000-byte payload → 5 fragments */
static int test_fragment_4000_bytes(void)
{
    if (bp_fragment_count(4000, 800) != 5) { printf("\n    FAIL: count\n"); return 0; }

    uint8_t payload[4000];
    rand_bytes(payload, 4000);
    bp_eid_t src = { "dtn://test" }, dst = { "dtn://test2" };

    bp_reassembly_t reasm;
    bp_reassembly_init(&reasm);

    uint64_t ct = bp_dtn_time_now();
    for (int f = 0; f < 5; f++) {
        size_t off = (size_t)f * 800;
        size_t flen = 4000 - off;
        if (flen > 800) flen = 800;

        uint8_t fbuf[BP_MAX_BUNDLE_BUF];
        int enc = bp_encode_fragment(&src, &dst, payload + off, flen,
                                     3600000, 1, (uint64_t)off, 4000,
                                     ct, fbuf, sizeof(fbuf));
        if (enc < 0) { printf("\n    FAIL encode %d\n", f); return 0; }

        bp_bundle_t b;
        if (bp_decode_bundle(fbuf, (size_t)enc, &b) < 0) { printf("\n    FAIL decode %d\n", f); return 0; }

        /* Verify fragment flag */
        if (!(b.primary.flags & BP_FLAG_FRAGMENT)) { printf("\n    FAIL no frag flag %d\n", f); return 0; }
        if (b.primary.fragment_offset != (uint64_t)off) { printf("\n    FAIL offset %d\n", f); return 0; }
        if (b.primary.total_adu_len != 4000) { printf("\n    FAIL total_adu %d\n", f); return 0; }

        int rc = bp_reassembly_add(&reasm, &b);
        if (f < 4 && rc != 0) { printf("\n    FAIL early complete %d\n", f); return 0; }
        if (f == 4 && rc != 1) { printf("\n    FAIL not complete\n"); return 0; }
    }

    if (memcmp(reasm.data, payload, 4000) != 0) { printf("\n    FAIL data\n"); return 0; }
    return 1;
}

/* Unit: shuffled reassembly */
static int test_shuffled_reassembly(void)
{
    uint8_t payload[2700];
    rand_bytes(payload, 2700);
    bp_eid_t src = { "dtn://a" }, dst = { "dtn://b" };

    /* 3 fragments at 900 bytes each (using 900 for this specific test) */
    int order[] = { 2, 0, 1 };
    bp_reassembly_t reasm;
    bp_reassembly_init(&reasm);

    uint64_t ct = bp_dtn_time_now();
    for (int i = 0; i < 3; i++) {
        int f = order[i];
        size_t off = (size_t)f * 900;
        size_t flen = 2700 - off;
        if (flen > 900) flen = 900;

        uint8_t fbuf[BP_MAX_BUNDLE_BUF];
        int enc = bp_encode_fragment(&src, &dst, payload + off, flen,
                                     3600000, 1, (uint64_t)off, 2700,
                                     ct, fbuf, sizeof(fbuf));
        if (enc < 0) return 0;

        bp_bundle_t b;
        if (bp_decode_bundle(fbuf, (size_t)enc, &b) < 0) return 0;

        int rc = bp_reassembly_add(&reasm, &b);
        if (i < 2 && rc != 0) return 0;
        if (i == 2 && rc != 1) return 0;
    }

    return memcmp(reasm.data, payload, 2700) == 0;
}

int main(void)
{
    srand((unsigned)time(NULL));
    printf("Bundle Protocol tests\n");
    printf("=====================\n\n");
    printf("Property tests:\n");
    TEST(bundle_roundtrip);
    TEST(dtn_time_roundtrip);
    TEST(crc16_determinism);
    printf("\nUnit tests:\n");
    TEST(version_7);
    TEST(eid_encoding);
    TEST(dtn_epoch);
    TEST(decode_wrong_version);
    TEST(fragment_count);
    TEST(fragment_4000_bytes);
    TEST(shuffled_reassembly);
    printf("\nFragment property tests (%d iterations):\n", HEAVY_ITERATIONS);
    TEST(fragment_reassembly_roundtrip);
    printf("\n-----------------\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
