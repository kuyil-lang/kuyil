# FileIO Shared Library Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -fPIC -O2
INCLUDES = -I../.. -I../../src -I..
LIBS = 
# LIBS = -lyaml  # Uncomment if YAML support is desired
LIBNAME = fileio
BUILDDIR = ../../build/$(LIBNAME)
TARGET = ../../libs/libkyl$(LIBNAME).so
ARCHIVE = $(BUILDDIR)/libkyl$(LIBNAME).a
SOURCES = fileio_utils.c
OBJECTS = $(SOURCES:%.c=$(BUILDDIR)/%.o)

.PHONY: all clean install

all: $(TARGET) $(ARCHIVE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJECTS)
	mkdir -p ../../libs
	$(CC) -shared -o $@ $(OBJECTS) $(LIBS)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(ARCHIVE): $(OBJECTS)
	ar rcs $(ARCHIVE) $(OBJECTS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: $(TARGET)
	cp $(TARGET) ../../libs/

# Dependencies
$(BUILDDIR)/fileio_utils.o: fileio_utils.c fileio_utils.h ../kuyil_types.h ../c_interface.h

.DEFAULT_GOAL := all