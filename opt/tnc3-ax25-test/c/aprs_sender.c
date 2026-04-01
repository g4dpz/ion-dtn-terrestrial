#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <math.h>
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
    
    // Set baud rate
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    
    // 8N1, raw mode
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

void create_aprs_position(double lat, double lon, const char *comment, char *output) {
    int lat_deg = (int)fabs(lat);
    double lat_min = (fabs(lat) - lat_deg) * 60.0;
    char lat_dir = (lat >= 0) ? 'N' : 'S';
    
    int lon_deg = (int)fabs(lon);
    double lon_min = (fabs(lon) - lon_deg) * 60.0;
    char lon_dir = (lon >= 0) ? 'E' : 'W';
    
    sprintf(output, "!%02d%05.2f%c/%03d%05.2f%c>%s",
            lat_deg, lat_min, lat_dir,
            lon_deg, lon_min, lon_dir,
            comment);
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        printf("Usage: %s <device> <src_call> <dest_call> <lat> <lon> [comment] [interval]\n", argv[0]);
        printf("Example: %s /dev/tty.usbmodem123 G4DPZ-1 APRS 51.5074 -0.1278 \"Test\" 30\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    const char *src_call = argv[2];
    const char *dest_call = argv[3];
    double lat = atof(argv[4]);
    double lon = atof(argv[5]);
    const char *comment = (argc > 6) ? argv[6] : "";
    int interval = (argc > 7) ? atoi(argv[7]) : 30;
    
    printf("APRS Sender (C)\n");
    printf("Device: %s\n", device);
    printf("Source: %s\n", src_call);
    printf("Destination: %s\n", dest_call);
    printf("Position: %.4f, %.4f\n", lat, lon);
    printf("Comment: %s\n", comment);
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
    
    int count = 1;
    while (1) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        // Create APRS position packet
        char aprs_data[256];
        create_aprs_position(lat, lon, comment, aprs_data);
        
        // Create AX.25 UI frame
        uint8_t ax25_frame[512];
        int ax25_len = create_ax25_ui_frame(dest_call, src_call,
                                            (uint8_t*)aprs_data, strlen(aprs_data),
                                            ax25_frame, sizeof(ax25_frame));
        
        if (ax25_len < 0) {
            fprintf(stderr, "Error creating AX.25 frame\n");
            break;
        }
        
        // Encode in KISS
        uint8_t kiss_frame[1024];
        int kiss_len = encode_kiss_frame(ax25_frame, ax25_len,
                                         kiss_frame, sizeof(kiss_frame));
        
        if (kiss_len < 0) {
            fprintf(stderr, "Error encoding KISS frame\n");
            break;
        }
        
        // Send
        write(fd, kiss_frame, kiss_len);
        printf("[%s] Sent beacon #%d: %s (%d bytes)\n",
               timestamp, count, aprs_data, kiss_len);
        
        count++;
        sleep(interval);
    }
    
    close(fd);
    return 0;
}
