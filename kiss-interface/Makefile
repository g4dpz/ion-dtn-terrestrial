CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

SRC = main.c kiss.c ax25.c serial.c ping.c
OBJ = $(SRC:.c=.o)

# Detect theft library availability
HAVE_THEFT := $(shell echo 'int main(){return 0;}' | $(CC) -x c - -ltheft -o /dev/null 2>/dev/null && echo yes || echo no)

ifeq ($(HAVE_THEFT),yes)
  THEFT_FLAGS = -ltheft -DHAVE_THEFT
else
  THEFT_FLAGS =
endif

kiss_interface: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test_kiss: test_kiss.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_ax25: test_ax25.c ax25.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test_serial: test_serial.c serial.c kiss.c
	$(CC) $(CFLAGS) -o $@ $^

test_cli: test_cli.c main.c kiss.c ax25.c serial.c ping.c
	$(CC) $(CFLAGS) -DTEST_CLI_MODE -o $@ $^

test_ping: test_ping.c ping.c
	$(CC) $(CFLAGS) -o $@ $^ $(THEFT_FLAGS)

test: test_kiss test_ax25 test_serial test_cli test_ping
	./test_kiss
	./test_ax25
	./test_serial
	./test_cli
	./test_ping

clean:
	rm -f kiss_interface test_kiss test_ax25 test_serial test_cli test_ping *.o

.PHONY: test clean
