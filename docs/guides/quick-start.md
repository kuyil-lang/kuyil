# Kuyil Quick Start Guide

Welcome to Kuyil! This guide will help you get started quickly with this fast scripting language that has built-in HTTP/REST support.

## Installation

### Prerequisites
- GCC compiler
- libcurl development libraries
- Make

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential libcurl4-openssl-dev
```

### macOS
```bash
brew install curl
```

### CentOS/RHEL
```bash
sudo yum install gcc curl-devel
```

## Building Kuyil

1. Clone or download the Kuyil source code
2. Navigate to the Kuyil directory
3. Build using make:

```bash
cd kuyil-lang
make all
```

Or use the automated build script:
```bash
./build.sh --full
```

## Your First Kuyil Program

Create a file called `hello.kyl`:

```kuyil
print("Hello, Kuyil!")

let name = "World"
print("Hello, " + name + "!")
```

Run it:
```bash
./kuyil hello.kyl
```

## Interactive Mode (REPL)

Start the interactive interpreter:
```bash
./kuyil
```

Try some commands:
```
> print("Hello!")
Hello!
> let x = 5 + 3
> print(x)
8
> exit
```

## Basic Examples

### Variables and Math
```kuyil
let a = 10
let b = 5
let sum = a + b
let product = a * b

print("Sum: " + sum)        // Sum: 15
print("Product: " + product) // Product: 50
```

### Functions
```kuyil
fn greet(name) {
    return "Hello, " + name + "!"
}

fn factorial(n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

print(greet("Alice"))    // Hello, Alice!
print(factorial(5))      // 120
```

### Control Flow
```kuyil
let age = 25

if age >= 18 {
    print("Adult")
} else {
    print("Minor")
}

// Loops
let i = 1
while i <= 3 {
    print("Count: " + i)
    i = i + 1
}
```

## HTTP Examples

### HTTP Client
```kuyil
// Simple GET request
let response = http.get("https://httpbin.org/get")
print("Status: " + response.status)

// POST with data
let data = {"name": "John", "age": 30}
let post_response = http.post("https://httpbin.org/post", data)
print("Posted successfully!")
```

### HTTP Server
```kuyil
let server = http.server(8080)

// Simple route
server.get("/", fn(req, res) {
    res.json({"message": "Welcome to Kuyil server!"})
})

// Route with parameters
server.get("/hello/:name", fn(req, res) {
    let name = req.params.name
    res.json({"greeting": "Hello, " + name + "!"})
})

print("Server running on http://localhost:8080")
server.listen()
```

## Compiling to Binary

Kuyil can compile scripts to standalone executables:

```bash
# Compile script to binary
./kuyil --compile myscript.kyl -o myapp

# Run the compiled binary
./myapp
```

## Common Use Cases

### 1. Quick API Testing
```kuyil
let api_url = "https://api.kylxample.com"
let token = "your-api-token"

fn test_endpoint(path) {
    let url = api_url + path
    let response = http.get(url)
    print("Testing " + path + ": " + response.status)
}

test_endpoint("/users")
test_endpoint("/products")
test_endpoint("/orders")
```

### 2. Simple Web Server
```kuyil
let server = http.server(3000)

// Serve static content
server.get("/", fn(req, res) {
    res.text("Welcome to my server!")
})

// API endpoint
server.get("/api/status", fn(req, res) {
    res.json({
        "status": "ok",
        "timestamp": Date.now(),
        "version": "1.0.0"
    })
})

server.listen()
```

### 3. Data Processing Script
```kuyil
fn process_data(items) {
    let processed = []
    let i = 0
    
    while i < items.length {
        let item = items[i]
        if item.value > 0 {
            processed.push({
                "id": item.id,
                "processed_value": item.value * 2
            })
        }
        i = i + 1
    }
    
    return processed
}

let data = [
    {"id": 1, "value": 10},
    {"id": 2, "value": -5},
    {"id": 3, "value": 20}
]

let result = process_data(data)
print("Processed " + result.length + " items")
```

## Development Tips

### 1. Use the REPL for experimentation
```bash
./kuyil
> let test = "Hello"
> print(test + " World")
```

### 2. Test HTTP endpoints interactively
```bash
./kuyil
> let response = http.get("https://httpbin.org/json")
> print(response.status)
> print(response.text)
```

### 3. Use the build script for development
```bash
# Full build and test pipeline
./build.sh --full

# Just build and test
./build.sh -b -t

# Run performance benchmark
./build.sh -p
```

## Debugging

### 1. Add debug prints
```kuyil
fn debug_function(x) {
    print("DEBUG: Input value = " + x)
    let result = x * 2
    print("DEBUG: Result = " + result)
    return result
}
```

### 2. Check HTTP responses
```kuyil
let response = http.get("https://api.kylxample.com/data")
print("Status: " + response.status)
print("Headers: " + response.headers)
print("Body: " + response.text)
```

### 3. Validate data
```kuyil
fn safe_divide(a, b) {
    if b == 0 {
        print("ERROR: Division by zero")
        return nil
    }
    return a / b
}
```

## What's Next?

1. **Read the Language Reference**: Check out `docs/language-reference.md` for complete syntax and features
2. **Explore Examples**: Look at the `examples/` directory for more complex examples
3. **Build Something**: Start with a simple HTTP client or server
4. **Contribute**: Kuyil is open source - contribute features or report bugs

## Getting Help

- Check the examples in the `examples/` directory
- Read the full language reference in `docs/language-reference.md`
- Use the REPL to experiment with code
- Look at the source code for implementation details

Happy coding with Kuyil! 🚀