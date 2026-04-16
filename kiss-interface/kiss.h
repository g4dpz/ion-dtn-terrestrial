#ifndef KISS_H
#define KISS_H

#include <stdint.h>
#include <stddef.h>

#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MAX_PAYLOAD 65535

/* Encode payload into KISS frame. Returns frame length, or -1 on error.
 * out must be at least (len * 2 + 3) bytes. */
int kiss_encode(const uint8_t *payload, size_t len,
                uint8_t *out, size_t out_size);

/* Stateful KISS decoder (byte-at-a-time state machine) */
typedef struct {
    uint8_t buf[KISS_MAX_PAYLOAD + 1]; /* +1 for command byte */
    size_t  len;
    int     in_frame;
    int     escape;
} kiss_decoder_t;

void kiss_decoder_init(kiss_decoder_t *dec);

/* Feed one byte. Returns 1 when a complete data frame is available.
 * On return 1, copies payload (without command byte) to out, sets *out_len.
 * Returns 0 if more bytes needed, -1 on error (overflow, non-data cmd). */
int kiss_decoder_feed(kiss_decoder_t *dec, uint8_t byte,
                      uint8_t *out, size_t out_size, size_t *out_len);

/* Build a KISS command frame (e.g. TX-delay, TX-tail).
 * cmd: command type (0x01=txdelay, 0x04=txtail, etc.)
 * value: parameter value byte.
 * out must be at least 4 bytes. Returns frame length (always 4). */
int kiss_build_cmd(uint8_t cmd, uint8_t value, uint8_t *out, size_t out_size);

#endif
