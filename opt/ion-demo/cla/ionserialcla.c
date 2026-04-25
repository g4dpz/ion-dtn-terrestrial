/*
 * ionserialcla.c - ION-integrated serial AX.25/KISS CLA for LTP
 *
 * Links against ION's libltp to provide natural backpressure.
 * Replaces both udplso and udplsi — reads segments directly from
 * ION's SDR and feeds received segments back in.
 *
 * TX: ltpDequeueOutboundSegment() → AX.25/KISS → serial → TNC → RF
 * RX: RF → TNC → serial → KISS/AX.25 → ltpHandleInboundSegment()
 *
 * Usage: ionserialcla <device>:<baud> <src_call> <dest_call> <remote_engine_id>
 *
 * ltprc config:
 *   a span 2 128 128 1400 10000 1 'ionserialcla /dev/ttyACM0:9600 G4DPZ-1 G4DPZ-2 2'
 *   s 'ionserialcla /dev/ttyACM0:9600 G4DPZ-1 G4DPZ-2 2'
 */

#include "ltpP.h"
#include <pthread.h>
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
#include <stdarg.h>
#include <math.h>

#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define MAX_SEGMENT  ((256 * 256) - 1)
#define AX25_HDR     16
#define MAX_AX25     (AX25_HDR + MAX_SEGMENT)
#define MAX_KISS     (MAX_AX25 * 2 + 4)

static int g_running = 1;
static int g_debug = 0;  /* set via env ION_SERIAL_DEBUG=1 */

static void dbg(const char *fmt, ...) {
    if (!g_debug) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    writeMemo(buf);
}

static void dbg_hex(const char *label, const uint8_t *data, size_t len) {
    if (!g_debug) return;
    char buf[512];
    int off = snprintf(buf, sizeof(buf), "[DBG] %s (%zu bytes): ", label, len);
    size_t show = len > 64 ? 64 : len;
    for (size_t i = 0; i < show && off < (int)sizeof(buf) - 4; i++)
        off += snprintf(buf + off, sizeof(buf) - off, "%02X ", data[i]);
    if (len > 64) snprintf(buf + off, sizeof(buf) - off, "...");
    writeMemo(buf);
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
    memcpy(o+i,p,pl); return (int)(i+pl);
}
static int strip_ax25(const uint8_t *f, size_t fl, const uint8_t **p) {
    if (fl < AX25_HDR) return -1;
    if (f[14]!=0x03 || f[15]!=0xF0) return -1;
    *p = f + AX25_HDR;
    return (int)(fl - AX25_HDR);
}

/* ── Serial ── */
static speed_t baud_speed(int b) {
    switch(b){case 1200:return B1200;case 9600:return B9600;
    case 19200:return B19200;case 38400:return B38400;
    case 57600:return B57600;case 115200:return B115200;default:return B9600;}
}
static int open_serial(const char *dev, int baud) {
    int fd=open(dev,O_RDWR|O_NOCTTY);
    if(fd<0){putSysErrmsg("Can't open serial device",dev);return -1;}
    struct termios t;
    memset(&t,0,sizeof(t));
    if(tcgetattr(fd,&t)!=0){close(fd);return -1;}
    cfmakeraw(&t);
    t.c_cflag&=~(CSIZE|PARENB|CSTOPB|CRTSCTS);
    t.c_cflag|=CS8|CLOCAL|CREAD;
    t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
    cfsetospeed(&t,baud_speed(baud)); cfsetispeed(&t,baud_speed(baud));
    if(tcsetattr(fd,TCSANOW,&t)!=0){close(fd);return -1;}
    tcflush(fd,TCIOFLUSH);
    dbg("[DBG] Serial opened: fd=%d dev=%s baud=%d", fd, dev, baud);

    /*
     * Send KISS parameter commands to configure the TNC.
     * TX-tail (cmd 0x04): time in 10ms units to keep PTT keyed after last byte.
     * TX-delay (cmd 0x01): time in 10ms units before first byte after PTT key.
     * Without sufficient TX-tail, the last bytes of the frame get clipped.
     * Default: TX-delay=50 (500ms), TX-tail=30 (300ms).
     * Override via ION_SERIAL_TXTAIL_MS and ION_SERIAL_TXDELAY_MS env vars.
     */
    {
        const char *tail_env = getenv("ION_SERIAL_TXTAIL_MS");
        const char *delay_env = getenv("ION_SERIAL_TXDELAY_MS");
        int txtail_10ms = tail_env ? (atoi(tail_env) / 10) : 30;  /* 300ms default */
        int txdelay_10ms = delay_env ? (atoi(delay_env) / 10) : 50; /* 500ms default */
        if (txtail_10ms < 0) txtail_10ms = 30;
        if (txdelay_10ms < 0) txdelay_10ms = 50;

        uint8_t kiss_txdelay[] = { FEND, 0x01, (uint8_t)txdelay_10ms, FEND };
        uint8_t kiss_txtail[]  = { FEND, 0x04, (uint8_t)txtail_10ms, FEND };

        write(fd, kiss_txdelay, sizeof(kiss_txdelay));
        write(fd, kiss_txtail, sizeof(kiss_txtail));
        tcdrain(fd);

        dbg("[DBG] KISS params: TX-delay=%dms TX-tail=%dms",
            txdelay_10ms * 10, txtail_10ms * 10);
    }

    return fd;
}

/* ── KISS ── */
static int kiss_send(int fd, const uint8_t *data, size_t len) {
    uint8_t frame[MAX_KISS]; size_t idx=0;
    dbg("[DBG] kiss_send: encoding %zu bytes", len);
    dbg_hex("kiss_send input", data, len);
    frame[idx++]=FEND; frame[idx++]=0x00;
    for(size_t i=0;i<len;i++){
        if(idx>=MAX_KISS-2) { dbg("[DBG] kiss_send: frame overflow at byte %zu", i); return -1; }
        if(data[i]==FEND){frame[idx++]=FESC;frame[idx++]=TFEND;}
        else if(data[i]==FESC){frame[idx++]=FESC;frame[idx++]=TFESC;}
        else frame[idx++]=data[i];
    }
    frame[idx++]=FEND;
    dbg("[DBG] kiss_send: KISS frame %zu bytes", idx);
    dbg_hex("kiss_send KISS frame", frame, idx);
    size_t w=0;
    while(w<idx){
        ssize_t n=write(fd,frame+w,idx-w);
        if(n<0){
            if(errno==EINTR)continue;
            if(errno==EAGAIN||errno==EWOULDBLOCK){usleep(10000);continue;}
            /* EIO = USB device disconnected. Fail immediately so caller
             * can close/reopen. Retrying a dead fd wastes time. */
            dbg("[DBG] kiss_send: write error errno=%d (%s)", errno, strerror(errno));
            return -1;
        }
        dbg("[DBG] kiss_send: wrote %zd bytes (total %zu/%zu)", n, w+n, idx);
        w+=n;
    }
    dbg("[DBG] kiss_send: calling tcdrain...");
    int drain_rc = tcdrain(fd);
    if (drain_rc < 0) {
        dbg("[DBG] kiss_send: tcdrain failed errno=%d (%s)", errno, strerror(errno));
        return -1;
    }
    dbg("[DBG] kiss_send: tcdrain returned %d", drain_rc);

    /* Post-transmit delay: wait for TNC to finish RF transmission.
     * At 1200 baud, each byte takes ~8.3ms over the air.
     * Add TX-delay(500ms) + TX-tail(300ms) + 700ms margin = 1500ms fixed.
     * This matches the proven pacing from the standalone LTP stack. */
    {
        int rf_baud = 1200;
        const char *rfenv = getenv("ION_SERIAL_RF_BAUD");
        if (rfenv) rf_baud = atoi(rfenv);
        if (rf_baud <= 0) rf_baud = 1200;
        int us_per_byte = (10 * 1000000) / rf_baud;
        int delay_us = (int)idx * us_per_byte + 1500000;
        dbg("[DBG] kiss_send: post-TX delay %d ms", delay_us / 1000);
        usleep((useconds_t)delay_us);
    }
    return (int)idx;
}

typedef struct { uint8_t buf[MAX_AX25]; size_t len; int in_frame, escape; } kiss_dec_t;
static void kiss_dec_init(kiss_dec_t *d){d->len=0;d->in_frame=0;d->escape=0;}
static int kiss_dec_byte(kiss_dec_t *d, uint8_t byte, uint8_t *out, size_t *out_len) {
    if(byte==FEND){
        if(d->in_frame && d->len>1 && (d->buf[0]&0x0F)==0){
            memcpy(out,d->buf+1,d->len-1); *out_len=d->len-1;
            dbg("[DBG] kiss_dec: complete frame %zu bytes (cmd=0x%02X)", d->len-1, d->buf[0]);
            d->len=0; d->in_frame=1; d->escape=0; return 1;
        }
        if(d->in_frame && d->len>0) {
            dbg("[DBG] kiss_dec: discarding partial frame %zu bytes (cmd=0x%02X)", d->len, d->len>0?d->buf[0]:0xFF);
        }
        d->len=0; d->in_frame=1; d->escape=0; return 0;
    }
    if(!d->in_frame) return 0;
    if(d->escape){d->escape=0;if(byte==TFEND)byte=FEND;else if(byte==TFESC)byte=FESC;}
    else if(byte==FESC){d->escape=1;return 0;}
    if(d->len<sizeof(d->buf)) d->buf[d->len++]=byte;
    else dbg("[DBG] kiss_dec: buffer overflow, dropping byte");
    return 0;
}

static int parse_device(const char *arg, char *dev, size_t ds, int *baud) {
    const char *c=strrchr(arg,':');
    if(!c){strncpy(dev,arg,ds-1);dev[ds-1]=0;*baud=9600;return 0;}
    size_t l=c-arg; if(l>=ds)l=ds-1; memcpy(dev,arg,l); dev[l]=0;
    *baud=atoi(c+1); if(*baud<=0)*baud=9600; return 0;
}

/* ── RX thread: serial → ION ── */
typedef struct {
    int serial_fd;
    int running;
    char device[256];
    int baud;
    pthread_mutex_t fd_lock;
} RxThreadParms;

static void *rx_thread(void *parm) {
    RxThreadParms *rtp = (RxThreadParms *)parm;
    kiss_dec_t dec;
    kiss_dec_init(&dec);
    uint8_t rbuf[4096];
    uint64_t count = 0;

    while (rtp->running) {
        int fd;
        pthread_mutex_lock(&rtp->fd_lock);
        fd = rtp->serial_fd;
        pthread_mutex_unlock(&rtp->fd_lock);

        if (fd < 0) { usleep(500000); continue; }

        ssize_t n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EIO) {
                dbg("[DBG] RX: EIO on read, waiting for TX to reopen port");
                usleep(500000);
                continue;
            }
            dbg("[DBG] RX: read error errno=%d (%s)", errno, strerror(errno));
            break;
        }
        if (n == 0) continue;

        dbg("[DBG] RX: read %zd bytes from serial", n);
        dbg_hex("RX raw serial", rbuf, (size_t)n);

        for (ssize_t i = 0; i < n; i++) {
            uint8_t frame[MAX_AX25]; size_t flen;
            if (kiss_dec_byte(&dec, rbuf[i], frame, &flen)) {
                dbg("[DBG] RX: decoded KISS frame %zu bytes", flen);
                dbg_hex("RX KISS decoded", frame, flen);

                const uint8_t *payload;
                int plen = strip_ax25(frame, flen, &payload);
                if (plen <= 0) {
                    /* Not AX.25, try raw */
                    dbg("[DBG] RX: not AX.25 (plen=%d), using raw frame", plen);
                    payload = frame;
                    plen = (int)flen;
                } else {
                    dbg("[DBG] RX: stripped AX.25 header, payload %d bytes", plen);
                }

                dbg_hex("RX LTP segment", payload, (size_t)plen);

                /* Feed directly into ION's LTP engine */
                dbg("[DBG] RX: calling ltpHandleInboundSegment(%d bytes)", plen);
                if (ltpHandleInboundSegment((char *)payload, plen) < 0) {
                    putErrmsg("Can't handle inbound segment.", NULL);
                    dbg("[DBG] RX: ltpHandleInboundSegment FAILED");
                    rtp->running = 0;
                    break;
                }
                dbg("[DBG] RX: ltpHandleInboundSegment OK");

                count++;
                if (count % 10 == 1) {
                    char msg[128];
                    isprintf(msg, sizeof(msg),
                        "[i] ionserialcla: RX #%llu (%d bytes)",
                        (unsigned long long)count, plen);
                    writeMemo(msg);
                }
            }
        }
    }

    writeMemo("[i] ionserialcla RX thread ended.");
    return NULL;
}

/* ── Main: TX loop with backpressure ── */

static void shutDown(int signum) {
    (void)signum;
    g_running = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        PUTS("Usage: ionserialcla <dev>:<baud> <src_call> <dest_call> <remote_engine_id>");
        PUTS("  Beacon env vars: ION_SERIAL_BEACON_LAT, ION_SERIAL_BEACON_LON");
        PUTS("  ION_SERIAL_BEACON_INTERVAL (default 120s), ION_SERIAL_BEACON_COMMENT");
        return 0;
    }

    char device[256]; int baud;
    parse_device(argv[1], device, sizeof(device), &baud);
    const char *src_call = argv[2];
    const char *dest_call = argv[3];
    uvast remoteEngineId = strtouvast(argv[4]);

    /* Wait for ION to finish initializing before attaching */
    sleep(2);

    /* Initialize LTP */
    if (ltpInit(0) < 0) {
        putErrmsg("ionserialcla can't initialize LTP.", NULL);
        return 1;
    }

    /* Enable debug via environment variable */
    const char *dbg_env = getenv("ION_SERIAL_DEBUG");
    if (dbg_env && (dbg_env[0] == '1' || dbg_env[0] == 'y' || dbg_env[0] == 'Y')) {
        g_debug = 1;
        writeMemo("[DBG] ionserialcla: debug logging ENABLED");
    }

    dbg("[DBG] args: dev=%s baud=%d src=%s dst=%s engine=%s",
        device, baud, src_call, dest_call, argv[4]);

    Sdr sdr = getIonsdr();
    LtpVspan *vspan;
    PsmAddress vspanElt;

    CHKZERO(sdr_begin_xn(sdr));
    findSpan(remoteEngineId, &vspan, &vspanElt);
    if (vspanElt == 0) {
        sdr_exit_xn(sdr);
        putErrmsg("No such engine in database.", itoa(remoteEngineId));
        return 1;
    }
    dbg("[DBG] Found span for engine %d, lsoPid=%d", (int)remoteEngineId, vspan->lsoPid);

    if (vspan->lsoPid != ERROR && vspan->lsoPid != sm_TaskIdSelf()) {
        sdr_exit_xn(sdr);
        putErrmsg("LSO already started for this span.", itoa(vspan->lsoPid));
        return 1;
    }
    sdr_exit_xn(sdr);
    dbg("[DBG] Span check passed, proceeding");

    /* Open serial port */
    int serial_fd = open_serial(device, baud);
    if (serial_fd < 0) return 1;

    /* Signal handling */
    signal(SIGTERM, shutDown);
    signal(SIGINT, shutDown);

    /* Start RX thread */
    RxThreadParms rtp;
    rtp.serial_fd = serial_fd;
    rtp.running = 1;
    strncpy(rtp.device, device, sizeof(rtp.device) - 1);
    rtp.device[sizeof(rtp.device) - 1] = '\0';
    rtp.baud = baud;
    pthread_mutex_init(&rtp.fd_lock, NULL);
    pthread_t rx_tid;
    if (pthread_begin(&rx_tid, NULL, rx_thread, &rtp, "ionserialcla_rx")) {
        putSysErrmsg("Can't create RX thread", NULL);
        close(serial_fd);
        return 1;
    }

    {
        char msg[256];
        isprintf(msg, sizeof(msg),
            "[i] ionserialcla running: dev=%s baud=%d src=%s dst=%s engine=%d",
            device, baud, src_call, dest_call, (int)remoteEngineId);
        writeMemo(msg);
    }

    /* TX loop: dequeue segments from ION, AX.25/KISS encode, write serial */
    uint64_t tx_count = 0;
    uint8_t ax25_frame[MAX_AX25];

    /* APRS beacon support — configured via environment variables */
    int beacon_enabled = 0;
    uint8_t beacon_kiss[600];
    int beacon_kiss_len = 0;
    int beacon_interval = 120;
    struct timespec beacon_last;
    clock_gettime(CLOCK_MONOTONIC, &beacon_last);

    {
        const char *lat_env = getenv("ION_SERIAL_BEACON_LAT");
        const char *lon_env = getenv("ION_SERIAL_BEACON_LON");
        const char *int_env = getenv("ION_SERIAL_BEACON_INTERVAL");
        const char *cmt_env = getenv("ION_SERIAL_BEACON_COMMENT");

        if (lat_env && lon_env) {
            double lat = atof(lat_env);
            double lon = atof(lon_env);
            if (int_env) beacon_interval = atoi(int_env);
            if (beacon_interval < 10) beacon_interval = 10;
            const char *comment = cmt_env ? cmt_env : "github.com/g4dpz/ion-dtn-terrestrial";

            /* Format APRS position: !DDMM.MMN/DDDMM.MMW-comment */
            char hemi_lat = (lat >= 0) ? 'N' : 'S';
            char hemi_lon = (lon >= 0) ? 'E' : 'W';
            double alat = fabs(lat), alon = fabs(lon);
            int dlat = (int)alat, dlon = (int)alon;
            double mlat = (alat - dlat) * 60.0, mlon = (alon - dlon) * 60.0;

            char info[256];
            int ilen = snprintf(info, sizeof(info),
                "!%02d%05.2f%c/%03d%05.2f%c-%s",
                dlat, mlat, hemi_lat, dlon, mlon, hemi_lon, comment);

            /* Build AX.25 frame: dst=APZ001, src=our callsign */
            uint8_t bcn_ax25[AX25_HDR + 256];
            int bcn_len = build_ax25("APZ001", src_call,
                                      (uint8_t *)info, (size_t)ilen,
                                      bcn_ax25, sizeof(bcn_ax25));
            if (bcn_len > 0) {
                /* KISS encode */
                uint8_t kf[600]; size_t ki = 0;
                kf[ki++] = FEND; kf[ki++] = 0x00;
                for (int i = 0; i < bcn_len; i++) {
                    if (bcn_ax25[i] == FEND) { kf[ki++] = FESC; kf[ki++] = TFEND; }
                    else if (bcn_ax25[i] == FESC) { kf[ki++] = FESC; kf[ki++] = TFESC; }
                    else kf[ki++] = bcn_ax25[i];
                }
                kf[ki++] = FEND;
                memcpy(beacon_kiss, kf, ki);
                beacon_kiss_len = (int)ki;
                beacon_enabled = 1;

                char msg[256];
                isprintf(msg, sizeof(msg),
                    "[i] ionserialcla: beacon enabled for %s every %ds",
                    src_call, beacon_interval);
                writeMemo(msg);
            }
        }
    }

    while (g_running && !(sm_SemEnded(vspan->segSemaphore))) {
        char *segment;
        dbg("[DBG] TX: waiting for ltpDequeueOutboundSegment...");
        int segmentLength = ltpDequeueOutboundSegment(vspan, &segment);

        if (segmentLength < 0) {
            dbg("[DBG] TX: dequeue returned %d (error/shutdown)", segmentLength);
            g_running = 0;
            continue;
        }
        if (segmentLength == 0) {
            dbg("[DBG] TX: dequeue returned 0 (interrupted), retrying");
            continue; /* Interrupted, retry */
        }

        dbg("[DBG] TX: dequeued LTP segment %d bytes", segmentLength);
        dbg_hex("TX LTP segment", (uint8_t *)segment, (size_t)segmentLength);

        /* Wrap in AX.25 UI frame */
        int ax25_len = build_ax25(dest_call, src_call,
                                  (uint8_t *)segment, segmentLength,
                                  ax25_frame, sizeof(ax25_frame));
        if (ax25_len < 0) {
            putErrmsg("AX.25 frame too large.", itoa(segmentLength));
            dbg("[DBG] TX: AX.25 build failed, segment %d bytes too large", segmentLength);
            continue;
        }

        dbg("[DBG] TX: AX.25 frame %d bytes", ax25_len);
        dbg_hex("TX AX.25 frame", ax25_frame, (size_t)ax25_len);

        /* KISS encode and write to serial — tcdrain provides backpressure */
        int kiss_len = kiss_send(serial_fd, ax25_frame, ax25_len);
        if (kiss_len < 0) {
            char errmsg[128];
            snprintf(errmsg, sizeof(errmsg),
                "[w] ionserialcla: serial write failed, reopening port");
            writeMemo(errmsg);

            /* Mark fd invalid so RX thread stops reading */
            pthread_mutex_lock(&rtp.fd_lock);
            rtp.serial_fd = -1;
            pthread_mutex_unlock(&rtp.fd_lock);

            close(serial_fd);
            usleep(3000000);  /* Wait 3s for TNC USB to fully recover */
            serial_fd = open_serial(device, baud);
            if (serial_fd < 0) {
                writeMemo("[!] ionserialcla: cannot reopen serial port, exiting");
                g_running = 0;
            } else {
                /* Update shared fd so RX thread uses the new one */
                pthread_mutex_lock(&rtp.fd_lock);
                rtp.serial_fd = serial_fd;
                pthread_mutex_unlock(&rtp.fd_lock);
                writeMemo("[i] ionserialcla: serial port reopened successfully");
                usleep(1000000);  /* Extra 1s settle time after reopen */
            }
            continue;
        }

        dbg("[DBG] TX: sent KISS frame %d bytes", kiss_len);

        tx_count++;
        if (tx_count % 10 == 1) {
            char msg[128];
            isprintf(msg, sizeof(msg),
                "[i] ionserialcla: TX #%llu LTP(%d)->AX25(%d)->KISS(%d)",
                (unsigned long long)tx_count, segmentLength, ax25_len, kiss_len);
            writeMemo(msg);
        }

        sm_TaskYield();

        /* Check if APRS beacon is due — but not immediately after a TX.
         * The pacing delay after kiss_send already ran, so the TNC should
         * be idle. Use kiss_send for proper pacing instead of raw write. */
        if (beacon_enabled) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int elapsed = (int)(now.tv_sec - beacon_last.tv_sec);
            if (elapsed >= beacon_interval) {
                /* Build AX.25 beacon frame fresh (reuse pre-built KISS) */
                ssize_t bw = write(serial_fd, beacon_kiss, (size_t)beacon_kiss_len);
                if (bw > 0) {
                    tcdrain(serial_fd);
                    /* Pacing delay for beacon frame — same formula as kiss_send */
                    int bcn_rf_us = beacon_kiss_len * 8333 + 1500000;
                    usleep((useconds_t)bcn_rf_us);
                }
                beacon_last = now;
                char msg[128];
                isprintf(msg, sizeof(msg),
                    "[i] ionserialcla: Beacon TX: %s", src_call);
                writeMemo(msg);
                dbg("[DBG] Beacon TX: %d bytes", beacon_kiss_len);
            }
        }
    }

    /* Shutdown */
    dbg("[DBG] Shutting down, tx_count=%llu", (unsigned long long)tx_count);
    rtp.running = 0;
    pthread_join(rx_tid, NULL);
    pthread_mutex_destroy(&rtp.fd_lock);
    close(serial_fd);
    writeErrmsgMemos();
    writeMemo("[i] ionserialcla has ended.");
    ionDetach();
    return 0;
}
