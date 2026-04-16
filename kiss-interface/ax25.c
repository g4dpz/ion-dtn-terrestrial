/*
 * ax25.c — AX.25 UI frame construction and parsing
 *
 * Implements callsign address encoding/decoding and AX.25 Unnumbered
 * Information (UI) frame building/stripping for amateur radio use.
 */

#include "ax25.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* ax25_encode_addr                                                    */
/* ------------------------------------------------------------------ */
int ax25_encode_addr(const char *callsign, uint8_t out[AX25_ADDR_LEN], int last)
{
    if (!callsign || !out)
        return -1;

    /* Parse callsign and SSID from "CALL" or "CALL-SSID" */
    char call[7];  /* up to 6 chars + NUL */
    int  ssid = 0;

    const char *dash = strchr(callsign, '-');
    size_t call_len;

    if (dash) {
        call_len = (size_t)(dash - callsign);
        if (call_len == 0 || call_len > 6)
            return -1;
        ssid = atoi(dash + 1);
        if (ssid < 0 || ssid > 15)
            return -1;
    } else {
        call_len = strlen(callsign);
        if (call_len == 0 || call_len > 6)
            return -1;
    }

    /* Copy and uppercase */
    for (size_t i = 0; i < call_len; i++)
        call[i] = (char)toupper((unsigned char)callsign[i]);
    call[call_len] = '\0';

    /* Bytes 0-5: callsign characters left-shifted by 1, space-padded */
    for (int i = 0; i < 6; i++) {
        char c = (i < (int)call_len) ? call[i] : ' ';
        out[i] = (uint8_t)(c << 1);
    }

    /* Byte 6: bit 7=0, bits 6:5=11 (reserved=0x60), bits 4:1=SSID, bit 0=extension */
    out[6] = 0x60 | ((uint8_t)(ssid & 0x0F) << 1) | (last ? 1 : 0);

    return 0;
}

/* ------------------------------------------------------------------ */
/* ax25_decode_addr                                                    */
/* ------------------------------------------------------------------ */
int ax25_decode_addr(const uint8_t addr[AX25_ADDR_LEN], char *out, size_t out_size)
{
    if (!addr || !out || out_size < 1)
        return -1;

    /* Extract callsign: right-shift bytes 0-5, trim trailing spaces */
    char call[7];
    int  call_len = 0;

    for (int i = 0; i < 6; i++) {
        char c = (char)(addr[i] >> 1);
        call[i] = c;
        if (c != ' ')
            call_len = i + 1;
    }
    call[call_len] = '\0';

    /* Extract SSID from bits 4:1 of byte 6 */
    int ssid = (addr[6] >> 1) & 0x0F;

    /* Format as "CALL-SSID" (always include SSID, even if 0) */
    int written = snprintf(out, out_size, "%s-%d", call, ssid);
    if (written < 0 || (size_t)written >= out_size)
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/* ax25_build_frame                                                    */
/* ------------------------------------------------------------------ */
int ax25_build_frame(const char *dst_call, const char *src_call,
                     const uint8_t *info, size_t info_len,
                     uint8_t *out, size_t out_size)
{
    if (!dst_call || !src_call || !out)
        return -1;
    if (info_len > 0 && !info)
        return -1;
    if (info_len > AX25_MAX_INFO)
        return -1;

    size_t total = AX25_HDR_LEN + info_len;
    if (out_size < total)
        return -1;

    /* Destination address (not last → extension bit = 0) */
    if (ax25_encode_addr(dst_call, out, 0) != 0)
        return -1;

    /* Source address (last → extension bit = 1) */
    if (ax25_encode_addr(src_call, out + AX25_ADDR_LEN, 1) != 0)
        return -1;

    /* Control byte: UI frame */
    out[14] = AX25_CTRL_UI;

    /* PID byte: no layer 3 */
    out[15] = AX25_PID_NOLAYER3;

    /* Information field */
    if (info_len > 0)
        memcpy(out + AX25_HDR_LEN, info, info_len);

    return (int)total;
}

/* ------------------------------------------------------------------ */
/* ax25_strip_frame                                                    */
/* ------------------------------------------------------------------ */
int ax25_strip_frame(const uint8_t *frame, size_t frame_len,
                     char *src_call, char *dst_call,
                     const uint8_t **info)
{
    if (!frame)
        return -1;

    /* Minimum AX.25 UI header: 7 dst + 7 src + 1 ctrl + 1 pid = 16 */
    if (frame_len < AX25_HDR_LEN)
        return -1;

    /* Verify control byte */
    if (frame[14] != AX25_CTRL_UI)
        return -1;

    /* Verify PID byte */
    if (frame[15] != AX25_PID_NOLAYER3)
        return -1;

    /* Decode destination callsign.
     * Note: caller buffer must be at least AX25_MAX_CALLSIGN bytes.
     * We decode into a local buffer first to handle the case where
     * AX25_MAX_CALLSIGN is marginally too small for 6-char + 2-digit SSID. */
    if (dst_call) {
        char tmp[12];
        if (ax25_decode_addr(frame, tmp, sizeof(tmp)) != 0)
            return -1;
        /* Copy as much as fits; always NUL-terminate */
        size_t slen = strlen(tmp);
        memcpy(dst_call, tmp, slen + 1);
    }

    /* Decode source callsign */
    if (src_call) {
        char tmp[12];
        if (ax25_decode_addr(frame + AX25_ADDR_LEN, tmp, sizeof(tmp)) != 0)
            return -1;
        size_t slen = strlen(tmp);
        memcpy(src_call, tmp, slen + 1);
    }

    /* Point to info field */
    if (info)
        *info = frame + AX25_HDR_LEN;

    return (int)(frame_len - AX25_HDR_LEN);
}
