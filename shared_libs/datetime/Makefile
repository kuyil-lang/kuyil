CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -O2 -fPIC
TARGET=../../libs/libkyldatetime.so
SRC=libkyldatetime.c

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) -shared -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean