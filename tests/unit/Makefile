# Unit Test Makefile for Kuyil
CC = gcc
CFLAGS = -g -Wall -I../../src -pthread -std=c99
LDFLAGS = -pthread -lm -ldl -lcurl -levent

# Source files needed for testing
SRC_DIR = ../../src

# Core source files (excluding main.c)
CORE_SRCS = $(SRC_DIR)/vm.c \
            $(SRC_DIR)/compiler.c \
            $(SRC_DIR)/lexer.c \
            $(SRC_DIR)/parser.c \
            $(SRC_DIR)/avatar_runtime.c \
            $(SRC_DIR)/thread_pool.c \
            $(SRC_DIR)/opcode_executor.c \
            $(SRC_DIR)/library_loader.c \
            $(SRC_DIR)/logging.c \
            $(SRC_DIR)/config.c \
            $(SRC_DIR)/ffi.c \
            $(SRC_DIR)/file_reader.c \
            $(SRC_DIR)/green_threads.c \
            $(SRC_DIR)/vm_library_integration.c \
            $(SRC_DIR)/vm_task_queue.c \
            $(SRC_DIR)/async_http.c \
            $(SRC_DIR)/async_request_queue.c

# Test executables
TESTS = test_avatar

.PHONY: all clean run

all: $(TESTS)

test_avatar: test_avatar.c $(CORE_SRCS)
	@echo "Building test_avatar..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(TESTS)
	@echo "Running unit tests..."
	@for test in $(TESTS); do \
		./$$test || exit 1; \
	done

clean:
	rm -f $(TESTS) *.o

help:
	@echo "Unit Test Makefile"
	@echo "=================="
	@echo "Targets:"
	@echo "  all   - Build all tests"
	@echo "  run   - Build and run all tests"
	@echo "  clean - Remove test binaries"
