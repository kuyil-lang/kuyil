# Kuyil Programming Language# Kuyil

# Kuyil

<div align="center">

A fast, compiled scripting language designed for quick script and binary generation with built-in HTTP/REST support.

**A modern, fast scripting language with built-in concurrency, FFI support, and comprehensive standard libraries**

## Table of Contents

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)- [Features](#features)

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/kuyil-lang/kuyil)- [Quick Start](#quick-start)

- [Project Structure](#project-structure)

</div>- [Language Syntax](#language-syntax)

- [Building](#building)

## 🌟 Features- [Architecture](#architecture)

- [Documentation](#documentation)

- **⚡ Fast Execution**: Bytecode compilation with VM execution- [License](#license)

- **🔄 Async/Await**: Avatar system for concurrent programming  

- **🔌 FFI System**: Load and use C libraries (.so files) natively## Features

- **📚 Rich Standard Library**: Math, string, HTTP, file I/O, datetime, and more- **Fast Execution**: Bytecode compilation with optional native binary generation

- **🌐 HTTP Built-in**: Native HTTP client and server with JSON support- **HTTP/REST Built-in**: Native HTTP client and server functionality with static file serving

- **📦 Module System**: Import scripts with `import "path"` or `import("path")`- **Static File Serving**: Comprehensive web server with MIME type detection and security

- **🎯 Modern Syntax**: Clean syntax with structs, interfaces, and methods- **Logging Framework**: 5-level logging with automatic context tracking

- **🔧 Cross-platform**: Works on Linux, macOS, and Windows- **Configuration System**: YAML-based configuration with multi-environment support

- **FFI System**: Foreign Function Interface for loading custom .so libraries

## 📥 Quick Start- **Development Tools**: File watching and auto-reload capabilities

- **Simple Syntax**: Clean, readable syntax inspired by modern languages

### Installation- **Binary Generation**: Compile scripts to standalone executables

- **Cross-platform**: Works on Linux, macOS, and Windows

```bash

# Clone the repository## Quick Start

git clone https://github.com/kuyil-lang/kuyil.git

cd kuyil### Build Kuyil

```bash

# Build Kuyil (includes all libraries)make all

make all```



# Run your first program### Run a script

./kuyil examples/hello.kyl```bash

```./kuyil examples/hello.kyl

```

### Hello World

### Compile to binary

```kuyil```bash

print("Hello, World!")./kuyil --compile examples/http_server.kyl -o server

```./server

```

### More Examples

### Run tests

```kuyil```bash

// Variables and operators# Run a specific test

let x = 10./kuyil tests/dynamic_execution_demo.kyl

let y = 20

let sum = x + y# Run all tests

print("Sum:", sum)for test in tests/*test*.kyl; do ./kuyil "$test"; done

```

// New syntax: 'and' and 'or' keywords

let a = true### Docker Quick Start

let b = false```bash

print("a and b:", a and b)   // false# Build Docker image

print("a or b:", a or b)      // true./docker-run.sh build

print("a && b:", a && b)      // Also works!

print("a || b:", a || b)      // Also works!# Run a script in Docker

./docker-run.sh run examples/hello.kyl

// Functions

fn greet(name) {# Run with GUI support (webview, etc.)

    return "Hello, " + name + "!"./docker-run.sh webview

}

# Interactive shell

print(greet("Kuyil"))./docker-run.sh shell

```

// Import modules (both syntaxes work)

import "my_module.kyl"           // New: without parentheses📦 **Docker Documentation:**

import("another_module.kyl")     // Traditional: with parentheses- **[DOCKER_QUICKSTART.md](DOCKER_QUICKSTART.md)** - Get started in 3 steps

- **[DOCKER_DISPLAY_GUIDE.md](DOCKER_DISPLAY_GUIDE.md)** - Platform-specific display forwarding

// Async functions (Avatars)- **[docker-run.sh](docker-run.sh)** - Helper script for common tasks

avatar fn fetchData(url) {

    let response = http.get(url)## Project Structure

    return response.json()

}```

kuyil-lang/

let data = await fetchData("https://api.example.com/data")├── src/           # Core language implementation

print("Data:", data)├── examples/      # Example scripts and tutorials  

```├── tests/         # Test suite and demonstrations

├── docs/          # Documentation and guides

## 📖 Documentation├── build/         # Compiled artifacts

└── README.md      # This file

### Core Language```



- **[Operators](docs/operators.md)** - Arithmetic, logical, comparison operators## Language Syntax

- **[Conditionals](docs/conditionals.md)** - if/else, switch/case

- **[Loops](docs/loops.md)** - for, while, break, continue### Basic Example

- **[Functions](docs/functions.md)** - Function declaration and usage```kuyil

- **[Structs & Methods](docs/structs-and-methods.md)** - Object-oriented programming// Hello World

- **[Avatars (Async)](docs/avatars.md)** - Concurrent programmingprint("Hello, World!")

- **[Import System](docs/import-system.md)** - Module imports

// Variables

### Advanced Featureslet name = "Alice"

let age = 30

- **[FFI Guide](docs/ffi-guide.md)** - Creating and loading .so librarieslet pi = 3.14159

- **[Interface System](docs/INTERFACE_SYSTEM_GUIDE.md)** - Define and use interfaces

// Functions

### Standard Librariesfn greet(name) {

    return "Hello, " + name + "!"

#### Core Libraries (shared_libs/)}

- **[Math Library](docs/libraries/math.md)** - Mathematical functions

- **[String Library](docs/libraries/str.md)** - String manipulationprint(greet("World"))

- **[HTTP Library](docs/libraries/http.md)** - HTTP client and server```

- **[DateTime Library](docs/libraries/datetime.md)** - Date and time operations

- **[File I/O Library](docs/libraries/fileio.md)** - File operations### HTTP Server Example

- **[Event Loop Library](docs/libraries/eventloop.md)** - Event-driven programming```kuyil

- **[System Library](docs/libraries/system.md)** - System operations// Create HTTP server

- **[Async I/O Library](docs/libraries/asyncio.md)** - Asynchronous I/Olet server = http.server(8080)



#### Additional Libraries (additional_libs/)server.get("/", fn(req, res) {

- **[Crypto Library](docs/libraries/crypto.md)** - Cryptographic functions    res.json({"message": "Hello, World!"})

- **[FFmpeg Library](docs/libraries/ffmpeg.md)** - Audio/video processing})

- **[RPC Library](docs/libraries/rpc.md)** - Remote procedure calls

- **[SQLite Library](docs/libraries/sqlite.md)** - Database operationsserver.get("/users/:id", fn(req, res) {

- **[Transcoder Library](docs/libraries/transcoder.md)** - Data encoding/compression    let userId = req.params.id

- **[Webview Library](docs/libraries/webview.md)** - GUI with web technologies    res.json({"user_id": userId, "name": "User " + userId})

})

## 🏗️ Project Structure

print("Server running on http://localhost:8080")

```server.listen()

kuyil/```

├── src/                    # Core language implementation

├── shared_libs/           # Core standard libraries### HTTP Client Example

├── additional_libs/       # Optional extended libraries```kuyil

├── examples/              # Example scripts// Make HTTP requests

├── tests/                 # Test suitelet response = http.get("https://api.github.com/users/octocat")

├── docs/                  # Documentationif response.status == 200 {

└── README.md             # This file    let user = response.json()

```    print("User: " + user.name)

} else {

## 🔧 Building    print("Error: " + response.status)

}

### Requirements

// POST request

- GCC or Clanglet data = {"name": "John", "email": "john@example.com"}

- Makelet postResponse = http.post("https://httpbin.org/post", data)

- libcurl (for HTTP support)print("Response: " + postResponse.text)

```

```bash

# Build everything### Static File Serving Example

make all```kuyil

// Create server with static file support

# Build only interpreterlet server = http.server(8080)

make kuyil

// Set static file root directory

# Build librariesserver.static("/public", "./static")

make libs

// API routes

# Cleanserver.get("/api/status", fn(req, res) {

make clean    res.json({"status": "running", "files": "enabled"})

})

# Run tests

make test// Static files served automatically:

```// GET / -> ./static/index.html

// GET /style.css -> ./static/style.css  

## 🚀 Language Features// GET /app.js -> ./static/app.js

// GET /images/logo.png -> ./static/images/logo.png

See [docs/operators.md](docs/operators.md), [docs/conditionals.md](docs/conditionals.md), and other documentation for detailed syntax.

server.listen()

## 📄 License```



MIT License - see [LICENSE](LICENSE) for details.**Static File Features:**

- 25+ MIME types supported (HTML, CSS, JS, images, fonts, etc.)

## 📮 Contact- Path traversal protection (blocks `../` attacks)

- Multi-threaded request handling

- GitHub: [https://github.com/kuyil-lang/kuyil](https://github.com/kuyil-lang/kuyil)- Caching headers for performance

- Issues: [https://github.com/kuyil-lang/kuyil/issues](https://github.com/kuyil-lang/kuyil/issues)- URL decoding with validation

- Automatic `index.html` serving for `/`

---

## Building

**Made with ❤️ by the Kuyil community**

Requirements:
- GCC or Clang
- Make
- libcurl (for HTTP support)

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install build-essential libcurl4-openssl-dev

# Build
make all

# Run tests
make test
```

## Architecture

```
┌─────────────────┐    ┌──────────────┐    ┌─────────────┐
│   Kuyil Code   │ -> │   Lexer      │ -> │   Tokens    │
└─────────────────┘    └──────────────┘    └─────────────┘
                                                   │
┌─────────────────┐    ┌──────────────┐    ┌─────────────┘
│   Bytecode      │ <- │   Compiler   │ <- │    AST      │
└─────────────────┘    └──────────────┘    └─────────────┘
         │                                          ^
         │              ┌──────────────┐            │
         └─────────────>│   Parser     │────────────┘
                        └──────────────┘
         │
         v
┌─────────────────┐    ┌──────────────┐
│  Virtual Machine│ or │   Native     │
│    (Interpreter)│    │  Compiler    │
└─────────────────┘    └──────────────┘
```

## Documentation

### 📚 User Guides
- **[Quick Start Guide](docs/guides/quick-start.md)** - Get started with Kuyil quickly
- **[Language Reference](docs/guides/language-reference.md)** - Complete language syntax and built-in functions
- **[Shared Library Guide](docs/guides/shared-library.md)** - Creating and using shared libraries

### 🚀 Features
- **[Enhanced Features](docs/features/enhanced-features.md)** - Inline configuration & module imports
- **[FFI System](docs/features/ffi-system.md)** - Foreign Function Interface for loading .so libraries
- **[Configuration System](docs/features/config-system.md)** - YAML-based configuration management
- **[Static File Serving](docs/features/static-files.md)** - HTTP server with static file capabilities
- **[Dynamic Execution](docs/features/dynamic-execution.md)** - Runtime script execution
- **[Library Access](docs/features/library-access.md)** - Programmatic library management
- **[Modular Library System](docs/features/modular-library-system.md)** - Module organization
- **[Backtick Strings](docs/features/backtick-strings.md)** - Advanced string handling

### ⚙️ Implementation Details
- **[Implementation Summary](docs/implementation/summary.md)** - Overall system architecture
- **[FFI Complete](docs/implementation/ffi-complete.md)** - FFI system implementation details
- **[File Reading](docs/implementation/file-reading.md)** - File I/O system implementation
- **[String Interpolation](docs/implementation/string-interpolation.md)** - String processing details
- **[Reactive Programming](docs/implementation/reactive-programming.md)** - Green threads & observables
- **[Reactive Complete](docs/implementation/reactive-complete.md)** - Complete reactive system
- **[Environment Variables](docs/implementation/env-vars.md)** - Environment variable support
- **[Enhanced Metadata](docs/implementation/enhanced-metadata.md)** - FFI metadata management
- **[Startup/Shutdown](docs/implementation/startup-shutdown.md)** - Lifecycle management

### 🛠️ Development
- **[Development Helper](docs/development/development-helper.md)** - File watching and auto-reload tools

### 📊 Project Status
- **[Final Achievement Report](docs/project-status/final-achievement.md)** - FFI system achievement summary
- **[Reactive Analysis](docs/project-status/reactive-analysis.md)** - Comprehensive reactive system analysis

## License
MIT License