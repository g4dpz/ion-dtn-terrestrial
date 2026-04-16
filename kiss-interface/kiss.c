/*
 * kiss.c — KISS protocol encoding and decoding
 *
 * Implements KISS frame encoding (byte-stuffing), a byte-at-a-time
 * decoder state machine, and TNC command frame construction.
 */

#include "kiss.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* kiss_encode                                                         */
/* ------------------------------------------------------------------ */
int kiss_encode(const uint8_t *payload, size_t len,
                uint8_t *out, size_t out_size)
{
    if (!payload && len > 0)
        return -1;
    if (!out)
        return -1;

    /* Worst case: FEND + cmd + 2*len (all bytes escaped) + FEND */
    size_t needed = 3 + len * 2;
    if (out_size < needed && out_size < 3 + len)
        /* We'll check dynamically below; just need room for the minimum */
        (void)0;

    size_t pos = 0;

    /* Leading FEND */
    if (pos >= out_size) return -1;
    out[pos++] = KISS_FEND;

    /* Command byte 0x00 (data, port 0) */
    if (pos >= out_size) return -1;
    out[pos++] = 0x00;

    /* Payload with byte-stuffing */
    for (size_t i = 0; i < len; i++) {
        uint8_t b = payload[i];
        if (b == KISS_FEND) {
            if (pos + 2 > out_size) return -1;
            out[pos++] = KISS_FESC;
            out[pos++] = KISS_TFEND;
        } else if (b == KISS_FESC) {
            if (pos + 2 > out_size) return -1;
            out[pos++] = KISS_FESC;
            out[pos++] = KISS_TFESC;
        } else {
            if (pos >= out_size) return -1;
            out[pos++] = b;
        }
    }

    /* Trailing FEND */
    if (pos >= out_size) return -1;
    out[pos++] = KISS_FEND;

    return (int)pos;
}

/* ------------------------------------------------------------------ */
/* kiss_decoder_init                                                    */
/* ------------------------------------------------------------------ */
void kiss_decoder_init(kiss_decoder_t *dec)
{
    if (!dec) return;
    dec->len      = 0;
    dec->in_frame = 0;
    dec->escape   = 0;
}

/* ------------------------------------------------------------------ */
/* kiss_decoder_feed                                                    */
/* ------------------------------------------------------------------ */
int kiss_decoder_feed(kiss_decoder_t *dec, uint8_t byte,
                      uint8_t *out, size_t out_size, size_t *out_len)
{
    if (!dec)
        return -1;

    /* ---- FEND handling ---- */
    if (byte == KISS_FEND) {
        if (!dec->in_frame) {
            /* Transition IDLE → IN_FRAME */
            dec->in_frame = 1;
            dec->len      = 0;
            dec->escape   = 0;
            return 0;
        }
        /* We are in a frame and got FEND → frame boundary */
        if (dec->len == 0) {
            /* Empty frame (consecutive FENDs) — stay ready */
            return 0;
        }
        /* Frame complete — check command byte */
        uint8_t cmd = dec->buf[0];
        if ((cmd & 0x0F) != 0x00) {
            /* Non-data command — discard */
            dec->len      = 0;
            dec->in_frame = 1; /* ready for next frame */
            dec->escape   = 0;
            return -1;
        }
        /* Data frame — copy payload (skip command byte) to out */
        size_t payload_len = dec->len - 1;
        if (!out || out_size < payload_len) {
            dec->len      = 0;
            dec->in_frame = 1;
            dec->escape   = 0;
            return -1;
        }
        memcpy(out, dec->buf + 1, payload_len);
        if (out_len)
            *out_len = payload_len;

        dec->len      = 0;
        dec->in_frame = 1; /* ready for next frame */
        dec->escape   = 0;
        return 1;
    }

    /* If not in a frame, ignore bytes */
    if (!dec->in_frame)
        return 0;

    /* ---- FESC handling ---- */
    if (dec->escape) {
        dec->escape = 0;
        if (byte == KISS_TFEND)
            byte = KISS_FEND;
        else if (byte == KISS_TFESC)
            byte = KISS_FESC;
        /* else: protocol error — just store the byte as-is */
    } else if (byte == KISS_FESC) {
        dec->escape = 1;
        return 0;
    }

    /* ---- Store byte ---- */
    /* buf holds command byte + payload; max payload is KISS_MAX_PAYLOAD,
     * so max buf usage is KISS_MAX_PAYLOAD + 1 */
    if (dec->len >= KISS_MAX_PAYLOAD + 1) {
        /* Overflow — discard frame */
        dec->in_frame = 0;
        dec->len      = 0;
        dec->escape   = 0;
        return -1;
    }
    dec->buf[dec->len++] = byte;
    return 0;
}

/* ------------------------------------------------------------------ */
/* kiss_build_cmd                                                       */
/* ------------------------------------------------------------------ */
int kiss_build_cmd(uint8_t cmd, uint8_t value, uint8_t *out, size_t out_size)
{
    if (!out || out_size < 4)
        return -1;

    out[0] = KISS_FEND;
    out[1] = cmd;
    out[2] = value;
    out[3] = KISS_FEND;
    return 4;
}
