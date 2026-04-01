#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include "kiss.h"
#include "ax25.h"

typedef struct {
    uint8_t version;
    uint8_t type;
    uint64_t session_id;
    uint32_t client_id;
    uint32_t offset;
    uint32_t length;
    uint8_t data[256];
} ltp_segment_t;

int open_serial(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }
    
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CLOCAL | CREAD;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

int decode_ltp_segment(const uint8_t *data, size_t data_len, ltp_segment_t *seg) {
    if (data_len < 21) {
        return -1;
    }
    
    size_t idx = 0;
    
    // Version and type
    seg->version = (data[idx] >> 4) & 0x0F;
    seg->type = data[idx] & 0x0F;
    idx++;
    
    // Session ID (8 bytes, big-endian)
    seg->session_id = 0;
    for (int i = 0; i < 8; i++) {
        seg->session_id = (seg->session_id << 8) | data[idx++];
    }
    
    // Client ID (4 bytes, big-endian)
    seg->client_id = 0;
    for (int i = 0; i < 4; i++) {
        seg->client_id = (seg->client_id << 8) | data[idx++];
    }
    
    // Offset (4 bytes, big-endian)
    seg->offset = 0;
    for (int i = 0; i < 4; i++) {
        seg->offset = (seg->offset << 8) | data[idx++];
    }
    
    // Length (4 bytes, big-endian)
    seg->length = 0;
    for (int i = 0; i < 4; i++) {
        seg->length = (seg->length << 8) | data[idx++];
    }
    
    // Data
    if (seg->length > sizeof(seg->data)) {
        seg->length = sizeof(seg->data);
    }
    
    if (idx + seg->length > data_len) {
        return -1;
    }
    
    memcpy(seg->data, data + idx, seg->length);
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <device>\n", argv[0]);
        printf("Example: %s /dev/tty.usbmodem123\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    
    printf("LTP Test Receiver\n");
    printf("Device: %s\n\n", device);
    
    int fd = open_serial(device);
    if (fd < 0) {
        return 1;
    }
    
    printf("Connected to %s\n", device);
    printf("Waiting for LTP segments...\n\n");
    
    uint8_t buffer[4096];
    size_t buf_len = 0;
    
    while (1) {
        uint8_t byte;
        int n = read(fd, &byte, 1);
        
        if (n > 0) {
            buffer[buf_len++] = byte;
            
            // Look for complete KISS frame
            if (byte == FEND && buf_len > 2) {
                uint8_t ltp_data[512];
                int ltp_len = decode_kiss_frame(buffer, buf_len, ltp_data, sizeof(ltp_data));
                
                if (ltp_len > 0) {
                    // Parse AX.25 UI frame to extract callsigns
                    char dest_call[16], src_call[16];
                    uint8_t ax25_payload[512];
                    int payload_len = parse_ax25_ui_frame(ltp_data, ltp_len,
                                                          dest_call, src_call,
                                                          ax25_payload, sizeof(ax25_payload));

                    const uint8_t *seg_data;
                    int seg_len;

                    if (payload_len > 0) {
                        // AX.25 frame with callsigns
                        seg_data = ax25_payload;
                        seg_len = payload_len;
                    } else {
                        // Raw LTP (no AX.25 wrapper, backward compatible)
                        seg_data = ltp_data;
                        seg_len = ltp_len;
                        strcpy(src_call, "???");
                        strcpy(dest_call, "???");
                    }

                    ltp_segment_t seg;
                    if (decode_ltp_segment(seg_data, seg_len, &seg) == 0) {
                        time_t now = time(NULL);
                        struct tm *tm_info = localtime(&now);
                        char timestamp[32];
                        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                        
                        seg.data[seg.length] = '\0';
                        
                        printf("[%s] LTP Segment Received (%d bytes)\n", timestamp, ltp_len);
                        printf("  From: %s  To: %s\n", src_call, dest_call);
                        printf("  KISS frame (hex): ");
                        for (size_t i = 0; i < buf_len && i < 64; i++) {
                            printf("%02X ", buffer[i]);
                        }
                        if (buf_len > 64) printf("...");
                        printf("\n");
                        printf("  LTP data (hex):   ");
                        for (int i = 0; i < ltp_len && i < 64; i++) {
                            printf("%02X ", ltp_data[i]);
                        }
                        if (ltp_len > 64) printf("...");
                        printf("\n");
                        printf("  Session ID: %llu\n", seg.session_id);
                        printf("  Client ID: %u\n", seg.client_id);
                        printf("  Type: %u\n", seg.type);
                        printf("  Offset: %u\n", seg.offset);
                        printf("  Length: %u\n", seg.length);
                        printf("  Data: \"%s\"\n\n", seg.data);
                    }
                }
                
                buf_len = 0;
            }
            
            if (buf_len >= sizeof(buffer) - 1) {
                buf_len = 0;
            }
        }
    }
    
    close(fd);
    return 0;
}
