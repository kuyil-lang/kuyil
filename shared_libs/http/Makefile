CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -fPIC -I../../src
LDFLAGS = -shared
LIBNAME = http
BUILDDIR = ../../build/$(LIBNAME)

TARGET = ../../libs/libkyl$(LIBNAME).so
ARCHIVE = $(BUILDDIR)/libkyl$(LIBNAME).a
SOURCES = libkylhttp.c http_server.c http_client_ex.c libkylhttp_client_ex.c
OBJS = $(SOURCES:%.c=$(BUILDDIR)/%.o)

all: $(TARGET) $(ARCHIVE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lcurl -lpthread

$(ARCHIVE): $(OBJS)
	ar rcs $(ARCHIVE) $(OBJS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean