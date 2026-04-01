#include "kiss.h"
#include <string.h>

static size_t escape_kiss(const uint8_t *data, size_t len, uint8_t *output, size_t output_size) {
    size_t out_idx = 0;
    
    for (size_t i = 0; i < len && out_idx < output_size - 1; i++) {
        if (data[i] == FEND) {
            if (out_idx + 2 > output_size) break;
            output[out_idx++] = FESC;
            output[out_idx++] = TFEND;
        } else if (data[i] == FESC) {
            if (out_idx + 2 > output_size) break;
            output[out_idx++] = FESC;
            output[out_idx++] = TFESC;
        } else {
            output[out_idx++] = data[i];
        }
    }
    
    return out_idx;
}

int encode_kiss_frame(const uint8_t *data, size_t data_len, uint8_t *output, size_t output_size) {
    if (output_size < 3) return -1;
    
    size_t idx = 0;
    output[idx++] = FEND;
    output[idx++] = CMD_DATA;
    
    size_t escaped_len = escape_kiss(data, data_len, output + idx, output_size - idx - 1);
    idx += escaped_len;
    
    if (idx >= output_size) return -1;
    output[idx++] = FEND;
    
    return idx;
}

int decode_kiss_frame(const uint8_t *frame, size_t frame_len, uint8_t *output, size_t output_size) {
    if (frame_len < 3 || frame[0] != FEND || frame[frame_len - 1] != FEND) {
        return -1;
    }
    
    uint8_t cmd = frame[1];
    if ((cmd & 0x0F) != CMD_DATA) {
        return -1;
    }
    
    size_t out_idx = 0;
    for (size_t i = 2; i < frame_len - 1 && out_idx < output_size; i++) {
        if (frame[i] == FESC) {
            if (i + 1 < frame_len - 1) {
                if (frame[i + 1] == TFEND) {
                    output[out_idx++] = FEND;
                    i++;
                } else if (frame[i + 1] == TFESC) {
                    output[out_idx++] = FESC;
                    i++;
                }
            }
        } else {
            output[out_idx++] = frame[i];
        }
    }
    
    return out_idx;
}
