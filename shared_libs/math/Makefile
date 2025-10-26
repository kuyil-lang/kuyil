CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -O2 -fPIC
TARGET=../../libs/libkylmath.so
SRC=libkylmath.c

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) -shared -o $(TARGET) $(SRC) -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean