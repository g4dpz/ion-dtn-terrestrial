#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stddef.h>
#include "kiss.h"

#define AX25_ADDR_LEN    7
#define AX25_HDR_LEN     16  /* 7 dst + 7 src + 1 ctrl + 1 pid */
#define AX25_CTRL_UI     0x03
#define AX25_PID_NOLAYER3 0xF0
#define AX25_MAX_CALLSIGN 9  /* "ABCDEF-15" + NUL */
#define AX25_MAX_INFO    (KISS_MAX_PAYLOAD - AX25_HDR_LEN)

/* Encode a callsign string (e.g. "G4DPZ-1") into a 7-byte AX.25 address field.
 * last: set to 1 for the final address field (sets extension bit). */
int ax25_encode_addr(const char *callsign, uint8_t out[AX25_ADDR_LEN], int last);

/* Decode a 7-byte AX.25 address field back to a callsign string.
 * out must be at least AX25_MAX_CALLSIGN bytes. */
int ax25_decode_addr(const uint8_t addr[AX25_ADDR_LEN], char *out, size_t out_size);

/* Build a complete AX.25 UI frame. Returns total frame length, or -1 on error. */
int ax25_build_frame(const char *dst_call, const char *src_call,
                     const uint8_t *info, size_t info_len,
                     uint8_t *out, size_t out_size);

/* Strip AX.25 header from a frame. Returns info field length, or -1 on error.
 * Sets *src_call and *dst_call if non-NULL (must be AX25_MAX_CALLSIGN bytes).
 * Sets *info to point into frame buffer at the info field start. */
int ax25_strip_frame(const uint8_t *frame, size_t frame_len,
                     char *src_call, char *dst_call,
                     const uint8_t **info);

#endif
