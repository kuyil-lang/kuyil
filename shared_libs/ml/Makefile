# Makefile for Kuyil ML Library

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fPIC -O2 -g -pthread -I../../src
LDFLAGS = -shared -pthread -lm

TARGET = ../../libs/libkylml.so

SRCS = kmeans.c linear_regression.c ml_metrics.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)
	@echo "Built ML library: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
