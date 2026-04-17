#include "cbor.h"
#include <string.h>

/* Encode CBOR header: major type (3 bits) + argument */
static int cbor_encode_head(uint8_t major, uint64_t arg, uint8_t *out, size_t out_size)
{
    uint8_t mt = (uint8_t)(major << 5);
    if (arg <= 23) {
        if (out_size < 1) return -1;
        out[0] = mt | (uint8_t)arg;
        return 1;
    } else if (arg <= 0xFF) {
        if (out_size < 2) return -1;
        out[0] = mt | 24;
        out[1] = (uint8_t)arg;
        return 2;
    } else if (arg <= 0xFFFF) {
        if (out_size < 3) return -1;
        out[0] = mt | 25;
        out[1] = (uint8_t)(arg >> 8);
        out[2] = (uint8_t)arg;
        return 3;
    } else if (arg <= 0xFFFFFFFFULL) {
        if (out_size < 5) return -1;
        out[0] = mt | 26;
        out[1] = (uint8_t)(arg >> 24);
        out[2] = (uint8_t)(arg >> 16);
        out[3] = (uint8_t)(arg >> 8);
        out[4] = (uint8_t)arg;
        return 5;
    } else {
        if (out_size < 9) return -1;
        out[0] = mt | 27;
        out[1] = (uint8_t)(arg >> 56);
        out[2] = (uint8_t)(arg >> 48);
        out[3] = (uint8_t)(arg >> 40);
        out[4] = (uint8_t)(arg >> 32);
        out[5] = (uint8_t)(arg >> 24);
        out[6] = (uint8_t)(arg >> 16);
        out[7] = (uint8_t)(arg >> 8);
        out[8] = (uint8_t)arg;
        return 9;
    }
}

/* Decode CBOR header: returns bytes consumed, sets *major and *arg. */
static int cbor_decode_head(const uint8_t *buf, size_t len, uint8_t *major, uint64_t *arg)
{
    if (len < 1) return -1;
    *major = buf[0] >> 5;
    uint8_t ai = buf[0] & 0x1F;
    if (ai <= 23) {
        *arg = ai;
        return 1;
    } else if (ai == 24) {
        if (len < 2) return -1;
        *arg = buf[1];
        return 2;
    } else if (ai == 25) {
        if (len < 3) return -1;
        *arg = ((uint64_t)buf[1] << 8) | buf[2];
        return 3;
    } else if (ai == 26) {
        if (len < 5) return -1;
        *arg = ((uint64_t)buf[1] << 24) | ((uint64_t)buf[2] << 16) |
               ((uint64_t)buf[3] << 8) | buf[4];
        return 5;
    } else if (ai == 27) {
        if (len < 9) return -1;
        *arg = ((uint64_t)buf[1] << 56) | ((uint64_t)buf[2] << 48) |
               ((uint64_t)buf[3] << 40) | ((uint64_t)buf[4] << 32) |
               ((uint64_t)buf[5] << 24) | ((uint64_t)buf[6] << 16) |
               ((uint64_t)buf[7] << 8) | buf[8];
        return 9;
    }
    return -1; /* ai 28-30 reserved, 31 = indefinite */
}

int cbor_encode_uint(uint64_t value, uint8_t *out, size_t out_size)
{
    return cbor_encode_head(0, value, out, out_size);
}

int cbor_encode_bstr(const uint8_t *data, size_t len, uint8_t *out, size_t out_size)
{
    int hlen = cbor_encode_head(2, (uint64_t)len, out, out_size);
    if (hlen < 0) return -1;
    if ((size_t)hlen + len > out_size) return -1;
    if (len > 0 && data) memcpy(out + hlen, data, len);
    return hlen + (int)len;
}

int cbor_encode_tstr(const char *str, uint8_t *out, size_t out_size)
{
    size_t len = str ? strlen(str) : 0;
    int hlen = cbor_encode_head(3, (uint64_t)len, out, out_size);
    if (hlen < 0) return -1;
    if ((size_t)hlen + len > out_size) return -1;
    if (len > 0) memcpy(out + hlen, str, len);
    return hlen + (int)len;
}

int cbor_encode_array(uint64_t count, uint8_t *out, size_t out_size)
{
    return cbor_encode_head(4, count, out, out_size);
}

int cbor_encode_indef_array_start(uint8_t *out, size_t out_size)
{
    if (out_size < 1) return -1;
    out[0] = 0x9F;
    return 1;
}

int cbor_encode_break(uint8_t *out, size_t out_size)
{
    if (out_size < 1) return -1;
    out[0] = 0xFF;
    return 1;
}

int cbor_decode_uint(const uint8_t *buf, size_t len, uint64_t *value)
{
    uint8_t major;
    int n = cbor_decode_head(buf, len, &major, value);
    if (n < 0) return -1;
    if (major != 0) return -1;
    return n;
}

int cbor_decode_bstr(const uint8_t *buf, size_t len, const uint8_t **data, size_t *data_len)
{
    uint8_t major;
    uint64_t slen;
    int n = cbor_decode_head(buf, len, &major, &slen);
    if (n < 0) return -1;
    if (major != 2) return -1;
    if ((size_t)n + slen > len) return -1;
    if (data) *data = buf + n;
    if (data_len) *data_len = (size_t)slen;
    return n + (int)slen;
}

int cbor_decode_tstr(const uint8_t *buf, size_t len, const char **str, size_t *str_len)
{
    uint8_t major;
    uint64_t slen;
    int n = cbor_decode_head(buf, len, &major, &slen);
    if (n < 0) return -1;
    if (major != 3) return -1;
    if ((size_t)n + slen > len) return -1;
    if (str) *str = (const char *)(buf + n);
    if (str_len) *str_len = (size_t)slen;
    return n + (int)slen;
}

int cbor_decode_array(const uint8_t *buf, size_t len, uint64_t *count)
{
    uint8_t major;
    int n = cbor_decode_head(buf, len, &major, count);
    if (n < 0) return -1;
    if (major != 4) return -1;
    return n;
}

int cbor_is_indef_array(const uint8_t *buf, size_t len)
{
    return (len >= 1 && buf[0] == 0x9F) ? 1 : 0;
}

int cbor_is_break(const uint8_t *buf, size_t len)
{
    return (len >= 1 && buf[0] == 0xFF) ? 1 : 0;
}
