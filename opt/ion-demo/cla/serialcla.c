/*
 * serialcla.c - Threaded serial AX.25/KISS CLA for ION-DTN LTP
 *
 * Single process, three threads:
 *   - RX thread: reads KISS frames from serial, strips AX.25, queues LTP
 *   - TX thread: dequeues LTP segments, wraps in AX.25/KISS, writes serial
 *   - Main: reads UDP from ION (udplso), queues for TX thread;
 *           dequeues from RX thread, forwards UDP to ION (udplsi)
 *
 * Propagation delay is applied in the TX/RX threads without blocking I/O.
 *
 * Usage: serialcla <dev>:<baud> <src> <dst> <tx_port> <rx_port> [delay_ms]
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
#include <pthread.h>
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
#define QUEUE_SIZE   256

static volatile int running = 1;
static void handle_signal(int sig) { (void)sig; running = 0; }

/* ── Thread-safe queue ── */
typedef struct {
    uint8_t data[MAX_SEGMENT];
    size_t  len;
} qitem_t;

typedef struct {
    qitem_t       items[QUEUE_SIZE];
    int           head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
} queue_t;

static void queue_init(queue_t *q) {
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

static int queue_push(queue_t *q, const uint8_t *data, size_t len) {
    pthread_mutex_lock(&q->lock);
    if (q->count >= QUEUE_SIZE) { pthread_mutex_unlock(&q->lock); return -1; }
    memcpy(q->items[q->tail].data, data, len);
    q->items[q->tail].len = len;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static int queue_pop(queue_t *q, uint8_t *data, size_t *len, int timeout_ms) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && running) {
        if (timeout_ms > 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (long)timeout_ms * 1000000L;
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
            pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
            break;
        } else {
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
    }
    if (q->count == 0) { pthread_mutex_unlock(&q->lock); return -1; }
    *len = q->items[q->head].len;
    memcpy(data, q->items[q->head].data, *len);
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* ── AX.25 ── */
static void encode_ax25_addr(const char *call, uint8_t *out, int last) {
    char c[7] = "      "; uint8_t ssid = 0;
    const char *d = strchr(call, '-');
    if (d) { size_t l=d-call; if(l>6)l=6; memcpy(c,call,l); ssid=atoi(d+1)&0xF; }
    else { size_t l=strlen(call); if(l>6)l=6; memcpy(c,call,l); }
    for (int i=0;i<6;i++) out[i]=(uint8_t)(toupper(c[i]))<<1;
    out[6]=0x60|((ssid&0xF)<<1)|(last?1:0);
}

static int build_ax25(const char *dst, const char *src, const uint8_t *p, size_t pl, uint8_t *o, size_t os) {
    if (AX25_HDR+pl>os) return -1;
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
    *p=f+AX25_HDR; return (int)(fl-AX25_HDR);
}

/* ── Serial ── */
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

static int kiss_dec_byte(kiss_dec_t *d, uint8_t byte, uint8_t *out, size_t *out_len) {
    if(byte==FEND){
        if(d->in_frame && d->len>1 && (d->buf[0]&0x0F)==0){
            memcpy(out,d->buf+1,d->len-1); *out_len=d->len-1;
            d->len=0; d->in_frame=1; d->escape=0; return 1;
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

/* ── Thread contexts ── */
typedef struct {
    int serial_fd;
    queue_t *tx_queue;      /* segments to transmit */
    const char *src_call;
    const char *dest_call;
    int delay_ms;
} tx_ctx_t;

typedef struct {
    int serial_fd;
    queue_t *rx_queue;      /* decoded LTP segments to forward */
    int delay_ms;
} rx_ctx_t;

/* TX thread: dequeue segments, apply delay, AX.25/KISS encode, write serial */
static void *tx_thread(void *arg) {
    tx_ctx_t *ctx = (tx_ctx_t *)arg;
    uint8_t seg[MAX_SEGMENT], ax[MAX_AX25];
    size_t len;
    uint64_t count = 0;

    while (running) {
        if (queue_pop(ctx->tx_queue, seg, &len, 500) < 0) continue;
        if (ctx->delay_ms > 0) usleep(ctx->delay_ms * 1000);
        int al = build_ax25(ctx->dest_call, ctx->src_call, seg, len, ax, sizeof(ax));
        if (al > 0) {
            int kl = kiss_send(ctx->serial_fd, ax, al);
            if (kl > 0) {
                count++;
                fprintf(stderr, "serialcla: TX #%llu LTP(%zu)->AX25(%d)->KISS(%d)\n",
                        (unsigned long long)count, len, al, kl);
            }
        }
    }
    return NULL;
}

/* RX thread: read serial, KISS decode, strip AX.25, queue LTP segments */
static void *rx_thread(void *arg) {
    rx_ctx_t *ctx = (rx_ctx_t *)arg;
    kiss_dec_t dec; kiss_dec_init(&dec);
    uint8_t rbuf[4096];

    while (running) {
        ssize_t n = read(ctx->serial_fd, rbuf, sizeof(rbuf));
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) continue;
        for (ssize_t i = 0; i < n; i++) {
            uint8_t frame[MAX_AX25]; size_t flen;
            if (kiss_dec_byte(&dec, rbuf[i], frame, &flen)) {
                char sc[16], dc[16]; const uint8_t *payload;
                int plen = strip_ax25(frame, flen, &payload, sc, dc);
                if (plen > 0) {
                    if (ctx->delay_ms > 0) usleep(ctx->delay_ms * 1000);
                    queue_push(ctx->rx_queue, payload, plen);
                    fprintf(stderr, "serialcla: RX from %s (%d bytes)\n", sc, plen);
                }
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: serialcla <dev>:<baud> <src> <dst> <tx_port> <rx_port> [delay_ms]\n");
        fprintf(stderr, "  delay_ms: simulated OWLT in ms (default: 0)\n");
        return 1;
    }
    char device[256]; int baud;
    parse_device(argv[1], device, sizeof(device), &baud);
    const char *src_call = argv[2], *dest_call = argv[3];
    int tx_port = atoi(argv[4]), rx_port = atoi(argv[5]);
    int delay_ms = (argc > 6) ? atoi(argv[6]) : 0;

    fprintf(stderr, "serialcla: dev=%s baud=%d src=%s dst=%s tx=%d rx=%d",
            device, baud, src_call, dest_call, tx_port, rx_port);
    if (delay_ms > 0) fprintf(stderr, " delay=%dms", delay_ms);
    fprintf(stderr, "\n");

    signal(SIGINT, handle_signal); signal(SIGTERM, handle_signal); signal(SIGPIPE, SIG_IGN);

    int serial_fd = open_serial(device, baud);
    if (serial_fd < 0) return 1;

    /* UDP sockets */
    int tx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(tx_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(tx_sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
    struct sockaddr_in ta = {0}; ta.sin_family=AF_INET; ta.sin_port=htons(tx_port); ta.sin_addr.s_addr=htonl(INADDR_ANY);
    if (bind(tx_sock, (struct sockaddr*)&ta, sizeof(ta)) < 0) { perror("bind tx"); return 1; }

    int rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in rd = {0}; rd.sin_family=AF_INET; rd.sin_port=htons(rx_port); rd.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

    /* Queues */
    queue_t tx_q, rx_q;
    queue_init(&tx_q); queue_init(&rx_q);

    /* Start threads */
    tx_ctx_t tc = { serial_fd, &tx_q, src_call, dest_call, delay_ms };
    rx_ctx_t rc = { serial_fd, &rx_q, delay_ms };
    pthread_t tx_tid, rx_tid;
    pthread_create(&tx_tid, NULL, tx_thread, &tc);
    pthread_create(&rx_tid, NULL, rx_thread, &rc);

    fprintf(stderr, "serialcla: running (threaded)\n");

    /* Main loop: UDP↔queues */
    while (running) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(tx_sock, &rfds);
        struct timeval tv = {0, 100000}; /* 100ms */
        select(tx_sock + 1, &rfds, NULL, NULL, &tv);

        /* UDP → TX queue */
        if (FD_ISSET(tx_sock, &rfds)) {
            uint8_t seg[MAX_SEGMENT];
            struct sockaddr_in from; socklen_t fl = sizeof(from);
            ssize_t n = recvfrom(tx_sock, seg, sizeof(seg), 0, (struct sockaddr*)&from, &fl);
            if (n > 0) queue_push(&tx_q, seg, n);
        }

        /* RX queue → UDP */
        uint8_t seg[MAX_SEGMENT]; size_t len;
        while (queue_pop(&rx_q, seg, &len, 0) == 0) {
            sendto(rx_sock, seg, len, 0, (struct sockaddr*)&rd, sizeof(rd));
        }
    }

    pthread_join(tx_tid, NULL);
    pthread_join(rx_tid, NULL);
    close(tx_sock); close(rx_sock); close(serial_fd);
    fprintf(stderr, "serialcla: exiting\n");
    return 0;
}
