CC = gcc
CFLAGS = -fPIC -Wall -O2 -I../../src
LDFLAGS = -shared
LIBNAME = asyncio
BUILDDIR = ../../build/$(LIBNAME)

TARGET = ../../libs/libkyl$(LIBNAME).so
ARCHIVE = $(BUILDDIR)/libkyl$(LIBNAME).a
SRC = libasyncio.c
OBJS = $(BUILDDIR)/libasyncio.o

all: $(TARGET) $(ARCHIVE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS)

$(ARCHIVE): $(OBJS)
	ar rcs $(ARCHIVE) $(OBJS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean
