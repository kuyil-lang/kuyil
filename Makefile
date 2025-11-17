# Kuyil Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -fPIC
LIBS = -lcurl -lpthread -lm -ldl -levent
SRCDIR = src

# Check if embedded_resources.c exists, include it automatically
EMBED_SRC := $(wildcard $(SRCDIR)/embedded_resources.c)
ifneq ($(EMBED_SRC),)
    $(info Found embedded_resources.c, including in build)
endif

# Optional: Support legacy EMBED_DIR variable for backward compatibility
ifdef EMBED_DIR
    EMBED_SRC = $(SRCDIR)/embedded_resources.c
    $(info Using EMBED_DIR=$(EMBED_DIR), will generate embedded_resources.c)
endif

# Source files  
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/vm.c $(SRCDIR)/vm_call_shared.c $(SRCDIR)/logging.c $(SRCDIR)/config.c $(SRCDIR)/ffi.c $(SRCDIR)/file_reader.c $(SRCDIR)/green_threads.c $(SRCDIR)/library_loader.c $(SRCDIR)/vm_library_integration.c $(SRCDIR)/vm_task_queue.c $(SRCDIR)/async_http.c $(SRCDIR)/thread_pool.c $(SRCDIR)/avatar_runtime.c $(SRCDIR)/async_request_queue.c $(SRCDIR)/opcode_executor.c $(SRCDIR)/heapfs.c $(EMBED_SRC)
HEADERS = $(SRCDIR)/tokens.h $(SRCDIR)/ast.h $(SRCDIR)/bytecode.h $(SRCDIR)/vm.h $(SRCDIR)/vm_call_shared.h $(SRCDIR)/logging.h $(SRCDIR)/config.h $(SRCDIR)/ffi.h $(SRCDIR)/file_reader.h $(SRCDIR)/green_threads.h $(SRCDIR)/library_loader.h $(SRCDIR)/vm_library_integration.h $(SRCDIR)/async_http.h $(SRCDIR)/thread_pool.h $(SRCDIR)/avatar_runtime.h $(SRCDIR)/async_request_queue.h $(SRCDIR)/opcode_executor.h $(SRCDIR)/heapfs.h

# Target executable
TARGET = kuyil

# Main target - MODIFIED: Now runs 'clean', 'directories', 'libs', then '$(TARGET)'
all: directories libs $(TARGET)

# Build with embedded resources
embed: embed-resources all

# Generate embedded resources using shell script
embed-resources:
ifndef EMBED_DIR
	$(error EMBED_DIR is not set. Use: make embed EMBED_DIR=path/to/resources)
endif
	@echo "Embedding resources from $(EMBED_DIR) and interfaces/..."
	./tools/embed_resources.sh $(SRCDIR)/embedded_resources.c $(EMBED_DIR) interfaces

# Build shared libraries (CORE LIBS via Makefile.libs)
libs:
	$(MAKE) -f Makefile.libs all

$(TARGET): $(SOURCES) $(HEADERS) libs
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS) -rdynamic

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -O3
release: clean $(TARGET)

# Install dependencies (Ubuntu/Debian)
deps:
	sudo apt-get update
	sudo apt-get install -y build-essential libcurl4-openssl-dev

# Install dependencies (macOS)
deps-mac:
	brew install curl

# Install dependencies (CentOS/RHEL)
deps-centos:
	sudo yum install -y gcc curl-devel

# Install cross-compilation dependencies
deps-cross-windows:
	@echo "Installing MinGW-w64 for Windows cross-compilation..."
	sudo apt-get update
	sudo apt-get install -y mingw-w64
	@echo "MinGW-w64 installed successfully!"
	@echo "You can now use: make cross-windows or make cross-all"

# Cross-compilation targets
# Windows cross-compilation (requires mingw-w64)
cross-windows: CC = x86_64-w64-mingw32-gcc
cross-windows: TARGET = kuyil.exe
cross-windows: LIBS = -lws2_32 -lwinhttp -lpthread -lm -lpsapi
cross-windows: CFLAGS += -DWIN32 -D_WIN32_WINNT=0x0600
cross-windows:
	@if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then \
		echo "Error: MinGW-w64 not found. Install with:"; \
		echo "  Ubuntu/Debian: sudo apt-get install mingw-w64"; \
		echo "  Fedora: sudo dnf install mingw64-gcc"; \
		echo "  Arch: sudo pacman -S mingw-w64-gcc"; \
		exit 1; \
	fi
	@echo "Cross-compiling for Windows (x86_64)..."
	@echo "Building shared libraries for Windows..."
	$(MAKE) -f Makefile.libs cross-windows
	@echo "Building main executable for Windows..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "Windows executable created: $(TARGET)"
	@echo "Windows libraries created in libs/ (.dll files)"
	@echo "Note: Windows builds may need additional DLLs to run"

# Windows 32-bit cross-compilation
cross-windows-32: CC = i686-w64-mingw32-gcc
cross-windows-32: TARGET = kuyil-win32.exe
cross-windows-32: LIBS = -lws2_32 -lwinhttp -lpthread -lm -lpsapi
cross-windows-32: CFLAGS += -DWIN32 -D_WIN32_WINNT=0x0600
cross-windows-32:
	@if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then \
		echo "Error: MinGW-w64 32-bit not found. Install with:"; \
		echo "  Ubuntu/Debian: sudo apt-get install mingw-w64"; \
		exit 1; \
	fi
	@echo "Cross-compiling for Windows (i686/32-bit)..."
	@echo "Building shared libraries for Windows 32-bit..."
	$(MAKE) -f Makefile.libs cross-windows-32
	@echo "Building main executable for Windows 32-bit..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "Windows 32-bit executable created: $(TARGET)"
	@echo "Windows 32-bit libraries created in libs/ (.dll files)"

# macOS cross-compilation (requires osxcross)
cross-macos: CC = x86_64-apple-darwin20.4-clang
cross-macos: TARGET = kuyil-macos
cross-macos: LIBS = -lcurl -lpthread -lm
cross-macos:
	@if ! command -v x86_64-apple-darwin20.4-clang >/dev/null 2>&1; then \
		echo "Error: OSXCross not found."; \
		echo "OSXCross setup is complex. See: https://github.com/tpoechtrager/osxcross"; \
		echo "Alternative: Build natively on macOS using 'make' or 'make macos'"; \
		exit 1; \
	fi
	@echo "Cross-compiling for macOS (x86_64)..."
	@echo "Building shared libraries for macOS..."
	$(MAKE) -f Makefile.libs cross-macos
	@echo "Building main executable for macOS..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "macOS executable created: $(TARGET)"
	@echo "macOS libraries created in libs/ (.dylib files)"

# Native macOS build (when running on macOS)
macos: CC = clang
macos: TARGET = kuyil
macos: LIBS = -lcurl -lpthread -lm
macos: CFLAGS += -mmacosx-version-min=10.13
macos:
	@echo "Building for macOS (native)..."
	@echo "Building shared libraries for macOS..."
	$(MAKE) -f Makefile.libs macos
	@echo "Building main executable for macOS..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "macOS executable created: $(TARGET)"
	@echo "macOS libraries created in libs/ (.dylib files)"

# ARM64 (Apple Silicon) build
macos-arm64: CC = clang
macos-arm64: TARGET = kuyil-arm64
macos-arm64: LIBS = -lcurl -lpthread -lm
macos-arm64: CFLAGS += -arch arm64 -mmacosx-version-min=11.0
macos-arm64:
	@echo "Building for macOS ARM64 (Apple Silicon)..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LIBS)
	@echo "macOS ARM64 executable created: $(TARGET)"

# Universal macOS binary (x86_64 + ARM64)
macos-universal: macos-arm64
	@echo "Building x86_64 version..."
	@$(MAKE) CC=clang TARGET=kuyil-x86_64 CFLAGS="$(CFLAGS) -arch x86_64 -mmacosx-version-min=10.13" $(TARGET)
	@echo "Creating universal binary..."
	lipo -create kuyil-x86_64 kuyil-arm64 -output kuyil
	@echo "Universal macOS binary created: kuyil"
	@rm -f kuyil-x86_64 kuyil-arm64

# Install cross-compilation toolchains
install-cross-tools:
	@echo "Installing cross-compilation tools..."
	@echo ""
	@echo "For Windows (MinGW-w64):"
	@echo "  Ubuntu/Debian: sudo apt-get install mingw-w64"
	@echo "  Fedora:        sudo dnf install mingw64-gcc mingw32-gcc"
	@echo "  Arch Linux:    sudo pacman -S mingw-w64-gcc"
	@echo ""
	@echo "For macOS (OSXCross) - Complex setup required:"
	@echo "  See: https://github.com/tpoechtrager/osxcross"
	@echo "  Requires: Xcode SDK from Apple"
	@echo ""
	@echo "Alternatively, build natively on the target platform"

# Build all cross-platform binaries (requires all toolchains)
cross-all: cross-windows cross-windows-32
	@echo ""
	@echo "Cross-compilation complete!"
	@echo "Windows 64-bit: kuyil.exe"
	@echo "Windows 32-bit: kuyil-win32.exe"
	@echo ""
	@echo "Note: macOS cross-compilation requires OSXCross setup"
	@echo "For macOS, build natively or use CI/CD (GitHub Actions)"

# Test the build
test: $(TARGET)
	@echo "Testing Kuyil build..."
	@echo 'print("Hello from Kuyil!")' | ./$(TARGET) -
	@echo "Testing logging system..."
	@echo 'log_info("Testing Kuyil logging system")' | ./$(TARGET) --log-level debug -
	@echo "Build test completed successfully!"

# Performance test
perf: $(TARGET)
	@echo "Performance test - Computing factorial(20) 1000 times..."
	@echo 'fn fact(n) { if n <= 1 { return 1 } return n * fact(n-1) } let i = 0 while i < 1000 { fact(20) i = i + 1 } print("Completed 1000 factorial calculations")' | time ./$(TARGET) -

# Memory test with valgrind (if available)
memtest: $(TARGET)
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running memory test with valgrind..."; \
		echo 'print("Memory test")' | valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) -; \
	else \
		echo "Valgrind not found. Install with: sudo apt-get install valgrind"; \
	fi

# Code analysis with cppcheck (if available)
analyze:
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "Running static analysis..."; \
		cppcheck --enable=all --std=c99 $(SRCDIR)/; \
	else \
		echo "cppcheck not found. Install with: sudo apt-get install cppcheck"; \
	fi

# Format code (if clang-format is available)
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting source code..."; \
		clang-format -i $(SRCDIR)/*.c $(SRCDIR)/*.h; \
	else \
		echo "clang-format not found. Install with: sudo apt-get install clang-format"; \
	fi

# Create distribution package
dist: clean release
	@echo "Creating distribution package..."
	mkdir -p kuyil-dist
	cp $(TARGET) kuyil-dist/
	cp README.md kuyil-dist/
	cp -r docs kuyil-dist/
	tar -czf kuyil-1.0.0.tar.gz kuyil-dist/
	rm -rf kuyil-dist/
	@echo "Distribution package created: kuyil-1.0.0.tar.gz"

# Install system-wide (requires sudo)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Kuyil installed to /usr/local/bin/$(TARGET)"

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	sudo rm -rf /usr/local/share/kuyil/
	@echo "Kuyil uninstalled"


# Benchmark against other languages (if available)
benchmark: $(TARGET)
	@echo "Benchmarking Kuyil..."
	@echo "1. Fibonacci calculation (Kuyil):"
	@echo 'fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) } print("fib(30) = " + fib(30))' | time ./$(TARGET) -
	@if command -v python3 >/dev/null 2>&1; then \
		echo "2. Fibonacci calculation (Python3):"; \
		echo 'def fib(n): return n if n <= 1 else fib(n-1) + fib(n-2); print("fib(30) =", fib(30))' | time python3; \
	fi
	@if command -v node >/dev/null 2>&1; then \
		echo "3. Fibonacci calculation (Node.js):"; \
		echo 'function fib(n) { return n <= 1 ? n : fib(n-1) + fib(n-2); } console.log("fib(30) =", fib(30));' | time node; \
	fi

# Documentation generation (if pandoc is available)
docs:
	@if command -v pandoc >/dev/null 2>&1; then \
		echo "Generating documentation..."; \
		pandoc README.md -o docs/kuyil-manual.html; \
		pandoc README.md -o docs/kuyil-manual.pdf; \
		echo "Documentation generated in docs/"; \
	else \
		echo "pandoc not found. Install with: sudo apt-get install pandoc"; \
	fi

# Clean build artifacts - MODIFIED with '-' to ignore errors
clean:
	-rm -f $(TARGET)
	-rm -rf $(BUILDDIR)
	-rm -f *.o *.core core
	-rm -f kuyil-*.tar.gz
	-rm -f hello_binary *_compiled
	$(MAKE) -f Makefile.libs clean

# Show help
help:
	@echo "Kuyil Build System"
	@echo "=================="
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build Kuyil (default, runs clean, directories, libs and main executable)"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  test         - Test the build"
	@echo "  test-http    - Test HTTP functionality"
	@echo "  perf         - Run performance test"
	@echo "  memtest      - Run memory leak test (requires valgrind)"
	@echo "  analyze      - Run static code analysis (requires cppcheck)"
	@echo "  format       - Format source code (requires clang-format)"
	@echo "  benchmark    - Benchmark against other languages"
	@echo "  dist         - Create distribution package"
	@echo "  install      - Install system-wide (requires sudo)"
	@echo "  uninstall    - Uninstall from system"
	@echo "  docs         - Generate documentation (requires pandoc)"
	@echo "  clean        - Clean build artifacts"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Cross-compilation targets:"
	@echo "  cross-windows      - Build Windows 64-bit executable (requires mingw-w64)"
	@echo "  cross-windows-32   - Build Windows 32-bit executable (requires mingw-w64)"
	@echo "  cross-macos        - Build macOS executable (requires osxcross)"
	@echo "  macos              - Build macOS native (on macOS)"
	@echo "  macos-arm64        - Build macOS ARM64/Apple Silicon (on macOS)"
	@echo "  macos-universal    - Build Universal macOS binary (on macOS)"
	@echo "  cross-all          - Build all Windows binaries"
	@echo "  install-cross-tools - Show cross-compilation toolchain installation"
	@echo ""
	@echo "Dependency installation:"
	@echo "  deps         - Install dependencies (Ubuntu/Debian)"
	@echo "  deps-mac     - Install dependencies (macOS)"
	@echo "  deps-centos  - Install dependencies (CentOS/RHEL)"
	@echo "  deps-cross-windows - Install MinGW-w64 for Windows cross-compilation"

.PHONY: all libs debug release test perf memtest analyze format dist install uninstall compile-example benchmark docs clean help deps deps-mac deps-centos deps-cross-windows cross-windows cross-windows-32 cross-macos macos macos-arm64 macos-universal cross-all install-cross-tools directories