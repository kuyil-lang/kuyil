# FFI Guide - Foreign Function Interface

Create and use native C libraries (.so files) with Kuyil.

## Table of Contents
- [What is FFI?](#what-is-ffi)
- [Creating a C Library](#creating-a-c-library)
- [Building Shared Libraries](#building-shared-libraries)
- [Loading Libraries in Kuyil](#loading-libraries-in-kuyil)
- [Calling C Functions](#calling-c-functions)
- [Type Mapping](#type-mapping)
- [Complete Example](#complete-example)

## What is FFI?

FFI (Foreign Function Interface) allows Kuyil to call functions written in C/C++. This enables:

- **Performance**: Use C for CPU-intensive operations
- **Native APIs**: Access system libraries and OS APIs
- **Existing Code**: Reuse existing C/C++ libraries
- **Extensions**: Extend Kuyil with custom functionality

## Creating a C Library

### Basic Structure

Create a C file with your functions:

```c
// my_library.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple function: add two numbers
double add_numbers(double a, double b) {
    return a + b;
}

// String function: convert to uppercase
char* to_uppercase(const char* str) {
    if (str == NULL) return NULL;
    
    int len = strlen(str);
    char* result = (char*)malloc(len + 1);
    
    for (int i = 0; i < len; i++) {
        result[i] = toupper(str[i]);
    }
    result[len] = '\0';
    
    return result;
}

// Array function: sum array elements
double sum_array(double* arr, int length) {
    double sum = 0.0;
    for (int i = 0; i < length; i++) {
        sum += arr[i];
    }
    return sum;
}

// Complex function: multiple return values (via pointers)
void divide_with_remainder(int dividend, int divisor, int* quotient, int* remainder) {
    *quotient = dividend / divisor;
    *remainder = dividend % divisor;
}
```

### Header File (Optional but Recommended)

```c
// my_library.h
#ifndef MY_LIBRARY_H
#define MY_LIBRARY_H

#ifdef __cplusplus
extern "C" {
#endif

double add_numbers(double a, double b);
char* to_uppercase(const char* str);
double sum_array(double* arr, int length);
void divide_with_remainder(int dividend, int divisor, int* quotient, int* remainder);

#ifdef __cplusplus
}
#endif

#endif // MY_LIBRARY_H
```

## Building Shared Libraries

### On Linux

```bash
# Compile to shared library
gcc -shared -fPIC -o libmylibrary.so my_library.c

# With optimization
gcc -shared -fPIC -O2 -o libmylibrary.so my_library.c

# With multiple source files
gcc -shared -fPIC -o libmylibrary.so file1.c file2.c file3.c

# Link with other libraries
gcc -shared -fPIC -o libmylibrary.so my_library.c -lm -lpthread
```

### On macOS

```bash
# macOS uses .dylib extension
gcc -shared -fPIC -o libmylibrary.dylib my_library.c

# Or with macOS-specific flags
gcc -dynamiclib -o libmylibrary.dylib my_library.c
```

### On Windows

```bash
# Windows uses .dll extension
gcc -shared -o mylibrary.dll my_library.c

# With MinGW
x86_64-w64-mingw32-gcc -shared -o mylibrary.dll my_library.c
```

### Makefile Example

```makefile
# Makefile
CC = gcc
CFLAGS = -fPIC -O2 -Wall
LDFLAGS = -shared

TARGET = libmylibrary.so
SOURCES = my_library.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
```

Build with:
```bash
make
```

## Loading Libraries in Kuyil

### Using load_library

```kuyil
// Load the library
load_library("mylibrary", "./libmylibrary.so")

// Or with absolute path
load_library("mylibrary", "/path/to/libmylibrary.so")

// On macOS
load_library("mylibrary", "./libmylibrary.dylib")

// On Windows
load_library("mylibrary", "./mylibrary.dll")
```

### Registering Functions

```kuyil
// Register function: name, return type, parameter types
register_function("mylibrary", "add_numbers", "float64", ["float64", "float64"])

// Register string function
register_function("mylibrary", "to_uppercase", "string", ["string"])

// Register array function
register_function("mylibrary", "sum_array", "float64", ["array", "int32"])
```

## Calling C Functions

### Basic Calls

```kuyil
// Load and register
load_library("mylibrary", "./libmylibrary.so")
register_function("mylibrary", "add_numbers", "float64", ["float64", "float64"])

// Call the function
let result = call_function("mylibrary", "add_numbers", [10.5, 20.3])
print("Result:", result)  // 30.8
```

### String Functions

```kuyil
register_function("mylibrary", "to_uppercase", "string", ["string"])

let upper = call_function("mylibrary", "to_uppercase", ["hello world"])
print(upper)  // "HELLO WORLD"
```

### Array Functions

```kuyil
register_function("mylibrary", "sum_array", "float64", ["array", "int32"])

let numbers = [1.5, 2.5, 3.5, 4.5]
let sum = call_function("mylibrary", "sum_array", [numbers, 4])
print("Sum:", sum)  // 12.0
```

## Type Mapping

### C to Kuyil Type Mapping

| C Type | Kuyil Type | register_function Type |
|--------|------------|------------------------|
| `int`, `int32_t` | number | `"int32"` |
| `long`, `int64_t` | number | `"int64"` |
| `float` | number | `"float32"` |
| `double` | number | `"float64"` |
| `char*`, `const char*` | string | `"string"` |
| `void*` | special | `"pointer"` |
| `bool`, `_Bool` | boolean | `"bool"` |
| `void` | nil | `"void"` |
| arrays | array | `"array"` |

### Parameter Type Examples

```kuyil
// Int parameters
register_function("lib", "func", "int32", ["int32", "int32"])

// Mixed types
register_function("lib", "func", "float64", ["int32", "float64", "string"])

// No parameters
register_function("lib", "func", "int32", [])

// Void return
register_function("lib", "func", "void", ["int32"])
```

## Complete Example

### 1. C Library (math_ops.c)

```c
// math_ops.c
#include <math.h>
#include <stdlib.h>

// Calculate factorial
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// Calculate power
double power(double base, double exponent) {
    return pow(base, exponent);
}

// Check if prime
int is_prime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i <= sqrt(n); i++) {
        if (n % i == 0) return 0;
    }
    return 1;
}

// Greatest common divisor
int gcd(int a, int b) {
    while (b != 0) {
        int temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}
```

### 2. Build the Library

```bash
gcc -shared -fPIC -O2 -o libmathops.so math_ops.c -lm
```

### 3. Use in Kuyil (math_demo.kyl)

```kuyil
// Load the library
load_library("mathops", "./libmathops.so")

// Register functions
register_function("mathops", "factorial", "int32", ["int32"])
register_function("mathops", "power", "float64", ["float64", "float64"])
register_function("mathops", "is_prime", "int32", ["int32"])
register_function("mathops", "gcd", "int32", ["int32", "int32"])

// Use the functions
print("Factorial of 5:", call_function("mathops", "factorial", [5]))
print("2^10:", call_function("mathops", "power", [2.0, 10.0]))
print("Is 17 prime?", call_function("mathops", "is_prime", [17]))
print("GCD(48, 18):", call_function("mathops", "gcd", [48, 18]))

// Output:
// Factorial of 5: 120
// 2^10: 1024
// Is 17 prime? 1
// GCD(48, 18): 6
```

### 4. Create a Wrapper Module

```kuyil
// math_wrapper.kyl
// One-time setup
load_library("mathops", "./libmathops.so")
register_function("mathops", "factorial", "int32", ["int32"])
register_function("mathops", "power", "float64", ["float64", "float64"])
register_function("mathops", "is_prime", "int32", ["int32"])
register_function("mathops", "gcd", "int32", ["int32", "int32"])

// Wrapper functions
fn factorial(n) {
    return call_function("mathops", "factorial", [n])
}

fn power(base, exp) {
    return call_function("mathops", "power", [base, exp])
}

fn isPrime(n) {
    let result = call_function("mathops", "is_prime", [n])
    return result == 1
}

fn gcd(a, b) {
    return call_function("mathops", "gcd", [a, b])
}
```

```kuyil
// main.kyl
import "math_wrapper.kyl"

// Now use like regular functions
print("5! =", factorial(5))
print("2^10 =", power(2, 10))
print("Is 17 prime?", isPrime(17))
print("GCD(48, 18) =", gcd(48, 18))
```

## Best Practices

### 1. Memory Management

```c
// Good: Allocate memory for return strings
char* get_message() {
    char* msg = (char*)malloc(50);
    strcpy(msg, "Hello from C!");
    return msg;  // Caller must free
}

// Bad: Returning stack memory
char* get_message() {
    char msg[50] = "Hello";
    return msg;  // WRONG! Stack memory will be invalid
}
```

### 2. Error Handling

```c
// Return error codes or special values
int safe_divide(int a, int b, int* result) {
    if (b == 0) {
        return -1;  // Error code
    }
    *result = a / b;
    return 0;  // Success
}
```

```kuyil
let result_ptr = 0
let status = call_function("lib", "safe_divide", [10, 0, result_ptr])

if status == 0 {
    print("Success")
} else {
    print("Error: division by zero")
}
```

### 3. Thread Safety

```c
// Use thread-safe functions
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int thread_safe_increment(int* counter) {
    pthread_mutex_lock(&lock);
    int result = ++(*counter);
    pthread_mutex_unlock(&lock);
    return result;
}
```

### 4. Library Initialization

```c
// Initialization function
void library_init() {
    // Setup code
    initialize_resources();
}

// Cleanup function
void library_cleanup() {
    // Cleanup code
    free_resources();
}
```

```kuyil
load_library("mylib", "./libmylib.so")
register_function("mylib", "library_init", "void", [])
register_function("mylib", "library_cleanup", "void", [])

// Initialize
call_function("mylib", "library_init", [])

// Use library functions
// ...

// Cleanup
call_function("mylib", "library_cleanup", [])
```

## Debugging FFI Libraries

### Check Library Loading

```bash
# List symbols in library
nm -D libmylibrary.so

# Check dependencies
ldd libmylibrary.so

# On macOS
otool -L libmylibrary.dylib
```

### Common Issues

1. **Symbol not found**: Function not exported or name mangled
   - Solution: Use `extern "C"` in C++
   - Check with: `nm -D library.so | grep function_name`

2. **Library not found**: Wrong path or filename
   - Solution: Use absolute path or ensure library is in LD_LIBRARY_PATH

3. **Segmentation fault**: Type mismatch or memory error
   - Solution: Verify parameter types match exactly

4. **Wrong results**: Type size mismatch
   - Solution: Use explicit types (int32_t, int64_t, etc.)

## See Also

- [Interface System](INTERFACE_SYSTEM_GUIDE.md) - High-level library interfaces
- [Shared Library Guide](SHARED_LIBRARY_GUIDE.md) - Creating Kuyil standard libraries
- [Libraries Documentation](libraries/) - All available libraries
