# Kuyil Modular Library System

## Overview

The Kuyil VM now features a **modular library system** that decouples shared library integration from the core VM. Instead of hardcoded library registration, the system uses configuration-driven dynamic loading.

## Architecture

### Core Components

1. **Library Loader** (`library_loader.h/c`)
   - Manages dynamic library loading and unloading
   - Handles library-specific function registration
   - Supports multiple library categories (WebView, Crypto, SQLite, etc.)

2. **VM Library Integration** (`vm_library_integration.h/c`) 
   - Bridges between library loader and VM core
   - Provides dynamic function call dispatch
   - Manages function signature handling

3. **Configuration File** (`libraries.conf`)
   - Lists shared libraries to load at startup
   - Defines library paths and optional loading flags

### Key Features

- **Configuration-Driven**: Libraries defined in `libraries.conf`, no hardcoded paths
- **Optional Loading**: Libraries can be marked as optional (won't fail if missing)
- **Dynamic Function Dispatch**: Automatic function signature handling and calling
- **Extensible**: Easy to add new library categories and function types
- **Clean Separation**: VM core completely decoupled from specific library implementations

## Configuration Format

### libraries.conf Syntax
```
library_name:path:optional[true/false]
```

### Example Configuration
```
webview:./shared_libs/webview/libwebview_utils.so:true
crypto:./shared_libs/crypto/libcrypto_utils.so:true
compression:./shared_libs/transcoder/libcompression_utils.so:true
sqlite:./shared_libs/sqlite/libsqlite_utils.so:true
http:./shared_libs/http/libhttp_utils.so:true
fileio:./shared_libs/fileio/libfileio_utils.so:true
math:./shared_libs/math/libmath_utils.so:true
system:./shared_libs/system/libsystem_utils.so:true
```

## Implementation Details

### Function Signature Support

The system supports multiple function signatures for different use cases:

- `FUNC_SIG_VOID_VOID`: Functions that take no parameters and return nothing
- `FUNC_SIG_INT_VOID`: Functions that take no parameters and return an integer
- `FUNC_SIG_VALUE_ARGS`: Functions that use Kuyil's native `Value` system

### Library Categories

Each library category has its own function loader:

1. **WebView Functions**
   - `webview_init`, `webview_create_window`, `webview_load_html`, etc.

2. **Crypto Functions** 
   - Cryptographic operations and hashing functions

3. **Compression Functions**
   - File compression and decompression utilities

4. **SQLite Functions**
   - Database operations and SQL query handling

## Migration from Hardcoded System

### Before (Hardcoded)
```c
// Hardcoded WebView function registration in vm_init()
Value webview_init_val = {VALUE_STRING, {.string = strdup("webview_init")}};
define_global(vm, "webview_init", webview_init_val);
```

### After (Modular)
```c
// Dynamic library loading based on configuration
vm_init_library_system(vm);
```

The system automatically:
1. Reads `libraries.conf`
2. Loads available shared libraries
3. Registers functions dynamically
4. Creates proper VM bindings

## Adding New Libraries

### Step 1: Create Library-Specific Loader
Add a new function to `library_loader.c`:

```c
void load_mylib_functions(SharedLibrary* lib) {
    const char* mylib_functions[][3] = {
        {"mylib_function1", "my_func1", "FUNC_SIG_VOID_VOID"},
        {"mylib_function2", "my_func2", "FUNC_SIG_INT_VOID"},
        {NULL, NULL, NULL} // Terminator
    };
    
    load_generic_functions(lib, mylib_functions, "MyLib");
}
```

### Step 2: Add to Library Loading Logic
In `library_loader.c` `load_library()` function:

```c
} else if (strcmp(library_name, "mylib") == 0) {
    load_mylib_functions(lib);
}
```

### Step 3: Add to Configuration
Add your library to `libraries.conf`:
```
mylib:/path/to/libmylib.so:true
```

## Benefits

### For Developers
- **Easier Maintenance**: No need to modify VM core for new libraries
- **Better Testing**: Libraries can be developed and tested independently
- **Flexible Deployment**: Different environments can load different library sets

### For Users
- **Smaller Footprint**: Only load libraries you actually need
- **Better Error Handling**: Optional libraries won't break the system
- **Runtime Configuration**: Change library paths without recompilation

## Runtime Behavior

### Startup Sequence
1. VM initializes core systems
2. `vm_init_library_system()` called
3. Library loader reads `libraries.conf`
4. Each library loaded and functions registered
5. Dynamic function dispatch system activated

### Function Calls
When a Kuyil script calls a library function:
1. VM checks if function is dynamically registered
2. `call_dynamic_function()` looks up the function
3. Appropriate wrapper called based on signature
4. Result returned to Kuyil script

### Example Runtime Log
```
[INFO] Initializing VM library system
[INFO] Library loader initialized  
[INFO] Loaded library config from: ./libraries.conf
[INFO] Successfully loaded library: webview
[INFO] Loaded WebView function: webview_init
[INFO] Optional library not available: crypto (file not found)
[DEBUG] Registered dynamic function: webview_init
[INFO] VM library system initialized successfully
```

## Debugging and Monitoring

### Log Levels
- **INFO**: Library loading success/failure
- **DEBUG**: Individual function registration
- **WARNING**: Missing functions or signature issues
- **ERROR**: Critical loading failures

### Available Debug Functions
- `list_loaded_libraries()`: Show all loaded libraries
- `get_library_stats()`: Get loading statistics
- `is_dynamic_function(name)`: Check if function is available

## Future Enhancements

### Planned Features
1. **Hot Library Reloading**: Reload libraries without VM restart
2. **Dependency Management**: Library dependency resolution
3. **Version Checking**: Ensure library compatibility
4. **Plugin Marketplace**: Standard format for library distribution

### Extension Points
The architecture is designed to be extensible:
- Add new function signatures as needed
- Support for more complex parameter passing
- Integration with package managers
- Automatic library discovery

## Conclusion

The modular library system transforms Kuyil from a monolithic VM into a flexible, extensible platform. Libraries can now be developed independently, deployed selectively, and configured at runtime.

**Key Achievement**: Successfully decoupled WebView integration from VM core, creating a clean plugin architecture that serves as the foundation for all future library integrations.