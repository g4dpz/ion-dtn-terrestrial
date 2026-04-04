CC = gcc
CFLAGS = -Wall -Wextra -O2

ION_DIR ?= $(shell cd ../../ION-DTN 2>/dev/null && pwd || cd ../../../ION-DTN 2>/dev/null && pwd)
ION_INC = -I$(ION_DIR)/ici/include -I$(ION_DIR)/ltp/include -I$(ION_DIR)/ltp/library -I$(ION_DIR)/ltp/udp
ION_LIB = -L$(ION_DIR)/.libs -lltp -lici -lpthread

all: ionserialcla

ionserialcla: ionserialcla.c
	$(CC) $(CFLAGS) $(ION_INC) -o $@ $< $(ION_LIB)

clean:
	rm -f ionserialcla *.o

.PHONY: all clean
