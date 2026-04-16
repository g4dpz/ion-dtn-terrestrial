#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

/* Open serial port in raw 8N1 mode. Returns fd or -1 on error. */
int serial_open(const char *device, int baud);

/* Close serial port. */
void serial_close(int fd);

/* Send KISS TNC parameter commands (TX-delay, TX-tail).
 * txdelay_ms and txtail_ms are in milliseconds; converted to 10ms units. */
int serial_configure_tnc(int fd, int txdelay_ms, int txtail_ms);

/* Parse "device:baud" string. Sets *baud to 9600 if not specified. */
int serial_parse_device(const char *arg, char *device, size_t dev_size, int *baud);

#endif
