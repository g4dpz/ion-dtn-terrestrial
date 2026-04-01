CC = gcc
CFLAGS = -Wall -O2
TARGETS = aprs_sender aprs_receiver ax25_ltp_sender ax25_ltp_receiver ax25_ltp_bp_sender ax25_ltp_bp_receiver

all: $(TARGETS)

aprs_sender: aprs_sender.c kiss.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

aprs_receiver: aprs_receiver.c kiss.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

ax25_ltp_sender: ax25_ltp_sender.c kiss.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^

ax25_ltp_receiver: ax25_ltp_receiver.c kiss.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^

ax25_ltp_bp_sender: ax25_ltp_bp_sender.c kiss.c bp.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^

ax25_ltp_bp_receiver: ax25_ltp_bp_receiver.c kiss.c bp.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
