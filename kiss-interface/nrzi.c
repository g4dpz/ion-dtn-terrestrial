/*
 * nrzi.c — NRZI encoding, CRC-CCITT FCS, bit stuffing, flag framing
 */

#include "nrzi.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* nrzi_compute_fcs — CRC-CCITT (polynomial 0x8408, reflected)         */
/* ------------------------------------------------------------------ */
uint16_t nrzi_compute_fcs(const uint8_t *data, size_t len)
{
    uint16_t crc = NRZI_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ NRZI_CRC_POLY;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFF;
}

/* ------------------------------------------------------------------ */
/* nrzi_bytes_to_bits — convert bytes to bits, LSB first per byte      */
/* ------------------------------------------------------------------ */
size_t nrzi_bytes_to_bits(const uint8_t *data, size_t len,
                          uint8_t *out, size_t out_size)
{
    size_t total = len * 8;
    if (total > out_size) return 0;
    size_t idx = 0;
    for (size_t i = 0; i < len; i++) {
        for (int j = 0; j < 8; j++)
            out[idx++] = (data[i] >> j) & 1;
    }
    return idx;
}

/* ------------------------------------------------------------------ */
/* nrzi_bit_stuff — insert 0 after every 5 consecutive 1-bits          */
/* ------------------------------------------------------------------ */
int nrzi_bit_stuff(const uint8_t *bits, size_t num_bits,
                   uint8_t *out, size_t out_size)
{
    size_t idx = 0;
    int ones = 0;
    for (size_t i = 0; i < num_bits; i++) {
        if (idx >= out_size) return -1;
        out[idx++] = bits[i];
        if (bits[i] == 1) {
            ones++;
            if (ones == 5) {
                if (idx >= out_size) return -1;
                out[idx++] = 0;
                ones = 0;
            }
        } else {
            ones = 0;
        }
    }
    return (int)idx;
}

/* ------------------------------------------------------------------ */
/* nrzi_bit_destuff — remove stuffed 0 after every 5 consecutive 1s    */
/* ------------------------------------------------------------------ */
int nrzi_bit_destuff(const uint8_t *bits, size_t num_bits,
                     uint8_t *out, size_t out_size)
{
    size_t idx = 0;
    int ones = 0;
    int skip_next = 0;
    for (size_t i = 0; i < num_bits; i++) {
        if (skip_next) { skip_next = 0; ones = 0; continue; }
        if (idx >= out_size) return -1;
        out[idx++] = bits[i];
        if (bits[i] == 1) {
            ones++;
            if (ones == 5) skip_next = 1;
        } else {
            ones = 0;
        }
    }
    return (int)idx;
}

/* ------------------------------------------------------------------ */
/* nrzi_frame_to_bitstream — full pipeline: FCS + bits + stuff + flags  */
/* ------------------------------------------------------------------ */
int nrzi_frame_to_bitstream(const uint8_t *frame, size_t frame_len,
                            uint8_t *out, size_t out_size)
{
    if (!frame || frame_len < 16 || frame_len > NRZI_MAX_FRAME_BYTES - 2)
        return -1;

    /* Copy frame and fix SSID reserved bits (0xE0 mask) */
    uint8_t fixed[NRZI_MAX_FRAME_BYTES];
    memcpy(fixed, frame, frame_len);
    /* Dst SSID at byte 6, Src SSID at byte 13 */
    if (frame_len >= 14) {
        fixed[6]  = (fixed[6]  & 0x1F) | 0xE0;
        fixed[13] = (fixed[13] & 0x1F) | 0xE0;
    }

    /* Compute and append FCS */
    uint16_t fcs = nrzi_compute_fcs(fixed, frame_len);
    fixed[frame_len]     = fcs & 0xFF;
    fixed[frame_len + 1] = (fcs >> 8) & 0xFF;
    size_t total_bytes = frame_len + 2;

    /* Convert to bits */
    uint8_t raw_bits[NRZI_MAX_FRAME_BYTES * 8];
    size_t num_bits = nrzi_bytes_to_bits(fixed, total_bytes,
                                          raw_bits, sizeof(raw_bits));
    if (num_bits == 0) return -1;

    /* Bit stuff */
    uint8_t stuffed[NRZI_MAX_BITS];
    int stuffed_len = nrzi_bit_stuff(raw_bits, num_bits,
                                      stuffed, sizeof(stuffed));
    if (stuffed_len < 0) return -1;

    /* Flag bits: 0x7E = 01111110 LSB-first */
    static const uint8_t flag_bits[8] = {0, 1, 1, 1, 1, 1, 1, 0};

    /* Calculate total size */
    size_t preamble_bits = NRZI_PREAMBLE_FLAGS * 8;
    size_t closing_bits  = NRZI_CLOSING_FLAGS * 8;
    size_t total = preamble_bits + (size_t)stuffed_len + closing_bits;
    if (total > out_size) return -1;

    /* Assemble: preamble flags + stuffed content + closing flags */
    size_t pos = 0;
    for (int f = 0; f < NRZI_PREAMBLE_FLAGS; f++)
        for (int b = 0; b < 8; b++)
            out[pos++] = flag_bits[b];

    memcpy(out + pos, stuffed, (size_t)stuffed_len);
    pos += (size_t)stuffed_len;

    for (int f = 0; f < NRZI_CLOSING_FLAGS; f++)
        for (int b = 0; b < 8; b++)
            out[pos++] = flag_bits[b];

    return (int)pos;
}

/* ------------------------------------------------------------------ */
/* nrzi_encode — NRZI encode: 0=toggle, 1=hold                        */
/* ------------------------------------------------------------------ */
size_t nrzi_encode(const uint8_t *bits, size_t num_bits,
                   uint8_t *out, size_t out_size,
                   int initial_state)
{
    if (num_bits > out_size) return 0;
    int state = initial_state ? 1 : 0;
    for (size_t i = 0; i < num_bits; i++) {
        if (bits[i] == 0)
            state ^= 1;
        out[i] = (uint8_t)state;
    }
    return num_bits;
}

/* ------------------------------------------------------------------ */
/* nrzi_decode — NRZI decode: transition=0, no transition=1            */
/* ------------------------------------------------------------------ */
size_t nrzi_decode(const uint8_t *nrzi_bits, size_t num_bits,
                   uint8_t *out, size_t out_size,
                   int initial_state)
{
    if (num_bits > out_size) return 0;
    int prev = initial_state ? 1 : 0;
    for (size_t i = 0; i < num_bits; i++) {
        out[i] = (nrzi_bits[i] == prev) ? 1 : 0;
        prev = nrzi_bits[i];
    }
    return num_bits;
}
