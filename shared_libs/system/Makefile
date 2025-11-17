LIBNAME = system
BUILDDIR = ../../build/$(LIBNAME)
LIBDIR = ../../libs

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -fPIC -I../../src
LDFLAGS = -shared

TARGET = $(LIBDIR)/libkyl$(LIBNAME).so
ARCHIVE = $(BUILDDIR)/libkyl$(LIBNAME).a
OBJS = $(BUILDDIR)/system_utils.o

all: $(TARGET) $(ARCHIVE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	mkdir -p $(LIBDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

$(ARCHIVE): $(OBJS)
	ar rcs $(ARCHIVE) $(OBJS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean
