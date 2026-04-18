/*
 * nrzi.h — NRZI encoding, CRC-CCITT FCS, bit stuffing, flag framing
 *
 * Converts AX.25 frame bytes into a complete bitstream ready for
 * AFSK modulation: FCS → byte-to-bit → bit stuff → flag frame → NRZI.
 * Static allocation only.
 */

#ifndef NRZI_H
#define NRZI_H

#include <stdint.h>
#include <stddef.h>

/* Maximum AX.25 frame bytes (header + info + FCS) */
#define NRZI_MAX_FRAME_BYTES  330
/* Max bits: frame_bytes*8 + worst-case stuffing + flags */
#define NRZI_MAX_BITS         4096

#define NRZI_PREAMBLE_FLAGS   80
#define NRZI_CLOSING_FLAGS    3
#define NRZI_FLAG             0x7E

/* CRC-CCITT parameters */
#define NRZI_CRC_POLY  0x8408
#define NRZI_CRC_INIT  0xFFFF

/* Compute CRC-CCITT FCS over data.
 * Polynomial 0x8408 (reflected), init 0xFFFF, final XOR 0xFFFF.
 * Returns 16-bit FCS value. */
uint16_t nrzi_compute_fcs(const uint8_t *data, size_t len);

/* Convert frame bytes to bits (LSB-first per byte).
 * out must hold at least len*8 elements.
 * Returns number of bits written. */
size_t nrzi_bytes_to_bits(const uint8_t *data, size_t len,
                          uint8_t *out, size_t out_size);

/* Apply AX.25 bit stuffing: insert 0 after every 5 consecutive 1-bits.
 * Returns number of output bits, or -1 on overflow. */
int nrzi_bit_stuff(const uint8_t *bits, size_t num_bits,
                   uint8_t *out, size_t out_size);

/* Remove AX.25 bit stuffing (inverse of nrzi_bit_stuff).
 * Returns number of output bits, or -1 on error. */
int nrzi_bit_destuff(const uint8_t *bits, size_t num_bits,
                     uint8_t *out, size_t out_size);

/* Build complete flag-framed bitstream from AX.25 frame bytes.
 * Computes FCS, converts to bits, applies bit stuffing, adds flags.
 * Applies SSID reserved-bit fix (0xE0 mask) before FCS computation.
 * out must hold at least NRZI_MAX_BITS elements.
 * Returns total number of bits, or -1 on error. */
int nrzi_frame_to_bitstream(const uint8_t *frame, size_t frame_len,
                            uint8_t *out, size_t out_size);

/* NRZI-encode a bitstream. 0-bit toggles state, 1-bit holds.
 * initial_state: 0 (space) or 1 (mark).
 * out must hold at least num_bits elements.
 * Returns number of output bits. */
size_t nrzi_encode(const uint8_t *bits, size_t num_bits,
                   uint8_t *out, size_t out_size,
                   int initial_state);

/* NRZI-decode a bitstream (inverse of nrzi_encode).
 * Transition -> 0, no transition -> 1.
 * Returns number of output bits. */
size_t nrzi_decode(const uint8_t *nrzi_bits, size_t num_bits,
                   uint8_t *out, size_t out_size,
                   int initial_state);

#endif
