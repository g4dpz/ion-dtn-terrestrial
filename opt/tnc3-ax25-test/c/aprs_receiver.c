#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include "kiss.h"
#include "ax25.h"

int open_serial(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
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
    
    return fd;
}

const char* parse_aprs_type(const char *data) {
    if (!data || !data[0]) return "unknown";
    
    switch (data[0]) {
        case '!': return "position";
        case '=': return "position_msg";
        case '/': return "position_time";
        case '@': return "position_time_msg";
        case ':': return "message";
        case '>': return "status";
        case 'T': return "telemetry";
        case '_': return "weather";
        default: return "unknown";
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <device> <callsign>\n", argv[0]);
        printf("Example: %s /dev/tty.usbmodem123 G4DPZ-2\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    const char *callsign = argv[2];
    
    printf("APRS Receiver (C)\n");
    printf("Device: %s\n", device);
    printf("Callsign: %s\n\n", callsign);
    
    int fd = open_serial(device);
    if (fd < 0) {
        return 1;
    }
    
    printf("Connected to %s\n", device);
    printf("Waiting for incoming frames...\n\n");
    
    uint8_t buffer[4096];
    size_t buf_len = 0;
    
    while (1) {
        uint8_t byte;
        int n = read(fd, &byte, 1);
        
        if (n > 0) {
            buffer[buf_len++] = byte;
            
            // Look for complete KISS frame (FEND...FEND)
            if (byte == FEND && buf_len > 2) {
                // Try to decode frame
                uint8_t ax25_data[512];
                int ax25_len = decode_kiss_frame(buffer, buf_len, ax25_data, sizeof(ax25_data));
                
                if (ax25_len > 0) {
                    // Parse AX.25 frame
                    char dest_call[16], src_call[16];
                    uint8_t payload[512];
                    int payload_len = parse_ax25_ui_frame(ax25_data, ax25_len,
                                                          dest_call, src_call,
                                                          payload, sizeof(payload));
                    
                    if (payload_len > 0) {
                        payload[payload_len] = '\0';
                        
                        time_t now = time(NULL);
                        struct tm *tm_info = localtime(&now);
                        char timestamp[32];
                        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                        
                        const char *aprs_type = parse_aprs_type((char*)payload);
                        
                        if (strcmp(aprs_type, "unknown") != 0) {
                            printf("[%s] APRS %s from %s to %s\n",
                                   timestamp, aprs_type, src_call, dest_call);
                            printf("  Data: %s\n", payload);
                        } else {
                            printf("[%s] From %s to %s: %s\n",
                                   timestamp, src_call, dest_call, payload);
                        }
                    }
                }
                
                // Reset buffer
                buf_len = 0;
            }
            
            // Prevent buffer overflow
            if (buf_len >= sizeof(buffer) - 1) {
                buf_len = 0;
            }
        }
    }
    
    close(fd);
    return 0;
}
