LIBNAME = libkyleventloop.so
LIBDIR = ../../libs

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -fPIC -I../../src
LDFLAGS = -shared

all: $(LIBDIR)/$(LIBNAME)

$(LIBDIR)/$(LIBNAME): libkyleventloop.c
	mkdir -p $(LIBDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(LIBDIR)/$(LIBNAME)

.PHONY: all clean
