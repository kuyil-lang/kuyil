# FileIO Shared Library Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -fPIC -O2
INCLUDES = -I../.. -I../../src -I..
LIBS = 
# LIBS = -lyaml  # Uncomment if YAML support is desired
TARGET = ../../libs/libkylfileio.so
SOURCES = fileio_utils.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	mkdir -p ../../libs
	$(CC) -shared -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) ../../libs/

# Dependencies
fileio_utils.o: fileio_utils.c fileio_utils.h ../kuyil_types.h ../c_interface.h

.DEFAULT_GOAL := all