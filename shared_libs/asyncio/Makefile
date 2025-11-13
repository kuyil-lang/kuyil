CC = gcc
CFLAGS = -fPIC -Wall -O2 -I../../src
LDFLAGS = -shared

TARGET = ../../libs/libkylasyncio.so
SRC = libasyncio.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
