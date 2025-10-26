# Kuyil Shared Library Creation Guide

## Overview

Kuyil now supports creating shared libraries (.so files) from Kuyil source code. This feature allows you to compile Kuyil functions into native shared libraries that can be loaded and called efficiently using the enhanced FFI system.

## Core Functions

### 1. create_shared_library(name, source_file, output_path, [flags])

Creates a shared library from Kuyil source code.

**Parameters:**
- `name` (string): Name of the library (used for loading/unloading)
- `source_file` (string): Path to the Kuyil source file
- `output_path` (string): Where to save the compiled .so file
- `flags` (number, optional): Compilation flags

**Library Compilation Flags:**
- `KUYIL_LIB_DEFAULT = 0`: Standard compilation
- `KUYIL_LIB_OPTIMIZE = 1`: Enable optimizations (-O2)
- `KUYIL_LIB_DEBUG = 2`: Include debug information (-g)
- `KUYIL_LIB_EXPORT_ALL = 4`: Export all functions (default exports only marked functions)

**Returns:** `true` if successful, `false` otherwise

**Example:**
```kuyil
// Create optimized library with debug info
var flags = 1 + 2;  // OPTIMIZE + DEBUG
var success = create_shared_library(
    "my_math_lib", 
    "src/math.kuyil", 
    "lib/libmath.so", 
    flags
);
```

### 2. load_library_ex(lib_path, flags)

Load a library with enhanced options.

**Parameters:**
- `lib_path` (string): Path to the .so file
- `flags` (number): Loading behavior flags

**Load Flags:**
- `FFI_LOAD_LAZY = 1`: Lazy symbol binding (resolve symbols on first use)
- `FFI_LOAD_NOW = 2`: Immediate symbol binding (resolve all symbols at load time)
- `FFI_LOAD_GLOBAL = 4`: Make symbols globally available
- `FFI_LOAD_LOCAL = 8`: Keep symbols local to this library

**Returns:** `true` if loaded successfully, `false` otherwise

**Example:**
```kuyil
// Load with immediate binding and global symbols
var flags = 2 + 4;  // NOW + GLOBAL
var loaded = load_library_ex("lib/libmath.so", flags);
```

### 3. unload_library_ex(lib_name, flags)

Unload a library with enhanced options.

**Parameters:**
- `lib_name` (string): Name of the library to unload
- `flags` (number): Unloading behavior flags

**Unload Flags:**
- `FFI_UNLOAD_NORMAL = 0`: Standard unload
- `FFI_UNLOAD_FORCE = 1`: Force unload even if references exist
- `FFI_UNLOAD_CLEANUP = 2`: Run cleanup functions before unloading

**Returns:** `true` if unloaded successfully, `false` otherwise

**Example:**
```kuyil
// Unload with cleanup
var success = unload_library_ex("my_math_lib", 2);
```

### 4. list_loaded_libraries()

Lists all currently loaded libraries to the console.

**Example:**
```kuyil
list_loaded_libraries();
// Output: Lists all loaded library names and their status
```

### 5. is_library_loaded(lib_name)

Check if a specific library is currently loaded.

**Parameters:**
- `lib_name` (string): Name of the library to check

**Returns:** `true` if loaded, `false` otherwise

**Example:**
```kuyil
if (is_library_loaded("my_math_lib")) {
    print("Math library is ready to use");
}
```

## Workflow Example

Here's a complete example of creating and using a shared library:

### Step 1: Create Source File (math_functions.kuyil)

```kuyil
// Functions to be compiled into shared library
fun add(a, b) {
    return a + b;
}

fun multiply(a, b) {
    return a * b;
}

fun factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
```

### Step 2: Compile to Shared Library

```kuyil
// Create the shared library
var success = create_shared_library(
    "math_ops",                    // Library name
    "src/math_functions.kuyil",    // Source file
    "lib/libmath_ops.so",          // Output .so file
    1                              // Enable optimizations
);

if (!success) {
    print("Failed to create library");
    return;
}
```

### Step 3: Load and Use Library

```kuyil
// Load the library
var load_flags = 2 + 4;  // Immediate binding + global symbols
var loaded = load_library_ex("lib/libmath_ops.so", load_flags);

if (loaded) {
    // Define function signatures
    define_function("math_ops", "add", "int", "int,int");
    define_function("math_ops", "multiply", "int", "int,int");
    define_function("math_ops", "factorial", "int", "int");
    
    // Use the functions
    var sum = call_function("math_ops", "add", 10, 20);
    var product = call_function("math_ops", "multiply", 5, 6);
    var fact = call_function("math_ops", "factorial", 5);
    
    print("Sum: " + str(sum));          // Sum: 30
    print("Product: " + str(product));  // Product: 30
    print("Factorial: " + str(fact));   // Factorial: 120
}
```

### Step 4: Cleanup

```kuyil
// Unload when done
var cleanup_flags = 2;  // Run cleanup functions
unload_library_ex("math_ops", cleanup_flags);
```

## Advanced Features

### Library Management

```kuyil
// Check library status
if (is_library_loaded("math_ops")) {
    print("Library is active");
} else {
    print("Library not loaded");
}

// List all loaded libraries
print("Currently loaded libraries:");
list_loaded_libraries();
```

### Error Handling

```kuyil
// Always check return values
var created = create_shared_library("my_lib", "src.kuyil", "lib.so");
if (!created) {
    print("Library creation failed - check source file and permissions");
    return;
}

var loaded = load_library_ex("lib.so", 2);
if (!loaded) {
    print("Library loading failed - check file path and dependencies");
    return;
}
```

### Performance Optimization

```kuyil
// Use immediate binding for performance-critical libraries
var perf_flags = 2;  // FFI_LOAD_NOW
load_library_ex("high_perf_lib.so", perf_flags);

// Use optimized compilation for math-heavy libraries
var opt_flags = 1;  // KUYIL_LIB_OPTIMIZE
create_shared_library("fast_math", "math.kuyil", "fast_math.so", opt_flags);
```

## Best Practices

1. **Use meaningful library names** - They're used for loading/unloading
2. **Check return values** - Always verify operations succeeded
3. **Manage library lifecycle** - Load when needed, unload when done
4. **Use appropriate flags** - Choose flags based on your use case
5. **Handle errors gracefully** - Provide fallbacks for failed operations
6. **Document exported functions** - Make it clear what functions are available
7. **Test thoroughly** - Verify library creation and function calls work correctly

## File Structure

```
project/
├── src/
│   ├── math_functions.kuyil     # Source for compilation
│   └── utils.kuyil              # Another library source
├── lib/
│   ├── libmath_ops.so           # Compiled shared library
│   └── libutils.so              # Another compiled library
├── tests/
│   └── test_libraries.kuyil     # Library tests
└── main.kuyil                   # Main program using libraries
```

This system provides a complete solution for creating, managing, and using shared libraries in Kuyil, enabling code reuse, performance optimization, and modular programming.