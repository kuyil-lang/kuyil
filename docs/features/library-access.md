# Kuyil Language: Programmatic Library Access

## Overview

The Kuyil language startup function can now programmatically access the list of shared objects (.so files) that are loaded by the modular library system. This provides runtime introspection capabilities for applications to understand their available functionality.

## Available Functions

### Library Count Functions
- `get_library_count()` - Returns the total number of configured libraries
- `get_total_function_count()` - Returns the total number of available functions across all libraries

### Library Information Functions
- `get_library_name(index)` - Get the name of library at given index
- `get_library_path(index)` - Get the file path of library at given index  
- `is_library_loaded(index)` - Check if library at given index loaded successfully
- `get_library_function_count(index)` - Get number of functions in library at given index

### Summary Functions  
- `get_loaded_libraries_info()` - Get formatted string with all library information

## Example Usage

```kuyil
// Startup function that shows all loaded libraries
function startup_show_libraries() {
    var lib_count = get_library_count();
    print("Total libraries: " + lib_count);
    
    for (var i = 0; i < lib_count; i = i + 1) {
        var name = get_library_name(i);
        var loaded = is_library_loaded(i);
        var func_count = get_library_function_count(i);
        
        if (loaded) {
            print("✓ " + name + " (" + func_count + " functions)");
        } else {
            print("✗ " + name + " (failed to load)");
        }
    }
    
    return get_total_function_count();
}

var total_functions = startup_show_libraries();
print("Application ready with " + total_functions + " functions available");
```

## Configuration File Format

Libraries are configured in `libraries.conf`:

```
# Format: library_name:path:optional[true/false]
webview:./shared_libs/webview/libwebview_utils.so:true
crypto:./shared_libs/crypto/libcrypto_utils.so:true
sqlite:./shared_libs/sqlite/libsqlite_utils.so:true
```

## Runtime Library Status

The test system shows:
- 8 libraries configured (webview, crypto, compression, sqlite, http, fileio, math, system)
- 1 library successfully loaded (webview with 5 functions)
- 7 libraries failed to load (shared object files not found)

## Implementation Architecture

1. **Library Loader** (`library_loader.c`) - Manages shared library loading
2. **VM Integration** (`vm_library_integration.c`) - Bridges libraries to VM
3. **Configuration System** - Text file based library configuration
4. **Function Registry** - Dynamic function registration and dispatch
5. **Introspection API** - Runtime access to library information

## Use Cases

- **Application Diagnostics** - Check which modules are available at startup
- **Feature Detection** - Conditionally enable features based on loaded libraries  
- **Error Handling** - Graceful degradation when optional libraries fail
- **Debug Information** - Runtime library status for troubleshooting
- **Plugin Management** - Dynamic module discovery and listing

## Benefits

1. **Modularity** - Clean separation between VM core and extensions
2. **Flexibility** - Applications can adapt to available functionality
3. **Maintainability** - Easy to add/remove library modules
4. **Transparency** - Full visibility into loaded components
5. **Robustness** - Graceful handling of missing optional libraries

This system provides the foundation for building extensible Kuyil applications that can dynamically discover and utilize available shared library functionality.