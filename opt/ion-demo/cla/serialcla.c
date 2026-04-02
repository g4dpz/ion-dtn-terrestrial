/*
 * serialcla.c - Combined serial AX.25/KISS CLA for ION-DTN LTP
 *
 * Single process handling both TX and RX on one serial device.
 * Required for half-duplex radio where both seriallso and seriallsi
 * need to share the same serial port.
 *
 * TX: Receives LTP segments via UDP from udplso, wraps in AX.25/KISS,
 *     writes to serial device.
 * RX: Reads KISS frames from serial device, strips AX.25, forwards
 *     LTP segments via UDP to udplsi.
 *
 * Usage: serialcla <device>:<baud> <src_call> <dest_call> <tx_port> <rx_port> [burst:pause]
 * Example: serialcla /dev/ttyACM0:9600 G4DPZ-1 G4DPZ-2 1114 1113 2:30
 *
 *   tx_port: UDP port to listen on (receives from udplso)
 *   rx_port: UDP port to forward to (sends to udplsi)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define MAX_SEGMENT  2048
#define AX25_HDR     16
#define MAX_AX25     (AX25_HDR + MAX_SEGMENT)
#define MAX_KISS     (MAX_AX25 * 2 + 4)
#define SERIAL_BUF   4096

static volatile int running = 1;
static void handle_signal(int sig) { (void)sig; running = 0; }

/* AX.25 encode/decode */
static void encode_ax25_addr(const char *call, uint8_t *out, int last) {
    char c[7] = "      "; uint8_t ssid = 0;
    const char *d = strchr(call, '-');
    if (d) { size_t l = d-call; if(l>6)l=6; memcpy(c,call,l); ssid=atoi(d+1)&0xF; }
    else { size_t l=strlen(call); if(l>6)l=6; memcpy(c,call,l); }
    for(int i=0;i<6;i++) out[i]=(uint8_t)(toupper(c[i]))<<1;
    out[6]=0x60|((ssid&0xF)<<1)|(last?1:0);
}

static int build_ax25(const char *dst, const char *src, const uint8_t *p, size_t pl, uint8_t *o, size_t os) {
    if(AX25_HDR+pl>os) return -1;
    size_t i=0;
    encode_ax25_addr(dst,o+i,0); i+=7;
    encode_ax25_addr(src,o+i,1); i+=7;
    o[i++]=0x03; o[i++]=0xF0;
    memcpy(o+i,p,pl);
    return (int)(i+pl);
}

static void decode_ax25_addr(const uint8_t *a, char *call) {
    char c[7]; for(int i=0;i<6;i++) c[i]=a[i]>>1; c[6]=0;
    for(int i=5;i>=0;i--) { if(c[i]==' ')c[i]=0; else break; }
    uint8_t ssid=(a[6]>>1)&0xF;
    if(ssid>0) sprintf(call,"%s-%d",c,ssid); else strcpy(call,c);
}

static int strip_ax25(const uint8_t *f, size_t fl, const uint8_t **p, char *src, char *dst) {
    if(fl<AX25_HDR) return -1;
    decode_ax25_addr(f,dst); decode_ax25_addr(f+7,src);
    if(f[14]!=0x03||f[15]!=0xF0) return -1;
    *p=f+AX25_HDR;
    return (int)(fl-AX25_HDR);
}

/* Serial port */
static speed_t baud_speed(int b) {
    switch(b){case 1200:return B1200;case 2400:return B2400;case 4800:return B4800;
    case 9600:return B9600;case 19200:return B19200;case 38400:return B38400;
    case 57600:return B57600;case 115200:return B115200;default:return B9600;}
}

static int open_serial(const char *dev, int baud) {
    int fd=open(dev,O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(fd<0){fprintf(stderr,"serialcla: open(%s): %s\n",dev,strerror(errno));return -1;}
    int fl=fcntl(fd,F_GETFL,0); fcntl(fd,F_SETFL,fl&~O_NONBLOCK);
    struct termios t; memset(&t,0,sizeof(t));
    if(tcgetattr(fd,&t)!=0){close(fd);return -1;}
    cfsetospeed(&t,baud_speed(baud)); cfsetispeed(&t,baud_speed(baud));
    t.c_cflag=(t.c_cflag&~CSIZE)|CS8;
    t.c_cflag&=~(PARENB|PARODD|CSTOPB|CRTSCTS); t.c_cflag|=CLOCAL|CREAD;
    t.c_iflag&=~(IXON|IXOFF|IXANY|IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);
    t.c_lflag=0; t.c_oflag=0; t.c_cc[VMIN]=0; t.c_cc[VTIME]=1;
    if(tcsetattr(fd,TCSANOW,&t)!=0){close(fd);return -1;}
    tcflush(fd,TCIOFLUSH); return fd;
}

/* KISS encode + send */
static int kiss_send(int fd, const uint8_t *data, size_t len) {
    uint8_t frame[MAX_KISS]; size_t idx=0;
    frame[idx++]=FEND; frame[idx++]=0x00;
    for(size_t i=0;i<len;i++){
        if(idx>=MAX_KISS-2) return -1;
        if(data[i]==FEND){frame[idx++]=FESC;frame[idx++]=TFEND;}
        else if(data[i]==FESC){frame[idx++]=FESC;frame[idx++]=TFESC;}
        else frame[idx++]=data[i];
    }
    frame[idx++]=FEND;
    size_t w=0;
    while(w<idx){ssize_t n=write(fd,frame+w,idx-w);if(n<0){if(errno==EINTR)continue;return -1;}w+=n;}
    tcdrain(fd);
    return (int)idx;
}

/* KISS decoder */
typedef struct { uint8_t buf[MAX_AX25]; size_t len; int in_frame, escape; } kiss_dec_t;
static void kiss_dec_init(kiss_dec_t *d){d->len=0;d->in_frame=0;d->escape=0;}

/* Returns 1 if a complete frame was decoded into out/out_len */
static int kiss_dec_byte(kiss_dec_t *d, uint8_t byte, uint8_t *out, size_t *out_len) {
    if(byte==FEND){
        if(d->in_frame && d->len>1 && (d->buf[0]&0x0F)==0){
            memcpy(out,d->buf+1,d->len-1); *out_len=d->len-1;
            d->len=0; d->in_frame=1; d->escape=0;
            return 1;
        }
        d->len=0; d->in_frame=1; d->escape=0; return 0;
    }
    if(!d->in_frame) return 0;
    if(d->escape){d->escape=0;if(byte==TFEND)byte=FEND;else if(byte==TFESC)byte=FESC;}
    else if(byte==FESC){d->escape=1;return 0;}
    if(d->len<sizeof(d->buf)) d->buf[d->len++]=byte;
    return 0;
}

static int parse_device(const char *arg, char *dev, size_t ds, int *baud) {
    const char *c=strrchr(arg,':');
    if(!c){strncpy(dev,arg,ds-1);dev[ds-1]=0;*baud=9600;return 0;}
    size_t l=c-arg; if(l>=ds)l=ds-1; memcpy(dev,arg,l); dev[l]=0;
    *baud=atoi(c+1); if(*baud<=0)*baud=9600; return 0;
}

int main(int argc, char *argv[]) {
    if(argc<6){
        fprintf(stderr,"Usage: serialcla <dev>:<baud> <src> <dst> <tx_port> <rx_port> [burst:pause]\n");
        return 1;
    }
    char device[256]; int baud;
    parse_device(argv[1],device,sizeof(device),&baud);
    const char *src_call=argv[2], *dest_call=argv[3];
    int tx_port=atoi(argv[4]), rx_port=atoi(argv[5]);
    int burst=0, pause_sec=0;
    if(argc>6) sscanf(argv[6],"%d:%d",&burst,&pause_sec);

    fprintf(stderr,"serialcla: dev=%s baud=%d src=%s dst=%s tx_port=%d rx_port=%d",
            device,baud,src_call,dest_call,tx_port,rx_port);
    if(burst>0) fprintf(stderr," pacing=%d:%d",burst,pause_sec);
    fprintf(stderr,"\n");

    signal(SIGINT,handle_signal); signal(SIGTERM,handle_signal); signal(SIGPIPE,SIG_IGN);

    int serial_fd=open_serial(device,baud);
    if(serial_fd<0) return 1;

    /* TX: UDP listen socket (receives from udplso) */
    int tx_sock=socket(AF_INET,SOCK_DGRAM,0);
    int reuse=1; setsockopt(tx_sock,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(tx_sock,SOL_SOCKET,SO_REUSEPORT,&reuse,sizeof(reuse));
#endif
    struct sockaddr_in tx_addr; memset(&tx_addr,0,sizeof(tx_addr));
    tx_addr.sin_family=AF_INET; tx_addr.sin_port=htons(tx_port); tx_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(tx_sock,(struct sockaddr*)&tx_addr,sizeof(tx_addr))<0){perror("bind tx");return 1;}

    /* RX: UDP send socket (forwards to udplsi) */
    int rx_sock=socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in rx_dest; memset(&rx_dest,0,sizeof(rx_dest));
    rx_dest.sin_family=AF_INET; rx_dest.sin_port=htons(rx_port); rx_dest.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

    fprintf(stderr,"serialcla: running (TX: UDP:%d→serial, RX: serial→UDP:%d)\n",tx_port,rx_port);

    kiss_dec_t dec; kiss_dec_init(&dec);
    uint64_t tx_count=0, rx_count=0;
    int burst_sent=0;

    while(running) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(serial_fd,&rfds);
        FD_SET(tx_sock,&rfds);
        int maxfd=(serial_fd>tx_sock?serial_fd:tx_sock)+1;
        struct timeval tv={1,0};

        int ret=select(maxfd,&rfds,NULL,NULL,&tv);
        if(ret<0){if(errno==EINTR)continue;break;}

        /* RX: serial → UDP (always process incoming RF data) */
        if(FD_ISSET(serial_fd,&rfds)){
            uint8_t rbuf[SERIAL_BUF];
            ssize_t n=read(serial_fd,rbuf,sizeof(rbuf));
            if(n>0){
                for(ssize_t i=0;i<n;i++){
                    uint8_t frame[MAX_AX25]; size_t flen;
                    if(kiss_dec_byte(&dec,rbuf[i],frame,&flen)){
                        char sc[16],dc[16]; const uint8_t *payload;
                        int plen=strip_ax25(frame,flen,&payload,sc,dc);
                        if(plen>0){
                            sendto(rx_sock,payload,plen,0,(struct sockaddr*)&rx_dest,sizeof(rx_dest));
                            rx_count++;
                            fprintf(stderr,"serialcla: RX #%llu from %s (%d bytes)\n",
                                    (unsigned long long)rx_count,sc,plen);
                        }
                    }
                }
            }
        }

        /* TX: UDP → serial (send outgoing LTP segments) */
        if(FD_ISSET(tx_sock,&rfds)){
            uint8_t seg[MAX_SEGMENT]; uint8_t ax[MAX_AX25];
            struct sockaddr_in from; socklen_t fl=sizeof(from);
            ssize_t n=recvfrom(tx_sock,seg,sizeof(seg),0,(struct sockaddr*)&from,&fl);
            if(n>0){
                int al=build_ax25(dest_call,src_call,seg,n,ax,sizeof(ax));
                if(al>0){
                    int kl=kiss_send(serial_fd,ax,al);
                    if(kl>0){
                        tx_count++; burst_sent++;
                        fprintf(stderr,"serialcla: TX #%llu LTP(%zd)->AX25(%d)->KISS(%d)\n",
                                (unsigned long long)tx_count,n,al,kl);
                        if(burst>0 && burst_sent>=burst){
                            fprintf(stderr,"serialcla: PAUSE %ds\n",pause_sec);
                            burst_sent=0;
                            sleep(pause_sec);
                        }
                    }
                }
            }
        }
    }

    close(tx_sock); close(rx_sock); close(serial_fd);
    fprintf(stderr,"serialcla: exiting, TX=%llu RX=%llu\n",
            (unsigned long long)tx_count,(unsigned long long)rx_count);
    return 0;
}
