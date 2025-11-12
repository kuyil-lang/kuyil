# Makefile for unit tests
CC = gcc
CFLAGS = -std=c99 -g -O0 -I../src
LDFLAGS = -lm

# Source files
TEST_SOURCES = test_opcode_executor.c
SHARED_SOURCES = ../src/opcode_executor.c
OBJECTS = $(TEST_SOURCES:.c=.o) $(SHARED_SOURCES:.c=.o)

# Test executable
TEST_EXE = test_opcode_executor

all: $(TEST_EXE)

$(TEST_EXE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TEST_EXE) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TEST_EXE) *.o ../src/opcode_executor.o

test: $(TEST_EXE)
	./$(TEST_EXE)

.PHONY: all clean test
