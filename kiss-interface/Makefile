CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

SRC = main.c kiss.c ax25.c serial.c ping.c ltp.c sdnv.c beacon.c aprs.c cbor.c bp.c nrzi.c afsk.c uhd_tx.c uhd_rx.c afsk_demod.c
OBJ = $(SRC:.c=.o)

# Detect theft library availability
HAVE_THEFT := $(shell echo 'int main(){return 0;}' | $(CC) -x c - -ltheft -o /dev/null 2>/dev/null && echo yes || echo no)

ifeq ($(HAVE_THEFT),yes)
  THEFT_FLAGS = -ltheft -DHAVE_THEFT
else
  THEFT_FLAGS =
endif

kiss_interface: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm $(LDFLAGS)

bp.o: bp.c
	$(CC) $(CFLAGS) -Wno-misleading-indentation -c -o $@ $<

test_kiss: test_kiss.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_ax25: test_ax25.c ax25.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_serial: test_serial.c serial.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^

test_cli: test_cli.c main.c kiss.c ax25.c serial.c ping.c ltp.c sdnv.c beacon.c aprs.c cbor.c bp.c nrzi.c afsk.c uhd_tx.c uhd_rx.c afsk_demod.c
	$(CC) $(CFLAGS) -DTEST_CLI_MODE -o $@ $^ -lm

test_ping: test_ping.c ping.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_sdnv: test_sdnv.c sdnv.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_ltp: test_ltp.c ltp.c sdnv.c kiss.c aprs.c ax25.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_beacon: test_beacon.c beacon.c ax25.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_aprs: test_aprs.c aprs.c ax25.c kiss.c beacon.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_cbor: test_cbor.c cbor.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_bp: test_bp.c bp.c cbor.c
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o $@ $^ $(THEFT_FLAGS)

test_nrzi: test_nrzi.c nrzi.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_afsk: test_afsk.c afsk.c nrzi.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_uhd_tx: test_uhd_tx.c uhd_tx.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_uhd_rx: test_uhd_rx.c uhd_rx.c uhd_tx.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test_afsk_demod: test_afsk_demod.c afsk_demod.c afsk.c nrzi.c ax25.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS) -lm

test: test_kiss test_ax25 test_serial test_cli test_ping test_sdnv test_ltp test_beacon test_aprs test_cbor test_bp test_nrzi test_afsk test_uhd_tx test_uhd_rx test_afsk_demod
	./test_kiss
	./test_ax25
	./test_serial
	./test_cli
	./test_ping
	./test_sdnv
	./test_ltp
	./test_beacon
	./test_aprs
	./test_cbor
	./test_bp
	./test_nrzi
	./test_afsk
	./test_uhd_tx
	./test_uhd_rx
	./test_afsk_demod

clean:
	rm -f kiss_interface test_kiss test_ax25 test_serial test_cli test_ping test_sdnv test_ltp test_beacon test_aprs test_cbor test_bp test_nrzi test_afsk test_uhd_tx test_uhd_rx test_afsk_demod *.o

.PHONY: test clean
