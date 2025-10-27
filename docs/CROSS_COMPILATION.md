# Cross-Platform Compilation Guide for Kuyil

This guide explains how to build Kuyil executables for different platforms (Windows, macOS, Linux).

## Table of Contents
- [Native Builds](#native-builds)
- [Cross-Compilation](#cross-compilation)
- [Platform-Specific Notes](#platform-specific-notes)
- [Troubleshooting](#troubleshooting)

## Native Builds

### Linux (Native)
```bash
# Install dependencies
sudo apt-get install build-essential libcurl4-openssl-dev

# Build
make
# or
make all

# The executable will be: kuyil
```

### macOS (Native)
```bash
# Install dependencies
brew install curl

# Build for current architecture
make macos

# Build for Apple Silicon (ARM64)
make macos-arm64

# Build Universal binary (x86_64 + ARM64)
make macos-universal

# The executable will be: kuyil
```

### Windows (Native with MinGW)
```bash
# Using MSYS2/MinGW on Windows
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-curl

# Build
make

# The executable will be: kuyil.exe
```

## Cross-Compilation

### Cross-Compiling to Windows (from Linux)

#### Install MinGW-w64
```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# Fedora
sudo dnf install mingw64-gcc mingw32-gcc

# Arch Linux
sudo pacman -S mingw-w64-gcc
```

#### Build Windows Executables
```bash
# Windows 64-bit (builds executable + shared libraries)
make cross-windows
# Output: kuyil.exe + libs/*.dll

# Windows 32-bit (builds executable + shared libraries)
make cross-windows-32
# Output: kuyil-win32.exe + libs/*-win32.dll

# Build both
make cross-all
```

**Note**: The cross-compilation targets automatically build both:
1. The main executable (`kuyil.exe`)
2. All shared libraries (`libkylmath.dll`, `libkylstr.dll`, `libkylhttp.dll`, `libkyldatetime.dll`, `libkylfileio.dll`)

These libraries must be distributed together with the executable.

#### Required DLLs for Windows
The Windows executable may need these DLLs to run:
- `libcurl.dll` (or `libcurl-x64.dll`)
- `libwinpthread-1.dll`
- `libgcc_s_seh-1.dll` (64-bit) or `libgcc_s_sjlj-1.dll` (32-bit)
- `libstdc++-6.dll`

These can be copied from MinGW installation:
```bash
# Find DLLs (64-bit)
find /usr/x86_64-w64-mingw32 -name "*.dll"

# Copy required DLLs to same directory as kuyil.exe
cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll .
```

Or build statically:
```bash
make cross-windows CFLAGS="-Wall -Wextra -std=c99 -O2 -static -DWIN32"
```

### Cross-Compiling to macOS (from Linux)

#### Install OSXCross
OSXCross allows cross-compiling to macOS from Linux. Setup is complex:

```bash
# Clone OSXCross
git clone https://github.com/tpoechtrager/osxcross
cd osxcross

# Download Xcode SDK (legally required - from Apple Developer)
# Place MacOSX SDK .tar.* in osxcross/tarballs/

# Build OSXCross
./build.sh

# Add to PATH
export PATH="$PATH:/path/to/osxcross/target/bin"
```

#### Build macOS Executable
```bash
# Cross-compile from Linux (builds executable + shared libraries)
make cross-macos
# Output: kuyil-macos + libs/*.dylib

# Or build natively on macOS (builds executable + shared libraries)
make macos
# Output: kuyil + libs/*.dylib
```

**Note**: macOS builds include:
1. The main executable (`kuyil-macos` or `kuyil`)
2. All shared libraries as `.dylib` files

These must be distributed together.

**Note**: macOS cross-compilation is complex due to Apple's licensing requirements. 
**Recommended**: Use native macOS builds or CI/CD (GitHub Actions with macOS runners).

## Platform-Specific Notes

### Windows Considerations
- **libcurl**: Windows build uses WinHTTP API or requires libcurl.dll
- **Threading**: Uses Windows threading APIs instead of pthreads
- **File paths**: Use Windows path separators or normalize paths
- **DLL Dependencies**: Bundle required DLLs with executable

### macOS Considerations
- **Minimum OS Version**: Targets macOS 10.13+ (High Sierra)
- **Apple Silicon**: Use `macos-arm64` target for M1/M2/M3 Macs
- **Universal Binary**: Use `macos-universal` for both Intel and ARM
- **Code Signing**: macOS may require signing for distribution
- **Notarization**: Required for Gatekeeper approval

### Linux Considerations
- **glibc vs musl**: Standard build uses glibc
- **Static Linking**: Possible but increases binary size
- **Dependencies**: libcurl, pthread are standard on most systems

## Build Targets Summary

| Target | Platform | Architecture | Output Files |
|--------|----------|--------------|-------------|
| `make` | Linux | x86_64 | `kuyil` + `libs/*.so` |
| `make cross-windows` | Windows | x86_64 | `kuyil.exe` + `libs/*.dll` |
| `make cross-windows-32` | Windows | i686 | `kuyil-win32.exe` + `libs/*-win32.dll` |
| `make cross-macos` | macOS | x86_64 | `kuyil-macos` + `libs/*.dylib` |
| `make macos` | macOS | native | `kuyil` + `libs/*.dylib` |
| `make macos-arm64` | macOS | ARM64 | `kuyil-arm64` + `libs/*.dylib` |
| `make macos-universal` | macOS | Universal | `kuyil` + `libs/*.dylib` |

**Shared Libraries Built**: `math`, `str`, `http`, `datetime`, `fileio`

## CI/CD Cross-Platform Builds

### GitHub Actions Example
```yaml
name: Cross-Platform Build

on: [push, pull_request]

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build Linux
        run: make
      - name: Upload Linux Binary
        uses: actions/upload-artifact@v3
        with:
          name: kuyil-linux
          path: kuyil

  build-windows:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install MinGW
        run: sudo apt-get install mingw-w64
      - name: Build Windows
        run: make cross-windows
      - name: Upload Windows Binary
        uses: actions/upload-artifact@v3
        with:
          name: kuyil-windows
          path: kuyil.exe

  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install Dependencies
        run: brew install curl
      - name: Build macOS Universal
        run: make macos-universal
      - name: Upload macOS Binary
        uses: actions/upload-artifact@v3
        with:
          name: kuyil-macos
          path: kuyil
```

## Troubleshooting

### MinGW libcurl Issues
If libcurl is not available in MinGW:
```bash
# Use WinHTTP instead (Windows native HTTP)
# Modify LIBS in Makefile for cross-windows target:
# LIBS = -lws2_32 -lwinhttp -lpthread -lm
```

### Static Linking
To create fully static binaries (no DLL dependencies):
```bash
# Linux
make CFLAGS="-Wall -Wextra -std=c99 -O2 -static" LIBS="-lcurl -lpthread -lm -static-libgcc"

# Windows
make cross-windows CFLAGS="-Wall -Wextra -std=c99 -O2 -static -DWIN32"
```

### Size Optimization
Reduce binary size:
```bash
make release
strip kuyil
upx kuyil  # Further compression (requires upx)
```

### Testing Cross-Compiled Binaries

#### Test Windows binary on Linux (using Wine)
```bash
sudo apt-get install wine64
wine kuyil.exe --version
```

#### Test on actual target platform
- Copy binary to target system
- Ensure dependencies are installed
- Test with: `./kuyil examples/hello.kyl`

## Distribution Checklist

- [ ] Build for all target platforms
- [ ] Test on actual hardware/OS
- [ ] Bundle shared libraries (.dll/.dylib/.so) with executable
- [ ] Verify library paths and runtime linking
- [ ] Include required system DLLs (Windows)
- [ ] Sign binaries (macOS, Windows for production)
- [ ] Create installers/packages
- [ ] Include README and documentation
- [ ] Verify permissions (executable bit on Unix)
- [ ] Test with antivirus software (false positives)
- [ ] Document library dependencies

## Platform-Specific Library Notes

### Windows (.dll files)
```
kuyil.exe
libs/
  libkylmath.dll
  libkylstr.dll
  libkylhttp.dll
  libkyldatetime.dll
  libkylfileio.dll
```

### macOS (.dylib files)
```
kuyil
libs/
  libkylmath.dylib
  libkylstr.dylib
  libkylhttp.dylib
  libkyldatetime.dylib
  libkylfileio.dylib
```

### Linux (.so files)
```
kuyil
libs/
  libkylmath.so
  libkylstr.so
  libkylhttp.so
  libkyldatetime.so
  libkylfileio.so
```

The Kuyil runtime automatically searches for libraries in:
1. `./libs/` (relative to executable)
2. System library paths

## Quick Start Commands

```bash
# Show all available targets
make help

# Show cross-compilation toolchain installation
make install-cross-tools

# Build for current platform
make

# Build for Windows (from Linux)
make cross-windows

# Build for macOS (on macOS)
make macos-universal

# Build all Windows binaries
make cross-all

# Clean and rebuild
make clean && make release
```

## Support

- **Issues**: Report cross-compilation issues on GitHub
- **Documentation**: See main README.md for general build instructions
- **Community**: Join discussions for platform-specific help
