# Makefile for Data Structures Library
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fPIC -O2 -g -pthread
INCLUDES = -I../../src
TARGET = ../../libs/libkylds.so

# Source files
SRCS = stack.c queue.c priority_queue.c list.c treemap.c graph.c concurrent_hashmap.c decorators.c vector_ops.c knn.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p ../../libs
	$(CC) -shared -o $(TARGET) $(OBJS) -pthread

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# Dependencies
stack.o: stack.c
queue.o: queue.c
priority_queue.o: priority_queue.c
list.o: list.c
treemap.o: treemap.c
graph.o: graph.c
concurrent_hashmap.o: concurrent_hashmap.c
decorators.o: decorators.c
