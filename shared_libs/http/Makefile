CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -fPIC -I../../src
LDFLAGS = -shared

TARGET = ../../libs/libkylhttp.so
SOURCES = libkylhttp.c http_server.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lcurl -lpthread

clean:
	rm -f $(TARGET)

.PHONY: all clean