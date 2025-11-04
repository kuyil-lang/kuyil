# Kuyil FFI Interface System

## Overview

Kuyil provides typed interface files for all shared libraries, enabling clean, type-safe FFI access without embedding library-specific code in the VM.

## Available Interface Files

All interface files are located in the `interfaces/` directory:

- **interface_str.kyl** - String manipulation (length, substring, upper, lower, trim, etc.)
- **interface_math.kyl** - Mathematical operations (abs, floor, ceil, round, sqrt, pow, trig)
- **interface_http.kyl** - HTTP client operations (client_get, client_post)
- **interface_datetime.kyl** - Date and time operations (now, format, add, sub, diff)
- **interface_crypto.kyl** - Cryptographic functions (hashing, encryption, random)
- **interface_sqlite.kyl** - SQLite database operations
- **interface_fileio.kyl** - File I/O operations (read, write, validate)
- **interface_rpc.kyl** - RPC client/server with Thrift and Protobuf support
- **interface_webview.kyl** - Desktop webview/GUI operations
- **interface_ffmpeg.kyl** - Audio/video processing with FFmpeg
- **interface_transcoder.kyl** - Compression/decompression (gzip, zip)

## How It Works

Each interface file contains:

1. **@loadlib directive** - Loads the shared library
2. **interface declarations** - Typed method signatures with parameters and return types

### Example: String Interface

```kuyil
// interfaces/interface_str.kyl
@loadlib("./libs/libkylstr.so");

interface str {
  number length(input: string)
  string substring(input: string, start: number, end: number)
  string upper(input: string)
  string lower(input: string)
  // ... more methods
}
```

## Library Loading Search Order

When `@loadlib("./libs/libkylstr.so")` is called, Kuyil searches in this order:

1. **Executable directory first**: `<kuyil_binary_dir>/libs/libkylstr.so`
2. **Current working directory**: `./libs/libkylstr.so`

This ensures libraries bundled with the Kuyil installation are found first, while allowing project-local overrides.

## Using Interface Files

### Method 1: Import Interface File (Recommended)

```kuyil
import("../interfaces/interface_str.kyl")

// Now you can call methods directly
print(length("hello"))        // 5
print(substring("kuyil", 1, 3)) // "uy"
print(upper("hello"))          // "HELLO"
```

### Method 1b: Namespaced Import (Avoid Conflicts)

Use the `as` keyword to import with a namespace, preventing naming conflicts:

```kuyil
import("../interfaces/interface_str.kyl") as strlib
import("../interfaces/interface_math.kyl") as mathlib

var text = "Kuyil"
print(strlib.length(text))    // 5 - namespaced access
print(strlib.upper(text))     // KUYIL

var num = -42.7
print(mathlib.abs(num))       // 42.7
print(mathlib.floor(num))     // -43
```

**Benefits of Namespaced Import:**
- Prevents naming conflicts when multiple interfaces have methods with the same name
- Makes code more explicit about which interface is being used
- Allows importing multiple interfaces with overlapping method names

**How It Works:**
- `import(path) as ns` creates a namespace object with hidden metadata
- Method calls like `ns.method(args)` are dispatched to the underlying library function
- Type checking and arity validation still occur as normal
- The namespace object is not passed as an argument (automatic receiver-dropping)

### Method 2: Direct @loadlib + Interface

```kuyil
@loadlib("./libs/libkylstr.so");

interface str {
  number length(input: string)
  string substring(input: string, start: number, end: number)
}

print(length("test"))  // 4
```

### Method 3: Use Prefixed Names

All interface methods create both unprefixed and prefixed aliases:

```kuyil
import("../interfaces/interface_str.kyl")

// These are equivalent:
print(length("test"))      // Unprefixed alias
print(str_length("test"))  // Prefixed (original) name
print(str.length("test"))  // Dotted notation
```

## Type Checking

Interface declarations enable runtime type checking:

```kuyil
interface str {
  number length(input: string)  // Parameter must be string, returns number
}

length("hello")  // ✓ OK - argument is string
length(42)       // ✗ Type error - expected string, got number
```

### Supported Types

- `string` - Text values
- `number` - All numeric values (int, float, double)
- `bool` - Boolean values
- `array` - Array/list values
- `object` - Map/object values
- `nil` - Null/undefined values

**Note**: Union types like `number|int32` are planned but not yet supported by the lexer. Use the broader type (`number`) for now.

## Architecture: Generic VM, Modular Libraries

### Key Principles

1. **VM is library-agnostic** - The VM contains only generic FFI dispatch code
2. **No hardcoded library logic** - All library-specific code lives in the loader and shared libraries
3. **Dynamic registration** - Libraries register their functions via the loader at startup
4. **Interface-driven binding** - Type checking and aliasing happen through interface declarations

### Component Responsibilities

#### VM (`src/vm_library_integration.c`)
- Generic dynamic function dispatch
- Interface method binding (creates aliases)
- Runtime type checking for interface methods
- **Does NOT contain**: Library-specific loading, injection, or function lists

#### Library Loader (`src/library_loader.c`)
- Loads shared libraries via dlopen
- Enumerates and registers library functions
- Handles library-specific initialization (e.g., HTTP callback injection)
- Manages path resolution (exe_dir → CWD)

#### Shared Libraries (`libs/libkyl*.so`)
- Implement library-specific functionality
- Export functions with signatures matching interface declarations
- Optional: Accept VM callback for calling Kuyil code from C (e.g., HTTP handlers)

## Examples

### Multi-Library Demo

```kuyil
// Load multiple libraries
import("../interfaces/interface_str.kyl")
import("../interfaces/interface_math.kyl")
import("../interfaces/interface_datetime.kyl")

// String operations
let name = "Kuyil"
print("Hello,", upper(name))
print("Length:", length(name))

// Math operations
let value = -42.7
print("Absolute:", abs(value))
print("Rounded:", round(value))
print("Square root of 144:", sqrt(144))

// DateTime operations
let now = date_now()
print("Current time:", date_iso(now))
```

### HTTP Client

```kuyil
import("../interfaces/interface_http.kyl")

let response = client_get("https://api.github.com/zen")
print("GitHub Zen:", response)
```

### File Operations

```kuyil
import("../interfaces/interface_fileio.kyl")

write_text("hello.txt", "Hello, World!")
let content = read_text("hello.txt")
print("File content:", content)
print("File size:", size("hello.txt"))
```

### Database Operations

```kuyil
import("../interfaces/interface_sqlite.kyl")

let db = open_database("test.db")
execute_sql(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER, name TEXT)")
execute_sql(db, "INSERT INTO users VALUES (1, 'Alice')")

let result = execute_query(db, "SELECT * FROM users")
let row = result_first_row(result)
print("User ID:", row_get_int(row, 0))
print("User name:", row_get_text(row, 1))
```

## Creating Custom Interfaces

You can create your own interface files for custom shared libraries:

```kuyil
// my_interface.kyl
@loadlib("./libs/libmycustom.so");

interface custom {
  number compute(x: number, y: number)
  string format(data: string)
}
```

Then use it:

```kuyil
import("./my_interface.kyl")

let result = compute(10, 20)
print("Result:", result)
```

## Benefits

1. **Type Safety** - Catch type errors at runtime before calling C functions
2. **Clean APIs** - Use simple method names instead of prefixed C function names
3. **Self-Documenting** - Interface files serve as API documentation
4. **No VM Changes** - Adding new libraries doesn't require VM recompilation
5. **Flexible Loading** - Load only the libraries you need per script
6. **Portable** - Libraries bundled with Kuyil work anywhere

## Configuration

Libraries are also configured via `libraries.conf`:

```
# Format: name:path:optional
str:./libs/libkylstr.so:false
math:./libs/libkylmath.so:false
http:./libs/libkylhttp.so:false
datetime:./libs/libkyldatetime.so:false
crypto:./libs/libkylcrypto.so:true
sqlite:./libs/libkylsqlite.so:true
```

Config file search order:
1. `$KUYIL_HOME/libraries.conf`
2. `<executable_dir>/libraries.conf`
3. `./libraries.conf` (CWD)

## Troubleshooting

### Library Not Found

```
[ERROR] Failed to load required library str: libkylstr.so: cannot open shared object file
```

**Solution**: Ensure the library exists in:
- `<kuyil_binary_dir>/libs/` or
- `./libs/` (current directory)

### Type Mismatch

```
[ERROR] Type mismatch calling length: param 1 does not match types 'string'
```

**Solution**: Check the interface declaration and pass the correct type:
```kuyil
length("hello")  // ✓ Correct - string argument
length(42)       // ✗ Wrong - number argument
```

### Function Not Found

```
[WARNING] Dynamic function not found: length
```

**Solution**: Ensure you've loaded the interface:
```kuyil
import("../interfaces/interface_str.kyl")  // Load before use
print(length("test"))
```

## Performance Notes

- Interface binding happens once at parse time
- Type checking happens at runtime per call (minimal overhead)
- Direct C function dispatch after type validation
- No reflection or dynamic lookup during execution

## Future Enhancements

- [ ] Union type support (`number|int32`, `string|nil`)
- [ ] Optional parameter syntax (`name?: string`)
- [ ] Generic/template interfaces (`Array<T>`)
- [ ] Interface inheritance and composition
- [ ] Compile-time type checking (static analysis)

## See Also

- [FFI System Guide](FFI_SYSTEM_GUIDE.md)
- [Shared Library Development](SHARED_LIBRARY_GUIDE.md)
- [Language Reference - FFI](language-reference.md#ffi-and-interfaces)
