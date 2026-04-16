/*
 * sdnv.c — Self-Delimiting Numeric Value encoding and decoding
 *
 * SDNV encodes non-negative integers using 7 data bits per byte,
 * with the MSB as a continuation bit (1 = more bytes follow, 0 = last byte).
 * Most significant byte first (big-endian style).
 *
 * Maximum encoded length: 10 bytes (for values up to 2^63 - 1).
 */

#include "sdnv.h"

int sdnv_encode(uint64_t value, uint8_t *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        return -1;

    /* Determine how many bytes we need: each byte carries 7 bits */
    uint8_t tmp[SDNV_MAX_BYTES];
    int nbytes = 0;

    /* Build encoding in reverse (least significant byte first) */
    tmp[0] = (uint8_t)(value & 0x7F);  /* last byte: MSB = 0 */
    value >>= 7;
    nbytes = 1;

    while (value > 0) {
        if (nbytes >= SDNV_MAX_BYTES)
            return -1;  /* value too large */
        tmp[nbytes] = (uint8_t)(value & 0x7F) | 0x80;  /* continuation bit */
        value >>= 7;
        nbytes++;
    }

    if ((size_t)nbytes > out_size)
        return -1;  /* output buffer too small */

    /* Reverse into output (most significant byte first) */
    for (int i = 0; i < nbytes; i++)
        out[i] = tmp[nbytes - 1 - i];

    return nbytes;
}

int sdnv_decode(const uint8_t *buf, size_t buf_len, uint64_t *value)
{
    if (buf == NULL || buf_len == 0 || value == NULL)
        return -1;

    uint64_t result = 0;
    size_t i;

    for (i = 0; i < buf_len; i++) {
        /* Check for overflow: if result would overflow when shifted left by 7 */
        if (result > (UINT64_MAX >> 7))
            return -1;  /* overflow */

        result = (result << 7) | (buf[i] & 0x7F);

        /* Check the decoded value stays within int64 range (0 to 2^63-1) */
        if (result > (uint64_t)INT64_MAX)
            return -1;  /* overflow: exceeds max supported value */

        if ((buf[i] & 0x80) == 0) {
            /* Last byte (no continuation bit) */
            *value = result;
            return (int)(i + 1);
        }
    }

    /* Ran out of buffer without finding a terminating byte */
    return -1;
}
