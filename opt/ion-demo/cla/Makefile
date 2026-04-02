CC = gcc
CFLAGS = -Wall -Wextra -O2
PREFIX ?= /usr/local/bin

TARGETS = seriallso seriallsi serialcla ionserialcla test_lso_feed test_lsi_recv

all: $(TARGETS)

seriallso: seriallso.c
	$(CC) $(CFLAGS) -o $@ $<

seriallsi: seriallsi.c
	$(CC) $(CFLAGS) -o $@ $<

serialcla: serialcla.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread

ION_DIR ?= $(shell cd ../../ION-DTN 2>/dev/null && pwd || cd ../../../ION-DTN 2>/dev/null && pwd)
ION_INC = -I$(ION_DIR)/ici/include -I$(ION_DIR)/ltp/include -I$(ION_DIR)/ltp/library -I$(ION_DIR)/ltp/udp
ION_LIB = -L$(ION_DIR)/.libs -lltp -lici -lpthread

ionserialcla: ionserialcla.c
	$(CC) $(CFLAGS) $(ION_INC) -o $@ $< $(ION_LIB)

test_lso_feed: test_lso_feed.c
	$(CC) $(CFLAGS) -o $@ $<

test_lsi_recv: test_lsi_recv.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(TARGETS)
	install -m 755 seriallso $(PREFIX)/seriallso
	install -m 755 seriallsi $(PREFIX)/seriallsi

clean:
	rm -f $(TARGETS) *.o

.PHONY: all install clean
