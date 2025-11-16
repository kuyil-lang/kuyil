# Import System in Kuyil

Module system for importing and using other Kuyil scripts and libraries.

## Table of Contents
- [Basic Imports](#basic-imports)
- [Import Syntax](#import-syntax)
- [What Gets Imported](#what-gets-imported)
- [Import Paths](#import-paths)
- [Namespace Imports](#namespace-imports)
- [Best Practices](#best-practices)

## Basic Imports

Import functions and definitions from other Kuyil files.

### Syntax (Two Forms)

```kuyil
// New syntax: without parentheses
import "path/to/module.kyl"

// Traditional syntax: with parentheses
import("path/to/module.kyl")
```

Both forms work identically - use whichever you prefer!

## Import Syntax

### Simple Import

```kuyil
// my_utils.kyl
fn add(a, b) {
    return a + b
}

fn multiply(a, b) {
    return a * b
}

let PI = 3.14159
```

```kuyil
// main.kyl
import "my_utils.kyl"

// Now you can use the functions
let sum = add(10, 20)
let product = multiply(5, 6)
print("Sum:", sum)         // 30
print("Product:", product) // 30
```

### Both Syntaxes Work

```kuyil
// These are equivalent:
import "module.kyl"
import("module.kyl")

// Both import the same way
let result = someFunction()
```

## What Gets Imported

When you import a module, you get:

### ✅ Functions

```kuyil
// utils.kyl
fn greet(name) {
    return "Hello, " + name
}

fn farewell(name) {
    return "Goodbye, " + name
}
```

```kuyil
// main.kyl
import "utils.kyl"

print(greet("Alice"))    // Works!
print(farewell("Bob"))   // Works!
```

### ✅ Avatar Functions

```kuyil
// api.kyl
avatar fn fetchData(url) {
    return http.get(url).json()
}
```

```kuyil
// main.kyl
import "api.kyl"

let data = await fetchData("https://api.example.com")
```

### ❌ Variables (Currently Not Supported)

```kuyil
// config.kyl
let API_KEY = "secret123"
let MAX_RETRIES = 3
```

```kuyil
// main.kyl
import "config.kyl"

// These won't work - variables aren't imported
// print(API_KEY)     // Error: undefined
// print(MAX_RETRIES) // Error: undefined
```

**Workaround**: Use functions to access module data:

```kuyil
// config.kyl
fn getApiKey() {
    return "secret123"
}

fn getMaxRetries() {
    return 3
}
```

```kuyil
// main.kyl
import "config.kyl"

let key = getApiKey()     // Works!
let retries = getMaxRetries()  // Works!
```

## Import Paths

### Relative Paths

```kuyil
// Current directory
import "module.kyl"
import("module.kyl")

// Subdirectory
import "utils/math.kyl"
import "utils/string.kyl"

// Parent directory
import "../shared/common.kyl"

// Multiple levels
import "../../lib/helpers.kyl"
```

### Absolute Paths (Not Recommended)

```kuyil
// Avoid absolute paths for portability
import "/home/user/project/module.kyl"  // Bad!

// Better: Use relative paths
import "module.kyl"  // Good!
```

## Namespace Imports

Import with a namespace to avoid name conflicts (syntax is parsed but runtime support is limited).

### Syntax

```kuyil
import "module.kyl" as namespace
```

### Example (Limited Support)

```kuyil
// math_utils.kyl
fn abs(x) {
    if x < 0 {
        return -x
    }
    return x
}

fn max(a, b) {
    if a > b {
        return a
    }
    return b
}
```

```kuyil
// main.kyl
import "math_utils.kyl" as math

// Note: Namespace access may have limited support
// let result = math.abs(-10)  // May not work yet
```

**Current Limitation**: The `import "file" as namespace` syntax is recognized by the parser, but runtime namespace object access has limitations. Use regular imports for now.

## Best Practices

### 1. Organize Related Functions

```kuyil
// Good: Grouped by functionality
// utils/math.kyl - Math functions
// utils/string.kyl - String functions
// utils/validation.kyl - Validation functions

// Bad: Everything in one file
// utils.kyl - All utilities mixed together
```

### 2. Use Relative Imports

```kuyil
// Good: Portable
import "utils/helpers.kyl"
import "../shared/common.kyl"

// Bad: Not portable
import "/home/user/project/utils/helpers.kyl"
```

### 3. Import at the Top

```kuyil
// Good: All imports at the top
import "module1.kyl"
import "module2.kyl"
import "module3.kyl"

fn main() {
    // Your code
}

// Bad: Imports scattered
fn main() {
    import "module1.kyl"  // Don't do this
    // code
}
```

### 4. Avoid Circular Imports

```kuyil
// Bad: Circular dependency
// file_a.kyl
import "file_b.kyl"
fn functionA() { functionB() }

// file_b.kyl
import "file_a.kyl"
fn functionB() { functionA() }  // Circular!

// Good: Break the cycle
// file_a.kyl
import "common.kyl"
fn functionA() { commonFunction() }

// file_b.kyl
import "common.kyl"
fn functionB() { commonFunction() }

// common.kyl
fn commonFunction() { /*...*/ }
```

### 5. Name Your Modules Clearly

```kuyil
// Good: Clear names
import "user_authentication.kyl"
import "data_validation.kyl"
import "http_client.kyl"

// Bad: Unclear names
import "stuff.kyl"
import "misc.kyl"
import "utils.kyl"
```

## Import Order Conventions

Follow a consistent import order:

```kuyil
// 1. Standard library (if separate)
import "std/math.kyl"
import "std/string.kyl"

// 2. Third-party modules (if any)
import "vendor/library.kyl"

// 3. Local project modules
import "utils/helpers.kyl"
import "models/user.kyl"
import "services/api.kyl"

// 4. Current directory modules
import "config.kyl"
```

## Module Patterns

### Utility Module Pattern

```kuyil
// utils/math.kyl
fn add(a, b) {
    return a + b
}

fn subtract(a, b) {
    return a - b
}

fn multiply(a, b) {
    return a * b
}

fn divide(a, b) {
    if b == 0 {
        return nil
    }
    return a / b
}
```

```kuyil
// main.kyl
import "utils/math.kyl"

let result = add(10, 20)
```

### Factory Pattern

```kuyil
// factories/user_factory.kyl
struct User {
    id
    name
    email
}

fn createUser(id, name, email) {
    return User{
        id: id,
        name: name,
        email: email
    }
}

fn createGuestUser() {
    return User{
        id: 0,
        name: "Guest",
        email: ""
    }
}
```

```kuyil
// main.kyl
import "factories/user_factory.kyl"

let user = createUser(1, "Alice", "alice@example.com")
let guest = createGuestUser()
```

### Service Pattern

```kuyil
// services/api_service.kyl
avatar fn fetchUsers() {
    return http.get("https://api.example.com/users").json()
}

avatar fn createUser(user_data) {
    return http.post("https://api.example.com/users", user_data).json()
}

avatar fn updateUser(id, user_data) {
    return http.put("https://api.example.com/users/" + id, user_data).json()
}
```

```kuyil
// main.kyl
import "services/api_service.kyl"

let users = await fetchUsers()
let new_user = await createUser({name: "Bob", email: "bob@example.com"})
```

## Common Issues

### Import Not Found

```kuyil
// Error: Cannot import "module.kyl"
import "module.kyl"

// Solutions:
// 1. Check the file exists
// 2. Check the path is correct
// 3. Use relative path
import "./module.kyl"
```

### Functions Not Available

```kuyil
import "utils.kyl"

// Error: Undefined function
someFunction()

// Solutions:
// 1. Check function is defined in utils.kyl
// 2. Check function is exported (top-level)
// 3. Check for typos
```

### Import Executed Multiple Times

```kuyil
// Imports are cached - modules are only executed once
import "setup.kyl"  // Executes setup
import "setup.kyl"  // Uses cached version, doesn't re-execute
```

## Interface Imports

For using interfaces from shared libraries:

```kuyil
// Import interface definitions
import "interfaces/interface_str.kyl"
import "interfaces/interface_math.kyl"

// Now you can use the interface functions
let result = str_to_upper("hello")
let abs_val = math_abs(-42)
```

See [Interface System Guide](INTERFACE_SYSTEM_GUIDE.md) for more details.

## See Also

- [Functions](functions.md) - Defining functions to export
- [Avatars](avatars.md) - Async functions and imports
- [Interface System](INTERFACE_SYSTEM_GUIDE.md) - Using library interfaces
- [FFI Guide](ffi-guide.md) - Loading native libraries
