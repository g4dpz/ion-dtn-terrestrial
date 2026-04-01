#ifndef KISS_H
#define KISS_H

#include <stdint.h>
#include <stddef.h>

// KISS special characters
#define FEND  0xC0  // Frame End
#define FESC  0xDB  // Frame Escape
#define TFEND 0xDC  // Transposed Frame End
#define TFESC 0xDD  // Transposed Frame Escape

// KISS commands
#define CMD_DATA        0x00
#define CMD_TXDELAY     0x01
#define CMD_PERSISTENCE 0x02
#define CMD_SLOTTIME    0x03
#define CMD_TXTAIL      0x04
#define CMD_FULLDUPLEX  0x05
#define CMD_SETHARDWARE 0x06
#define CMD_RETURN      0xFF

// Function prototypes
int encode_kiss_frame(const uint8_t *data, size_t data_len, uint8_t *output, size_t output_size);
int decode_kiss_frame(const uint8_t *frame, size_t frame_len, uint8_t *output, size_t output_size);

#endif // KISS_H
