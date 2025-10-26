# Kuyil Enhanced Features: Inline Configuration & Module Imports

## Overview

Kuyil now supports two major enhancements that make applications more self-contained and modular:

1. **Inline Library Configuration** - Configure shared libraries directly in Kuyil code instead of external files
2. **Module Import System** - Import functions from other Kuyil `.kyl` files for code reuse

## Feature 1: Inline Library Configuration

### Problem Solved
Previously, libraries were configured in external `libraries.conf` files, making applications dependent on external configuration. Now you can configure everything in code.

### New Functions

```kuyil
// Clear all existing library configurations  
clear_libraries();

// Add a library configuration
// add_library(name, path, optional)
add_library("webview", "/usr/lib/libwebview.so", true);
add_library("sqlite", "/usr/lib/libsqlite3.so", false);

// Load a specific library by name
load_library("webview");
load_library("sqlite");
```

### Example Usage

```kuyil
// Dynamic platform-specific library loading
function configure_ui_library() {
    var platform = get_platform(); // Hypothetical function
    
    clear_libraries();
    
    if (platform == "linux") {
        add_library("gtk", "/usr/lib/libgtk-3.so", true);
    } else if (platform == "windows") {
        add_library("win32ui", "C:\\Windows\\System32\\user32.dll", true);
    } else if (platform == "macos") {
        add_library("cocoa", "/System/Library/Frameworks/Cocoa.framework", true);
    }
    
    return load_library(platform == "linux" ? "gtk" : 
                       platform == "windows" ? "win32ui" : "cocoa");
}

// Feature-based library loading
function configure_optional_features(config) {
    if (config.enable_crypto) {
        add_library("crypto", "/usr/lib/libcrypto.so", true);
        load_library("crypto");
    }
    
    if (config.enable_database) {
        add_library("sqlite", "/usr/lib/libsqlite3.so", false); // Required
        load_library("sqlite");
    }
    
    if (config.enable_networking) {
        add_library("curl", "/usr/lib/libcurl.so", true);
        load_library("curl");
    }
}
```

### Benefits
- **Self-contained applications** - No external config files needed
- **Dynamic configuration** - Libraries loaded based on runtime conditions
- **Platform adaptation** - Different libraries for different platforms
- **Feature toggles** - Load only needed functionality

## Feature 2: Module Import System

### Problem Solved
Code reuse across Kuyil projects required copying functions. Now you can create reusable modules and import them.

### New Functions

```kuyil
// Import functions from another Kuyil module
var module_name = import("path/to/module.kyl");

// Mark a function for export (in the module file)
export_function("function_name");
```

### Creating a Module

**File: `modules/math_utils.kyl`**
```kuyil
// Mark functions for export
export_function("factorial");
export_function("fibonacci");
export_function("is_prime");

function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

function fibonacci(n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

function is_prime(n) {
    if (n < 2) return false;
    for (var i = 2; i * i <= n; i = i + 1) {
        if (n % i == 0) return false;
    }
    return true;
}
```

### Using the Module

**File: `main.kyl`**
```kuyil
// Import the math utilities
var math_utils = import("modules/math_utils.kyl");

// Use the imported functions
print("5! = " + factorial(5));
print("fibonacci(8) = " + fibonacci(8));
print("Is 17 prime? " + is_prime(17));
```

### Advanced Module Patterns

```kuyil
// Conditional module loading
var app_config = load_app_config();

if (app_config.features.includes("advanced_math")) {
    import("modules/advanced_math.kyl");
}

if (app_config.features.includes("string_processing")) {
    import("modules/string_utils.kyl");
}

// Namespace-style organization
import("modules/database/connection.kyl");
import("modules/database/queries.kyl");
import("modules/ui/components.kyl");
import("modules/ui/themes.kyl");

// Version-specific imports (future enhancement)
// import("modules/api_client_v2.kyl");
```

## Complete Application Example

```kuyil
// Application startup function demonstrating both features
function startup_initialize_app() {
    print("Initializing application...");
    
    // 1. Configure libraries inline based on environment
    var env = get_environment(); // dev, staging, prod
    
    clear_libraries();
    
    // Core libraries (always needed)
    add_library("sqlite", "/usr/lib/libsqlite3.so", false);
    
    // Environment-specific libraries
    if (env == "dev") {
        add_library("debug", "/usr/lib/libdebug.so", true);
    }
    
    // Feature-based libraries
    var features = get_enabled_features();
    if (features.includes("ui")) {
        add_library("webview", "/usr/lib/libwebview.so", true);
    }
    if (features.includes("crypto")) {
        add_library("crypto", "/usr/lib/libcrypto.so", true);
    }
    
    // Load all configured libraries
    var lib_count = get_library_count();
    for (var i = 0; i < lib_count; i = i + 1) {
        var name = get_library_name(i);
        load_library(name);
    }
    
    // 2. Import required modules
    import("modules/config.kyl");
    import("modules/database.kyl");
    import("modules/logging.kyl");
    
    // Conditionally import feature modules
    if (features.includes("api")) {
        import("modules/http_client.kyl");
    }
    if (features.includes("data_processing")) {
        import("modules/data_transform.kyl");
    }
    
    // 3. Show initialization summary
    var loaded_libs = 0;
    for (var i = 0; i < lib_count; i = i + 1) {
        if (is_library_loaded(i)) {
            loaded_libs = loaded_libs + 1;
        }
    }
    
    print("Application initialized:");
    print("  Environment: " + env);
    print("  Libraries loaded: " + loaded_libs + "/" + lib_count);
    print("  Total functions: " + get_total_function_count());
    
    return {
        status: "ready",
        libraries: loaded_libs,
        functions: get_total_function_count()
    };
}

// Start the application
var init_result = startup_initialize_app();
print("Application status: " + init_result.status);
```

## Implementation Status

### ✅ Implemented
- Inline library configuration functions (`add_library`, `load_library`, `clear_libraries`)
- Module import system framework (`import`, `export_function`)
- Integration with existing library introspection system
- Function registration in VM global namespace

### 🔄 In Progress  
- Full module parsing and execution
- Namespace-based function access
- Cross-module dependency resolution

### 🚀 Future Enhancements
- Module versioning and compatibility checking
- Circular dependency detection  
- Module caching and reload capabilities
- Package manager integration

## Usage Guidelines

### Best Practices

1. **Library Configuration**
   - Use `clear_libraries()` at startup for clean state
   - Configure required libraries as non-optional
   - Load libraries conditionally based on features

2. **Module Organization**
   - Group related functions in modules
   - Use descriptive module and function names
   - Export only public interface functions

3. **Application Structure**
   ```
   project/
   ├── main.kyl              # Main application
   ├── config/
   │   └── app_config.kyl    # Configuration module
   ├── modules/
   │   ├── database.kyl      # Database utilities
   │   ├── http_client.kyl   # HTTP functionality
   │   └── string_utils.kyl  # String processing
   └── libs/
       ├── custom.so         # Custom shared libraries
       └── third_party.so
   ```

### Migration from External Config

**Old approach** (`libraries.conf`):
```
webview:/usr/lib/libwebview.so:true
sqlite:/usr/lib/libsqlite3.so:false
```

**New approach** (inline):
```kuyil
add_library("webview", "/usr/lib/libwebview.so", true);
add_library("sqlite", "/usr/lib/libsqlite3.so", false);
load_library("webview");
load_library("sqlite");
```

This makes Kuyil applications more flexible, self-contained, and maintainable while providing powerful code reuse capabilities through the module system.