#include "ax25.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

void callsign_to_ax25(const char *callsign, uint8_t *output) {
    char call[7] = "      ";  // 6 spaces
    uint8_t ssid = 0;
    
    // Parse callsign and SSID
    const char *dash = strchr(callsign, '-');
    if (dash) {
        size_t call_len = dash - callsign;
        if (call_len > 6) call_len = 6;
        memcpy(call, callsign, call_len);
        ssid = atoi(dash + 1) & 0x0F;
    } else {
        size_t call_len = strlen(callsign);
        if (call_len > 6) call_len = 6;
        memcpy(call, callsign, call_len);
    }
    
    // Convert to uppercase and encode (shift left by 1)
    for (int i = 0; i < 6; i++) {
        output[i] = toupper(call[i]) << 1;
    }
    
    // SSID byte: C R R SSID SSID SSID SSID 0
    // C=1 for last address, R=reserved (11)
    output[6] = 0b01100000 | ((ssid & 0x0F) << 1);
}

void ax25_to_callsign(const uint8_t *ax25_addr, char *callsign) {
    char call[7];
    
    // Decode callsign (shift right by 1)
    for (int i = 0; i < 6; i++) {
        call[i] = ax25_addr[i] >> 1;
    }
    call[6] = '\0';
    
    // Remove trailing spaces
    for (int i = 5; i >= 0; i--) {
        if (call[i] == ' ') {
            call[i] = '\0';
        } else {
            break;
        }
    }
    
    // Decode SSID
    uint8_t ssid = (ax25_addr[6] >> 1) & 0x0F;
    
    if (ssid > 0) {
        sprintf(callsign, "%s-%d", call, ssid);
    } else {
        strcpy(callsign, call);
    }
}

int create_ax25_ui_frame(const char *dest_call, const char *src_call,
                         const uint8_t *payload, size_t payload_len,
                         uint8_t *output, size_t output_size) {
    if (output_size < 16 + payload_len) {
        return -1;
    }
    
    size_t idx = 0;
    
    // Destination address
    uint8_t dest_addr[7];
    callsign_to_ax25(dest_call, dest_addr);
    memcpy(output + idx, dest_addr, 6);
    idx += 6;
    output[idx++] = dest_addr[6] & 0b11111110;  // Clear C bit
    
    // Source address
    uint8_t src_addr[7];
    callsign_to_ax25(src_call, src_addr);
    memcpy(output + idx, src_addr, 6);
    idx += 6;
    output[idx++] = src_addr[6] | 0b00000001;  // Set C bit (last address)
    
    // Control field: UI frame (0x03)
    output[idx++] = 0x03;
    
    // Protocol ID: No layer 3 (0xF0)
    output[idx++] = 0xF0;
    
    // Payload
    memcpy(output + idx, payload, payload_len);
    idx += payload_len;
    
    return idx;
}

int parse_ax25_ui_frame(const uint8_t *frame, size_t frame_len,
                        char *dest_call, char *src_call,
                        uint8_t *payload, size_t payload_size) {
    if (frame_len < 16) {
        return -1;
    }
    
    // Parse destination address
    ax25_to_callsign(frame, dest_call);
    
    // Parse source address
    ax25_to_callsign(frame + 7, src_call);
    
    // Check control field (should be 0x03 for UI)
    if (frame[14] != 0x03) {
        return -1;
    }
    
    // Extract payload
    size_t payload_len = frame_len - 16;
    if (payload_len > payload_size) {
        payload_len = payload_size;
    }
    
    memcpy(payload, frame + 16, payload_len);
    
    return payload_len;
}
