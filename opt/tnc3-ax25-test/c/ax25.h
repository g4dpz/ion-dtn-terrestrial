#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stddef.h>

#define AX25_ADDR_LEN 7
#define AX25_MAX_FRAME 512

typedef struct {
    char callsign[7];  // 6 chars + null terminator
    uint8_t ssid;
} ax25_address_t;

// Function prototypes
int create_ax25_ui_frame(const char *dest_call, const char *src_call, 
                         const uint8_t *payload, size_t payload_len,
                         uint8_t *output, size_t output_size);

int parse_ax25_ui_frame(const uint8_t *frame, size_t frame_len,
                        char *dest_call, char *src_call,
                        uint8_t *payload, size_t payload_size);

void callsign_to_ax25(const char *callsign, uint8_t *output);
void ax25_to_callsign(const uint8_t *ax25_addr, char *callsign);

#endif // AX25_H
