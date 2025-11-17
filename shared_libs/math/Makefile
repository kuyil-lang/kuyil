CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -O2 -fPIC
LIBNAME=math
BUILDDIR=../../build/$(LIBNAME)
TARGET=../../libs/libkyl$(LIBNAME).so
ARCHIVE=$(BUILDDIR)/libkyl$(LIBNAME).a
SRC=libkylmath.c
OBJS=$(BUILDDIR)/libkylmath.o

all: $(TARGET) $(ARCHIVE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	mkdir -p ../../libs
	$(CC) $(CFLAGS) -shared -o $(TARGET) $(OBJS) -lm

$(ARCHIVE): $(OBJS)
	ar rcs $(ARCHIVE) $(OBJS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

.PHONY: all clean