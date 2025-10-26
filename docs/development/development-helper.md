# Kuyil Development Helper Service

The Kuyil programming language now includes a comprehensive **Development Helper Service** that provides file watching and auto-reload capabilities for faster development workflows.

## 🚀 Features

### Core Development Helper Functions

- **`dev_watch_file(filepath)`** - Watch a specific file for changes
- **`dev_watch_dir(directory)`** - Watch all `.kyl`, `.js`, `.css`, `.html` files in a directory
- **`dev_start_watching()`** - Start the file watcher service
- **`dev_stop_watching()`** - Stop the file watcher service

### File Change Detection

- **Real-time monitoring** - Checks file modification timestamps every 500ms
- **Multi-threaded** - Uses POSIX threads for non-blocking file watching
- **Smart filtering** - Only watches relevant file types (`.kyl`, `.js`, `.css`, `.html`)
- **Change notifications** - Logs when files are modified

### Auto-Reload Capabilities

- **Callback support** - Set custom reload handlers
- **Graceful reloading** - Built-in reload logic with error handling
- **Development mode** - Enable/disable dev features via configuration

## 📖 Usage Examples

### Basic File Watching

```kuyil
// Watch specific files
dev_watch_file("examples/my_server.kyl")
dev_watch_file("src/my_module.kyl")

// Watch entire directories
dev_watch_dir("examples")
dev_watch_dir("src")

// Start monitoring
dev_start_watching()

// Your application code here...

// Stop when done
dev_stop_watching()
```

### HTTP Server with Auto-Reload

```kuyil
// Server configuration
let config = {
    "port": 8080,
    "dev_mode": true,
    "auto_reload": true
}

// Create server
let server = http.server(config.port)

// Setup development helper
if config.dev_mode {
    log_info("Setting up development helper service...")
    
    // Watch server files
    dev_watch_file("examples/my_server.kyl")
    dev_watch_dir("examples")
    
    // Start file watcher
    if config.auto_reload {
        dev_start_watching()
        log_info("File watcher started - server will reload on changes")
    }
}

// Define your routes...
server.get("/", fn(req, res) {
    res.json({"message": "Hello from Kuyil dev server!"})
})

// Start server
server.listen()
```

### Development Status Endpoints

The development helper integrates with HTTP servers to provide status endpoints:

```kuyil
// Development status endpoint
server.get("/dev/status", fn(req, res) {
    if !config.dev_mode {
        res.status(403).json({"error": "Dev mode disabled"})
        return
    }
    
    let status = {
        "dev_mode": config.dev_mode,
        "auto_reload": config.auto_reload,
        "watched_files": get_watched_files(),
        "watcher_status": "active"
    }
    
    res.json(status)
})

// Manual reload trigger
server.post("/dev/reload", fn(req, res) {
    log_info("Manual reload triggered via API")
    handle_reload()
    res.json({"status": "reloaded"})
})
```

## 🛠 Technical Implementation

### File System Monitoring

- **POSIX-compliant** - Uses `stat()` and `dirent.h` for cross-platform compatibility
- **Efficient polling** - 500ms check intervals to balance responsiveness and CPU usage
- **Thread-safe** - Proper threading with pthread library
- **Memory management** - Dynamic arrays with proper cleanup

### Integration Points

- **VM Integration** - Native functions registered in Kuyil virtual machine
- **HTTP Server** - Built-in development endpoints and middleware
- **Logging System** - Full integration with Kuyil logging framework
- **Error Handling** - Graceful degradation when file operations fail

## 📁 File Structure

```
kuyil-lang/
├── src/
│   ├── http.h              # Development helper API definitions
│   ├── http.c              # Implementation with file watching
│   ├── vm.c                # VM integration and native functions
│   └── ...
├── examples/
│   ├── dev_server.kyl        # Full development server example
│   ├── simple_dev_test.kyl   # Basic file watching test
│   └── dev_helper_demo.kyl   # Comprehensive demo
└── README_DEV_HELPER.md    # This documentation
```

## 🚦 API Reference

### Development Helper Functions

#### `dev_watch_file(filepath)`
- **Parameters**: `filepath` (string) - Path to file to watch
- **Returns**: `boolean` - Success status
- **Description**: Adds a file to the watch list for change detection

#### `dev_watch_dir(directory)`
- **Parameters**: `directory` (string) - Directory path to watch
- **Returns**: `boolean` - Success status  
- **Description**: Recursively watches all supported files in directory

#### `dev_start_watching()`
- **Parameters**: None
- **Returns**: `boolean` - Success status
- **Description**: Starts the file watcher background thread

#### `dev_stop_watching()`
- **Parameters**: None
- **Returns**: `boolean` - Success status
- **Description**: Stops the file watcher and cleans up resources

### HTTP Development Endpoints

#### `GET /dev/status`
Returns development service status and configuration

#### `GET /dev/files` 
Lists all currently watched files and directories

#### `POST /dev/reload`
Triggers a manual reload of the server

## 🎯 Use Cases

### 1. **Rapid API Development**
- Watch API endpoint files
- Auto-reload server when routes change
- Real-time testing without manual restarts

### 2. **Full-Stack Development**
- Monitor frontend assets (CSS, JS, HTML)
- Watch backend Kuyil server files
- Coordinate full-stack reload workflows

### 3. **Microservice Development**  
- Watch service configuration files
- Monitor multiple service files
- Development-time service discovery

### 4. **Testing and Debugging**
- Watch test files for automatic re-runs
- Monitor log configurations
- Debug file-based workflows

## ⚙️ Configuration Options

```kuyil
let dev_config = {
    "enabled": true,           // Enable/disable dev helper
    "auto_reload": true,       // Automatic reload on changes
    "watch_interval": 500,     // File check interval (ms)
    "file_types": [".kyl", ".js", ".css", ".html"],  // Watched extensions
    "exclude_dirs": ["build", "dist", ".git"],      // Ignored directories
    "reload_delay": 1000,      // Delay before reload (ms)
    "verbose_logging": true    // Extra debug information
}
```

## 🔧 Building and Running

### Prerequisites
- GCC with C99 support
- POSIX-compliant system (Linux, macOS, WSL)
- libcurl, pthread, and math libraries

### Build Commands
```bash
# Clean build
make clean

# Compile with development helper
gcc -std=c99 -O2 -g src/main.c src/vm.c src/http.c src/logging.c \
    -o kuyil -lcurl -lpthread -lm

# Run development server
./kuyil examples/dev_server.kyl

# Test file watching
./kuyil examples/simple_dev_test.kyl
```

### Testing
```bash
# Basic functionality test
./kuyil examples/simple_dev_test.kyl

# Start development server
./kuyil examples/dev_server.kyl &

# Test development endpoints
curl http://localhost:8080/dev/status
curl http://localhost:8080/dev/files
curl -X POST http://localhost:8080/dev/reload
```

## 🐛 Troubleshooting

### Common Issues

1. **File watcher not starting**
   - Check pthread library is linked
   - Verify file permissions
   - Ensure POSIX compliance

2. **Files not being detected**
   - Verify file paths are absolute
   - Check file extensions are supported
   - Ensure files exist and are readable

3. **High CPU usage**
   - Reduce watch interval
   - Limit number of watched files
   - Exclude large directories

### Debug Commands
```kuyil
log_info("Dev helper status: " + dev_helper_status())
log_debug("Watched files: " + watched_file_count())
log_debug("Last change: " + last_file_change())
```

## 🚀 Future Enhancements

- **WebSocket integration** for real-time browser reload
- **Smart dependency tracking** - reload dependent files
- **Configuration file watching** - dynamic config updates
- **Plugin system** for custom reload handlers
- **IDE integration** - VS Code extension support
- **Hot module replacement** - update modules without full restart

---

The Kuyil Development Helper Service provides a complete solution for modern development workflows, enabling rapid iteration and testing cycles. Combined with the comprehensive logging framework and HTTP server capabilities, it creates a powerful development environment for building fast, reliable applications.