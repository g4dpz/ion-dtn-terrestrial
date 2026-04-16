/*
 * serial.c — Serial port handling for KISS TNC communication
 *
 * Implements device string parsing, serial port open/close with
 * raw 8N1 termios configuration, and KISS TNC parameter setup.
 */

#define _DEFAULT_SOURCE  /* cfmakeraw, CRTSCTS */

#include "serial.h"
#include "kiss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

/* Supported baud rates */
static const int supported_bauds[] = { 1200, 9600, 19200, 38400, 57600, 115200 };
#define NUM_SUPPORTED_BAUDS (sizeof(supported_bauds) / sizeof(supported_bauds[0]))

/* ------------------------------------------------------------------ */
/* baud_is_supported                                                   */
/* ------------------------------------------------------------------ */
static int baud_is_supported(int baud)
{
    for (size_t i = 0; i < NUM_SUPPORTED_BAUDS; i++) {
        if (supported_bauds[i] == baud)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* baud_to_speed                                                       */
/* ------------------------------------------------------------------ */
static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

/* ------------------------------------------------------------------ */
/* serial_parse_device                                                 */
/* ------------------------------------------------------------------ */
int serial_parse_device(const char *arg, char *device, size_t dev_size, int *baud)
{
    if (!arg || !device || dev_size == 0 || !baud)
        return -1;

    /* Look for the last colon — "device:baud" format */
    const char *colon = strrchr(arg, ':');

    if (colon && colon != arg && colon[1] != '\0') {
        /* Extract device path */
        size_t dev_len = (size_t)(colon - arg);
        if (dev_len >= dev_size)
            return -1;
        memcpy(device, arg, dev_len);
        device[dev_len] = '\0';

        /* Parse baud rate */
        char *endptr = NULL;
        long b = strtol(colon + 1, &endptr, 10);
        if (endptr == colon + 1 || *endptr != '\0' || b <= 0) {
            /* Not a valid number after colon — treat whole string as device */
            if (strlen(arg) >= dev_size)
                return -1;
            strncpy(device, arg, dev_size - 1);
            device[dev_size - 1] = '\0';
            *baud = 9600;
            return 0;
        }

        if (!baud_is_supported((int)b)) {
            fprintf(stderr, "warning: unsupported baud rate %ld, defaulting to 9600\n", b);
            *baud = 9600;
        } else {
            *baud = (int)b;
        }
    } else {
        /* No colon (or colon at start/end) — device only, default baud */
        if (strlen(arg) >= dev_size)
            return -1;
        strncpy(device, arg, dev_size - 1);
        device[dev_size - 1] = '\0';
        *baud = 9600;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* serial_open                                                         */
/* ------------------------------------------------------------------ */
int serial_open(const char *device, int baud)
{
    if (!device)
        return -1;

    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open %s: %s\n", device, strerror(errno));
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "error: tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Raw mode — no echo, no canonical, no signals, no processing */
    cfmakeraw(&tty);

    /* 8N1: 8 data bits, no parity, 1 stop bit */
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    tty.c_cflag |= CS8;

    /* No hardware flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Enable receiver, local mode */
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Blocking read: VMIN=1, VTIME=0 */
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    /* Set baud rate */
    speed_t spd = baud_to_speed(baud);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "error: tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* ------------------------------------------------------------------ */
/* serial_close                                                        */
/* ------------------------------------------------------------------ */
void serial_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

/* ------------------------------------------------------------------ */
/* serial_configure_tnc                                                */
/* ------------------------------------------------------------------ */
int serial_configure_tnc(int fd, int txdelay_ms, int txtail_ms)
{
    uint8_t cmd_buf[4];
    ssize_t written;

    /* Convert milliseconds to 10ms units, clamping to 0–255 */
    int txdelay_units = txdelay_ms / 10;
    if (txdelay_units < 0) txdelay_units = 0;
    if (txdelay_units > 255) txdelay_units = 255;

    int txtail_units = txtail_ms / 10;
    if (txtail_units < 0) txtail_units = 0;
    if (txtail_units > 255) txtail_units = 255;

    /* TX-delay: KISS command 0x01 */
    int len = kiss_build_cmd(0x01, (uint8_t)txdelay_units, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return -1;
    written = write(fd, cmd_buf, (size_t)len);
    if (written != len)
        return -1;

    /* TX-tail: KISS command 0x04 */
    len = kiss_build_cmd(0x04, (uint8_t)txtail_units, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return -1;
    written = write(fd, cmd_buf, (size_t)len);
    if (written != len)
        return -1;

    return 0;
}
