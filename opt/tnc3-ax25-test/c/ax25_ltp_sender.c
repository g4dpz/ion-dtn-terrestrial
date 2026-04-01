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

// Simple LTP segment structure (minimal implementation)
typedef struct {
    uint8_t version;        // LTP version (0)
    uint8_t type;           // Segment type
    uint64_t session_id;    // Session ID
    uint32_t client_id;     // Client service ID
    uint32_t offset;        // Data offset
    uint32_t length;        // Data length
    uint8_t data[256];      // Payload data
} ltp_segment_t;

#define LTP_DATA_SEGMENT 0x00
#define LTP_REPORT_SEGMENT 0x01
#define LTP_REPORT_ACK_SEGMENT 0x02

int open_serial(const char *device) {
    /* O_NONBLOCK needed on macOS to prevent open() from blocking
       on some USB serial devices until carrier detect */
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Clear O_NONBLOCK after open so read/write behave normally */
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

    /* Flush any stale data in the serial buffers */
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

int encode_ltp_segment(const ltp_segment_t *seg, uint8_t *output, size_t output_size) {
    if (output_size < 32 + seg->length) {
        return -1;
    }
    
    size_t idx = 0;
    
    // Version and type (1 byte)
    output[idx++] = (seg->version << 4) | (seg->type & 0x0F);
    
    // Session ID (8 bytes, big-endian)
    for (int i = 7; i >= 0; i--) {
        output[idx++] = (seg->session_id >> (i * 8)) & 0xFF;
    }
    
    // Client ID (4 bytes, big-endian)
    for (int i = 3; i >= 0; i--) {
        output[idx++] = (seg->client_id >> (i * 8)) & 0xFF;
    }
    
    // Offset (4 bytes, big-endian)
    for (int i = 3; i >= 0; i--) {
        output[idx++] = (seg->offset >> (i * 8)) & 0xFF;
    }
    
    // Length (4 bytes, big-endian)
    for (int i = 3; i >= 0; i--) {
        output[idx++] = (seg->length >> (i * 8)) & 0xFF;
    }
    
    // Data
    memcpy(output + idx, seg->data, seg->length);
    idx += seg->length;
    
    return idx;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s <device> <src_call> <dest_call> <message> [interval]\n", argv[0]);
        printf("Example: %s /dev/tty.usbmodem123 G4DPZ-1 G4DPZ-2 \"Hello LTP\" 5\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    const char *src_call = argv[2];
    const char *dest_call = argv[3];
    const char *message = argv[4];
    int interval = (argc > 5) ? atoi(argv[5]) : 5;
    
    printf("LTP Test Sender\n");
    printf("Device: %s\n", device);
    printf("Source: %s\n", src_call);
    printf("Destination: %s\n", dest_call);
    printf("Message: %s\n", message);
    printf("Interval: %d seconds\n\n", interval);
    
    int fd = open_serial(device);
    if (fd < 0) {
        return 1;
    }
    
    printf("Connected to %s\n", device);
    
    // Send KISS initialization
    uint8_t kiss_init[] = {FEND, FEND};
    write(fd, kiss_init, sizeof(kiss_init));
    usleep(500000);
    
    uint64_t session_id = 1;
    
    while (1) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        // Create LTP segment
        ltp_segment_t seg;
        seg.version = 0;
        seg.type = LTP_DATA_SEGMENT;
        seg.session_id = session_id;
        seg.client_id = 1;
        seg.offset = 0;
        seg.length = strlen(message);
        memcpy(seg.data, message, seg.length);
        
        // Encode LTP segment
        uint8_t ltp_data[512];
        int ltp_len = encode_ltp_segment(&seg, ltp_data, sizeof(ltp_data));
        
        if (ltp_len < 0) {
            fprintf(stderr, "Error encoding LTP segment\n");
            break;
        }
        
        // Wrap LTP segment in AX.25 UI frame (with callsigns for legal ID)
        uint8_t ax25_frame[1024];
        int ax25_len = create_ax25_ui_frame(dest_call, src_call,
                                            ltp_data, ltp_len,
                                            ax25_frame, sizeof(ax25_frame));
        if (ax25_len < 0) {
            fprintf(stderr, "Error creating AX.25 frame\n");
            break;
        }

        // Encode AX.25 frame in KISS
        uint8_t kiss_frame[2048];
        int kiss_len = encode_kiss_frame(ax25_frame, ax25_len, kiss_frame, sizeof(kiss_frame));
        
        if (kiss_len < 0) {
            fprintf(stderr, "Error encoding KISS frame\n");
            break;
        }
        
        // Send
        write(fd, kiss_frame, kiss_len);
        tcdrain(fd);  /* ensure data is actually transmitted */
        
        // Hex dump
        printf("[%s] Sent LTP segment #%llu: \"%s\" (%d bytes)\n",
               timestamp, session_id, message, kiss_len);
        printf("  KISS frame (hex): ");
        for (int i = 0; i < kiss_len && i < 64; i++) {
            printf("%02X ", kiss_frame[i]);
        }
        if (kiss_len > 64) printf("...");
        printf("\n");
        printf("  LTP data (hex):   ");
        for (int i = 0; i < ltp_len && i < 64; i++) {
            printf("%02X ", ltp_data[i]);
        }
        if (ltp_len > 64) printf("...");
        printf("\n\n");
        
        session_id++;
        sleep(interval);
    }
    
    close(fd);
    return 0;
}
