#ifndef SDNV_H
#define SDNV_H

#include <stdint.h>
#include <stddef.h>

#define SDNV_MAX_BYTES 10  /* Max bytes for values up to 2^63-1 */

/* Encode a non-negative value into SDNV format.
 * out must be at least SDNV_MAX_BYTES.
 * Returns number of bytes written, or -1 on error (value too large). */
int sdnv_encode(uint64_t value, uint8_t *out, size_t out_size);

/* Decode an SDNV from a buffer.
 * Returns number of bytes consumed, or -1 on error (truncated/overflow).
 * Sets *value on success. */
int sdnv_decode(const uint8_t *buf, size_t buf_len, uint64_t *value);

#endif
