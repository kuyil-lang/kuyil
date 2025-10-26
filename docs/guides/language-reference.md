# Kuyil Language Reference

## Table of Contents
1. [Basic Syntax](#basic-syntax)
2. [Data Types](#data-types)
3. [Variables](#variables)
4. [Functions](#functions)
5. [Control Flow](#control-flow)
6. [Built-in Functions](#built-in-functions)
7. [HTTP Support](#http-support)
8. [Error Handling](#error-handling)

## Basic Syntax

Kuyil uses a clean, readable syntax inspired by modern scripting languages.

### Comments
```kuyil
// Single line comment

/*
Multi-line comment
Can span multiple lines
*/
```

### Statements
Statements can be terminated with semicolons (optional) or newlines:
```kuyil
print("Hello")
print("World");
```

## Data Types

### Primitive Types
- **nil**: Represents absence of value
- **bool**: `true` or `false`
- **number**: Floating-point numbers (64-bit)
- **string**: Text enclosed in double quotes

```kuyil
let nothing = nil
let flag = true
let count = 42
let pi = 3.14159
let message = "Hello, World!"
```

### Collections (Future)
- **array**: Ordered collection of values
- **object**: Key-value pairs (dictionary/map)

## Variables

### Declaration
```kuyil
let name = "Alice"        // String
let age = 30              // Number
let is_student = false    // Boolean
let data                  // Defaults to nil
```

### Assignment
```kuyil
let x = 10
x = 20              // Reassignment
x = x + 5           // Update
```

### Compound Assignment (Future)
```kuyil
x += 5              // x = x + 5
x -= 3              // x = x - 3
x *= 2              // x = x * 2
x /= 4              // x = x / 4
```

## Functions

### Function Declaration
```kuyil
fn greet(name) {
    return "Hello, " + name + "!"
}

fn add(a, b) {
    return a + b
}

// Function with no parameters
fn get_pi() {
    return 3.14159
}

// Function with no return (returns nil)
fn say_hello() {
    print("Hello!")
}
```

### Function Calls
```kuyil
let message = greet("World")
let sum = add(5, 3)
say_hello()
```

### Anonymous Functions (Future)
```kuyil
let multiply = fn(a, b) {
    return a * b
}

let result = multiply(4, 7)
```

## Control Flow

### Conditional Statements
```kuyil
let age = 18

if age >= 18 {
    print("You can vote!")
} else {
    print("Too young to vote")
}

// Nested conditions
if age >= 21 {
    print("Can drink alcohol")
} else if age >= 18 {
    print("Can vote but not drink")
} else {
    print("Minor")
}
```

### Loops

#### While Loops
```kuyil
let i = 1
while i <= 5 {
    print("Count: " + i)
    i = i + 1
}
```

#### For Loops (Future)
```kuyil
for let i = 0; i < 10; i = i + 1 {
    print(i)
}

// For-in loops
for item in [1, 2, 3, 4, 5] {
    print(item)
}
```

### Break and Continue (Future)
```kuyil
let i = 0
while true {
    i = i + 1
    
    if i == 3 {
        continue  // Skip iteration
    }
    
    if i == 8 {
        break     // Exit loop
    }
    
    print(i)
}
```

## Built-in Functions

### Input/Output
```kuyil
print("Hello, World!")           // Print to console
print("Value:", 42)              // Multiple arguments
```

### Type Conversion (Future)
```kuyil
let str_num = "42"
let num = parseInt(str_num)      // Convert to number
let text = toString(num)         // Convert to string
```

### String Operations (Future)
```kuyil
let text = "Hello, World!"
print(text.length)               // String length
print(text.toUpperCase())        // Uppercase
print(text.toLowerCase())        // Lowercase
print(text.substring(0, 5))      // Substring
```

### Math Operations (Future)
```kuyil
print(Math.abs(-5))              // Absolute value
print(Math.sqrt(16))             // Square root
print(Math.pow(2, 8))            // Power
print(Math.floor(3.7))           // Floor
print(Math.ceil(3.2))            // Ceiling
print(Math.round(3.6))           // Round
```

## HTTP Support

Kuyil includes built-in HTTP client and server functionality.

### HTTP Client

#### GET Request
```kuyil
let response = http.get("https://api.github.com/users/octocat")
if response.status == 200 {
    let user = response.json()
    print("User: " + user.name)
} else {
    print("Error: " + response.status)
}
```

#### POST Request
```kuyil
let data = {
    "name": "John Doe",
    "email": "john@example.com"
}

let response = http.post("https://httpbin.org/post", data)
print("Status: " + response.status)
print("Response: " + response.text)
```

#### Other HTTP Methods
```kuyil
// PUT request
let put_response = http.put("https://httpbin.org/put", data)

// DELETE request
let delete_response = http.delete("https://httpbin.org/delete")
```

### HTTP Server

#### Basic Server
```kuyil
let server = http.server(8080)

server.get("/", fn(req, res) {
    res.json({"message": "Hello, World!"})
})

server.post("/users", fn(req, res) {
    let user_data = req.json()
    // Process user data
    res.status(201).json({"id": 123, "created": true})
})

print("Server starting on http://localhost:8080")
server.listen()
```

#### Route Parameters
```kuyil
server.get("/users/:id", fn(req, res) {
    let user_id = req.params.id
    res.json({"user_id": user_id})
})
```

#### Response Methods
```kuyil
// JSON response
res.json({"key": "value"})

// Text response
res.text("Plain text response")

// Status codes
res.status(404).json({"error": "Not found"})
res.status(201).json({"created": true})
```

## Error Handling

### Basic Error Handling
```kuyil
fn safe_divide(a, b) {
    if b == 0 {
        return {"error": "Division by zero"}
    }
    return {"result": a / b}
}

let result = safe_divide(10, 0)
if result.kylrror {
    print("Error: " + result.kylrror)
} else {
    print("Result: " + result.result)
}
```

### Try-Catch (Future)
```kuyil
try {
    let result = risky_operation()
    print("Success: " + result)
} catch error {
    print("Error: " + error.message)
} finally {
    print("Cleanup code")
}
```

## Operators

### Arithmetic Operators
```kuyil
let a = 10
let b = 3

print(a + b)    // Addition: 13
print(a - b)    // Subtraction: 7
print(a * b)    // Multiplication: 30
print(a / b)    // Division: 3.333...
print(a % b)    // Modulo: 1
```

### Comparison Operators
```kuyil
print(5 == 5)   // Equal: true
print(5 != 3)   // Not equal: true
print(5 > 3)    // Greater than: true
print(5 >= 5)   // Greater or equal: true
print(3 < 5)    // Less than: true
print(3 <= 5)   // Less or equal: true
```

### Logical Operators
```kuyil
print(true && false)   // AND: false
print(true || false)   // OR: true
print(!true)           // NOT: false
```

### String Concatenation
```kuyil
let first = "Hello"
let last = "World"
let message = first + ", " + last + "!"
print(message)  // "Hello, World!"
```

## Best Practices

### Code Organization
- Use meaningful variable and function names
- Keep functions small and focused
- Group related functionality together

### Performance Tips
- Minimize HTTP requests in loops
- Use local variables when possible
- Avoid deep recursion for large datasets

### Security
- Validate input data
- Use HTTPS for sensitive data
- Sanitize user input before processing

## Examples

### Simple Calculator
```kuyil
fn calculate(op, a, b) {
    if op == "+" {
        return a + b
    } else if op == "-" {
        return a - b
    } else if op == "*" {
        return a * b
    } else if op == "/" {
        if b == 0 {
            return nil
        }
        return a / b
    } else {
        return nil
    }
}

print(calculate("+", 5, 3))  // 8
print(calculate("/", 10, 2)) // 5
```

### REST API Client
```kuyil
fn fetch_user(user_id) {
    let url = "https://api.kylxample.com/users/" + user_id
    let response = http.get(url)
    
    if response.status == 200 {
        return response.json()
    } else {
        return nil
    }
}

let user = fetch_user("123")
if user {
    print("User name: " + user.name)
} else {
    print("User not found")
}
```