#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include "kiss.h"
#include "bp.h"
#include "ax25.h"

#define LTP_DATA_SEGMENT 0x00

typedef struct {
    uint8_t version;
    uint8_t type;
    uint64_t session_id;
    uint32_t client_id;
    uint32_t offset;
    uint32_t length;
    uint8_t data[1024];
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

int encode_ltp_segment(const ltp_segment_t *seg, uint8_t *output, size_t output_size) {
    if (output_size < 32 + seg->length) return -1;
    
    size_t idx = 0;
    output[idx++] = (seg->version << 4) | (seg->type & 0x0F);
    
    for (int i = 7; i >= 0; i--) output[idx++] = (seg->session_id >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; i--) output[idx++] = (seg->client_id >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; i--) output[idx++] = (seg->offset >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; i--) output[idx++] = (seg->length >> (i * 8)) & 0xFF;
    
    memcpy(output + idx, seg->data, seg->length);
    idx += seg->length;
    
    return idx;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        printf("Usage: %s <device> <src_call> <dest_call> <dest_eid> <src_eid> <message> [interval]\n", argv[0]);
        printf("Example: %s /dev/tty.usbmodem123 G4DPZ-1 G4DPZ-2 dtn://node2/test dtn://node1/app \"Hello BP\" 5\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    const char *src_call = argv[2];
    const char *dest_call = argv[3];
    const char *dest_eid = argv[4];
    const char *src_eid = argv[5];
    const char *message = argv[6];
    int interval = (argc > 7) ? atoi(argv[7]) : 5;
    
    printf("Bundle Protocol over LTP Sender\n");
    printf("Device: %s\n", device);
    printf("Source Call: %s\n", src_call);
    printf("Dest Call: %s\n", dest_call);
    printf("Destination EID: %s\n", dest_eid);
    printf("Source EID: %s\n", src_eid);
    printf("Message: %s\n", message);
    printf("Interval: %d seconds\n\n", interval);
    
    int fd = open_serial(device);
    if (fd < 0) return 1;
    
    printf("Connected to %s\n\n", device);
    
    uint8_t kiss_init[] = {FEND, FEND};
    write(fd, kiss_init, sizeof(kiss_init));
    usleep(500000);
    
    uint64_t session_id = 1;
    
    while (1) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        // Create Bundle Protocol bundle
        bp_bundle_t bundle;
        create_simple_bundle(&bundle, dest_eid, src_eid, 
                           (uint8_t*)message, strlen(message));
        
        // Encode bundle
        uint8_t bp_data[1024];
        int bp_len = encode_bp_bundle(&bundle, bp_data, sizeof(bp_data));
        
        if (bp_len < 0) {
            fprintf(stderr, "Error encoding BP bundle\n");
            break;
        }
        
        printf("[%s] Created BP Bundle (%d bytes)\n", timestamp, bp_len);
        printf("  Dest: %s\n", bundle.dest_eid);
        printf("  Src: %s\n", bundle.src_eid);
        printf("  Payload: \"%s\"\n", message);
        printf("  BP data (hex): ");
        for (int i = 0; i < bp_len && i < 64; i++) {
            printf("%02X ", bp_data[i]);
        }
        if (bp_len > 64) printf("...");
        printf("\n");
        
        // Wrap in LTP segment
        ltp_segment_t seg;
        seg.version = 0;
        seg.type = LTP_DATA_SEGMENT;
        seg.session_id = session_id;
        seg.client_id = 1; // BP client service
        seg.offset = 0;
        seg.length = bp_len;
        memcpy(seg.data, bp_data, bp_len);
        
        // Encode LTP
        uint8_t ltp_data[2048];
        int ltp_len = encode_ltp_segment(&seg, ltp_data, sizeof(ltp_data));
        
        if (ltp_len < 0) {
            fprintf(stderr, "Error encoding LTP segment\n");
            break;
        }
        
        // Wrap LTP in AX.25 UI frame (callsign identification)
        uint8_t ax25_frame[4096];
        int ax25_len = create_ax25_ui_frame(dest_call, src_call,
                                            ltp_data, ltp_len,
                                            ax25_frame, sizeof(ax25_frame));
        if (ax25_len < 0) {
            fprintf(stderr, "Error creating AX.25 frame\n");
            break;
        }

        // Encode in KISS
        uint8_t kiss_frame[4096];
        int kiss_len = encode_kiss_frame(ax25_frame, ax25_len, kiss_frame, sizeof(kiss_frame));
        
        if (kiss_len < 0) {
            fprintf(stderr, "Error encoding KISS frame\n");
            break;
        }
        
        // Send
        write(fd, kiss_frame, kiss_len);
        tcdrain(fd);
        printf("  Sent LTP segment #%llu (%d bytes KISS)\n", session_id, kiss_len);
        printf("  KISS frame (hex): ");
        for (int i = 0; i < kiss_len && i < 64; i++) {
            printf("%02X ", kiss_frame[i]);
        }
        if (kiss_len > 64) printf("...");
        printf("\n\n");
        
        session_id++;
        sleep(interval);
    }
    
    close(fd);
    return 0;
}
