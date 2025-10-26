# Kuyil Foreign Function Interface (FFI) System

## Overview

The Kuyil FFI system provides JNI-like functionality for loading and calling functions from external shared libraries (.so files). This enables seamless integration with existing C libraries and custom extensions.

## Key Features

- **Dynamic Library Loading**: Load .so files at runtime using `dlopen`
- **Type-Safe Interface**: Automatic type conversion between Kuyil and C types
- **Function Registration**: Register external functions with type signatures
- **Memory Management**: Automatic cleanup and garbage collection
- **Error Handling**: Comprehensive error reporting and validation
- **Standard Library Integration**: Pre-built integrations for Redis, Elasticsearch, and KMS

## Architecture

### Core Components

1. **FFI Context**: Manages loaded libraries and function registry
2. **Type System**: Handles conversion between Kuyil and C types
3. **Library Manager**: Loads, unloads, and tracks shared libraries
4. **Function Registry**: Maps function names to callable implementations
5. **Error System**: Provides detailed error reporting

### Supported Types

| FFI Type | C Type | Kuyil Type | Description |
|----------|---------|-------------|-------------|
| `FFI_TYPE_VOID` | `void` | `nil` | No return value |
| `FFI_TYPE_BOOL` | `bool` | `boolean` | Boolean values |
| `FFI_TYPE_INT32` | `int32_t` | `number` | 32-bit integers |
| `FFI_TYPE_INT64` | `int64_t` | `number` | 64-bit integers |
| `FFI_TYPE_DOUBLE` | `double` | `number` | Double-precision floats |
| `FFI_TYPE_STRING` | `char*` | `string` | Null-terminated strings |
| `FFI_TYPE_POINTER` | `void*` | `string` | Generic pointers (as hex strings) |

## API Reference

### Core FFI Functions

#### `load_library(name, path)`
Loads a shared library and makes it available for function calls.

```kuyil
// Load custom library
load_library("mylib", "./libs/mylib.so")

// Load with full path
load_library("redis_client", "/usr/local/lib/redis_client.so")
```

**Parameters:**
- `name`: Unique library identifier
- `path`: Path to the .so file

**Returns:** Library name on success, `nil` on failure

#### `register_function(library, function_name)`
Registers a function from a loaded library for calling.

```kuyil
// Register function after loading library
load_library("math_lib", "./math.so")
register_function("math_lib", "advanced_sqrt")
register_function("math_lib", "fibonacci")
```

**Parameters:**
- `library`: Name of the loaded library
- `function_name`: Name of the function to register

**Returns:** `true` on success, `false` on failure

#### `call_function(library, function_name, ...args)`
Calls a registered function with the provided arguments.

```kuyil
// Call function with arguments
result = call_function("math_lib", "advanced_sqrt", 256.0)
fib_result = call_function("math_lib", "fibonacci", 10)
```

**Parameters:**
- `library`: Name of the library
- `function_name`: Name of the function
- `...args`: Variable arguments to pass to the function

**Returns:** Function return value converted to Kuyil type

#### `unload_library(name)`
Unloads a library and frees its resources.

```kuyil
// Cleanup when done
unload_library("math_lib")
```

**Parameters:**
- `name`: Name of the library to unload

**Returns:** `true` on success, `false` on failure

### Convenience Functions

#### `load_redis_client(path)`
Loads Redis client library with pre-registered functions.

```kuyil
load_redis_client("./ffi_libs/redis_client.so")
// Pre-registered functions: redis_connect, redis_set, redis_get, redis_close
```

#### `load_elasticsearch_client(path)`
Loads Elasticsearch client library with pre-registered functions.

```kuyil
load_elasticsearch_client("./ffi_libs/elasticsearch_client.so")
// Pre-registered functions: es_connect, es_index, es_search, es_close
```

#### `load_kms_client(path)`
Loads KMS client library with pre-registered functions.

```kuyil
load_kms_client("./ffi_libs/kms_client.so")
// Pre-registered functions: kms_init, kms_encrypt, kms_decrypt, kms_cleanup
```

## Creating Custom Libraries

### Basic Library Structure

```c
// example_lib.c
#include <stdio.h>
#include <stdlib.h>

// Simple function that adds two numbers
double add_numbers(double a, double b) {
    return a + b;
}

// Function that formats a greeting
char* format_greeting(const char* name) {
    char* result = malloc(256);
    snprintf(result, 256, "Hello, %s! Welcome to Kuyil FFI.", name);
    return result;
}

// Function that checks if a number is prime
int is_prime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) return 0;
    }
    return 1;
}
```

### Building the Library

```bash
# Compile as shared library
gcc -shared -fPIC -o example_lib.so example_lib.c

# With optimization and debug info
gcc -shared -fPIC -O2 -g -o example_lib.so example_lib.c
```

### Using the Custom Library

```kuyil
// Load the custom library
load_library("example", "./example_lib.so")

// Register functions
register_function("example", "add_numbers")
register_function("example", "format_greeting")  
register_function("example", "is_prime")

// Call functions
sum = call_function("example", "add_numbers", 15.5, 20.3)
greeting = call_function("example", "format_greeting", "Alice")
prime = call_function("example", "is_prime", 17)

print("Sum: " + sum)
print("Greeting: " + greeting)
print("Is prime: " + prime)

// Cleanup
unload_library("example")
```

## Example Integrations

### Redis Integration

```kuyil
// Load Redis client
load_redis_client("./ffi_libs/redis_client.so")

// Connect to Redis
conn = call_function("redis_client", "redis_connect", "localhost", 6379)

// Set values
call_function("redis_client", "redis_set", "user:123", "Alice")
call_function("redis_client", "redis_set", "session:abc", "active")

// Get values
user = call_function("redis_client", "redis_get", "user:123")
session = call_function("redis_client", "redis_get", "session:abc")

print("User: " + user)
print("Session: " + session)

// Cleanup
call_function("redis_client", "redis_close")
```

### Elasticsearch Integration

```kuyil
// Load Elasticsearch client
load_elasticsearch_client("./ffi_libs/elasticsearch_client.so")

// Connect to Elasticsearch
client = call_function("elasticsearch_client", "es_connect", "http://localhost:9200", 30)

// Index documents
doc1 = '{"title": "Kuyil Tutorial", "content": "Learn Kuyil FFI system"}'
call_function("elasticsearch_client", "es_index", "docs", "_doc", "1", doc1)

doc2 = '{"title": "FFI Guide", "content": "Advanced FFI integration patterns"}'  
call_function("elasticsearch_client", "es_index", "docs", "_doc", "2", doc2)

// Search documents
query = '{"query": {"match": {"title": "Kuyil"}}}'
results = call_function("elasticsearch_client", "es_search", "docs", query)

print("Search results: " + results)

// Cleanup
call_function("elasticsearch_client", "es_close")
```

### KMS Integration

```kuyil
// Load KMS client
load_kms_client("./ffi_libs/kms_client.so")

// Initialize KMS client
client = call_function("kms_client", "kms_init", "us-east-1", "ACCESS_KEY", "SECRET_KEY")

// Encrypt sensitive data
key_id = "arn:aws:kms:us-east-1:123456789012:key/example-key-id"
plaintext = "This is sensitive information"
encrypted = call_function("kms_client", "kms_encrypt", key_id, plaintext)

print("Encrypted: " + encrypted)

// Decrypt data
decrypted = call_function("kms_client", "kms_decrypt", encrypted)
print("Decrypted: " + decrypted)

// Cleanup
call_function("kms_client", "kms_cleanup")
```

## Advanced Usage

### Multi-Library Integration

```kuyil
// Use multiple libraries together
load_redis_client("./ffi_libs/redis_client.so")
load_kms_client("./ffi_libs/kms_client.so")

// Initialize both clients
redis_conn = call_function("redis_client", "redis_connect", "localhost", 6379)
kms_client = call_function("kms_client", "kms_init", "us-east-1", "key", "secret")

// Encrypt data with KMS, store in Redis
sensitive_data = "password123"
encrypted_data = call_function("kms_client", "kms_encrypt", "key-id", sensitive_data)
call_function("redis_client", "redis_set", "encrypted:password", encrypted_data)

// Retrieve and decrypt
stored_data = call_function("redis_client", "redis_get", "encrypted:password")
decrypted_data = call_function("kms_client", "kms_decrypt", stored_data)

print("Original: " + sensitive_data)
print("Decrypted: " + decrypted_data)

// Cleanup
call_function("redis_client", "redis_close")
call_function("kms_client", "kms_cleanup")
```

### Error Handling

```kuyil
// Robust error handling
library_loaded = load_library("test_lib", "./missing_lib.so")
if (!library_loaded) {
    log_error("Failed to load library")
    // Handle error gracefully
} else {
    function_registered = register_function("test_lib", "test_function")
    if (function_registered) {
        result = call_function("test_lib", "test_function", "arg1", "arg2")
        if (result) {
            print("Function call successful: " + result)
        } else {
            log_warning("Function call returned null")
        }
    } else {
        log_error("Failed to register function")
    }
    unload_library("test_lib")
}
```

## Build Integration

### Makefile Targets

```bash
# Build all FFI libraries
make ffi-libs

# Build individual libraries
make redis-lib
make elasticsearch-lib  
make kms-lib

# Run FFI demos
make ffi-demo
make test-ffi

# Test individual libraries
make test-redis-ffi
make test-elasticsearch-ffi
make test-kms-ffi
```

### Custom Library Integration

```makefile
# Add to your Makefile
my_lib.so: my_lib.c
	gcc -shared -fPIC -O2 -o $@ $<

# Include in build process
all: kuyil my_lib.so
```

## Best Practices

### 1. Memory Management
- Always free allocated memory in C functions
- Use proper cleanup functions to avoid leaks
- Handle null pointers gracefully

### 2. Error Handling
- Return meaningful error codes from C functions
- Check function return values in Kuyil
- Use logging for debugging FFI calls

### 3. Type Safety
- Validate parameter types before conversion
- Handle type mismatches gracefully  
- Document expected parameter types

### 4. Performance
- Minimize type conversions in tight loops
- Cache frequently used function pointers
- Unload unused libraries to free memory

### 5. Security
- Validate all input parameters
- Sanitize string inputs to prevent buffer overflows
- Use secure functions like `snprintf` instead of `sprintf`

## Troubleshooting

### Common Issues

1. **Library Not Found**
   - Check file path and permissions
   - Ensure library is compiled for correct architecture
   - Verify all dependencies are available

2. **Function Not Found**
   - Check function name spelling
   - Ensure function is exported (not static)
   - Use `nm -D library.so` to list exported symbols

3. **Type Conversion Errors**
   - Verify parameter types match expectations
   - Check for null pointer handling
   - Validate string parameters

4. **Memory Issues**
   - Use valgrind to detect memory leaks
   - Ensure proper cleanup in library functions
   - Check for buffer overruns

### Debugging

```bash
# Check library symbols
nm -D my_library.so

# Check library dependencies  
ldd my_library.so

# Run with debugging
LD_DEBUG=libs ./kuyil my_script.kyl

# Memory debugging
valgrind --leak-check=full ./kuyil my_script.kyl
```

## Limitations

- Maximum 3 parameters supported in current implementation
- No support for complex nested structures yet
- Limited array type support
- Function pointers not yet implemented

## Future Enhancements

- [ ] Support for more parameter types and counts
- [ ] Complex struct and array support
- [ ] Function pointer callbacks
- [ ] Async function calls
- [ ] Performance optimizations with libffi

The Kuyil FFI system provides powerful capabilities for integrating with existing C libraries while maintaining type safety and ease of use. It enables Kuyil applications to leverage the vast ecosystem of C libraries for databases, cryptography, networking, and more.