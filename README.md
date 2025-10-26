# Kuyil
# Kuyil

A fast, compiled scripting language designed for quick script and binary generation with built-in HTTP/REST support.

## Table of Contents
- [Features](#features)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Language Syntax](#language-syntax)
- [Building](#building)
- [Architecture](#architecture)
- [Documentation](#documentation)
- [License](#license)

## Features
- **Fast Execution**: Bytecode compilation with optional native binary generation
- **HTTP/REST Built-in**: Native HTTP client and server functionality with static file serving
- **Static File Serving**: Comprehensive web server with MIME type detection and security
- **Logging Framework**: 5-level logging with automatic context tracking
- **Configuration System**: YAML-based configuration with multi-environment support
- **FFI System**: Foreign Function Interface for loading custom .so libraries
- **Development Tools**: File watching and auto-reload capabilities
- **Simple Syntax**: Clean, readable syntax inspired by modern languages
- **Binary Generation**: Compile scripts to standalone executables
- **Cross-platform**: Works on Linux, macOS, and Windows

## Quick Start

### Build Kuyil
```bash
make all
```

### Run a script
```bash
./kuyil examples/hello.kyl
```

### Compile to binary
```bash
./kuyil --compile examples/http_server.kyl -o server
./server
```

### Run tests
```bash
# Run a specific test
./kuyil tests/dynamic_execution_demo.kyl

# Run all tests
for test in tests/*test*.kyl; do ./kuyil "$test"; done
```

## Project Structure

```
kuyil-lang/
├── src/           # Core language implementation
├── examples/      # Example scripts and tutorials  
├── tests/         # Test suite and demonstrations
├── docs/          # Documentation and guides
├── build/         # Compiled artifacts
└── README.md      # This file
```

## Language Syntax

### Basic Example
```kuyil
// Hello World
print("Hello, World!")

// Variables
let name = "Alice"
let age = 30
let pi = 3.14159

// Functions
fn greet(name) {
    return "Hello, " + name + "!"
}

print(greet("World"))
```

### HTTP Server Example
```kuyil
// Create HTTP server
let server = http.server(8080)

server.get("/", fn(req, res) {
    res.json({"message": "Hello, World!"})
})

server.get("/users/:id", fn(req, res) {
    let userId = req.params.id
    res.json({"user_id": userId, "name": "User " + userId})
})

print("Server running on http://localhost:8080")
server.listen()
```

### HTTP Client Example
```kuyil
// Make HTTP requests
let response = http.get("https://api.github.com/users/octocat")
if response.status == 200 {
    let user = response.json()
    print("User: " + user.name)
} else {
    print("Error: " + response.status)
}

// POST request
let data = {"name": "John", "email": "john@example.com"}
let postResponse = http.post("https://httpbin.org/post", data)
print("Response: " + postResponse.text)
```

### Static File Serving Example
```kuyil
// Create server with static file support
let server = http.server(8080)

// Set static file root directory
server.static("/public", "./static")

// API routes
server.get("/api/status", fn(req, res) {
    res.json({"status": "running", "files": "enabled"})
})

// Static files served automatically:
// GET / -> ./static/index.html
// GET /style.css -> ./static/style.css  
// GET /app.js -> ./static/app.js
// GET /images/logo.png -> ./static/images/logo.png

server.listen()
```

**Static File Features:**
- 25+ MIME types supported (HTML, CSS, JS, images, fonts, etc.)
- Path traversal protection (blocks `../` attacks)
- Multi-threaded request handling
- Caching headers for performance
- URL decoding with validation
- Automatic `index.html` serving for `/`

## Building

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