# Avatars - Concurrent Programming in Kuyil

Avatars are Kuyil's approach to concurrent programming, providing async/await functionality.

## Table of Contents
- [What are Avatars?](#what-are-avatars)
- [Defining Avatar Functions](#defining-avatar-functions)
- [Using await](#using-await)
- [Concurrency Model](#concurrency-model)
- [Best Practices](#best-practices)

## What are Avatars?

Avatars are asynchronous functions that can run concurrently without blocking the main thread. They are similar to async/await in other languages but with Kuyil's unique implementation.

### Key Features

- **Non-blocking**: Avatar functions don't block the main thread
- **Concurrent execution**: Multiple avatars can run at the same time
- **Simple syntax**: Easy to use `avatar` and `await` keywords
- **Integration**: Works seamlessly with all Kuyil libraries

## Defining Avatar Functions

Use the `avatar` keyword before `fn` to declare an asynchronous function.

### Syntax

```kuyil
avatar fn functionName(parameters) {
    // async code
}
```

### Examples

```kuyil
// Simple avatar function
avatar fn sayHello(name) {
    return "Hello, " + name + "!"
}

// Avatar with HTTP request
avatar fn fetchUser(id) {
    let response = http.get("https://api.example.com/users/" + id)
    return response.json()
}

// Avatar with delay
avatar fn delayedMessage(message, delay_ms) {
    system.sleep(delay_ms)
    return message
}

// Avatar with error handling
avatar fn safeApiCall(url) {
    let response = http.get(url)
    
    if response.status == 200 {
        return {success: true, data: response.json()}
    } else {
        return {success: false, error: "HTTP " + response.status}
    }
}
```

## Using await

Use `await` to wait for an avatar function to complete and get its result.

### Syntax

```kuyil
let result = await avatarFunction(arguments)
```

### Examples

```kuyil
// Define avatar
avatar fn getData() {
    let response = http.get("https://api.example.com/data")
    return response.json()
}

// Use with await
let data = await getData()
print("Data:", data)

// Multiple awaits
avatar fn getUser(id) {
    return http.get("/api/users/" + id).json()
}

avatar fn getOrders(userId) {
    return http.get("/api/orders?user=" + userId).json()
}

// Call avatars in sequence
let user = await getUser(123)
let orders = await getOrders(user.id)

print("User:", user.name)
print("Orders:", orders)
```

### Parallel Execution

```kuyil
// Start multiple avatars (they run concurrently)
avatar fn fetchData1() {
    return http.get("https://api1.example.com/data").json()
}

avatar fn fetchData2() {
    return http.get("https://api2.example.com/data").json()
}

avatar fn fetchData3() {
    return http.get("https://api3.example.com/data").json()
}

// Start all three (they run in parallel)
let result1 = await fetchData1()
let result2 = await fetchData2()
let result3 = await fetchData3()

print("All data fetched!")
```

## Concurrency Model

Avatars in Kuyil use a lightweight concurrency model:

### How It Works

1. **Avatar Creation**: When an avatar function is called, it starts executing
2. **Non-blocking**: The caller can continue without waiting
3. **await Synchronization**: `await` waits for the avatar to complete
4. **Result Return**: Avatar returns its result to the awaiter

### Example Flow

```kuyil
print("1. Starting")

// Start avatar (doesn't block)
avatar fn longTask() {
    system.sleep(2000)  // Simulate long task
    return "Done!"
}

print("2. Avatar started")

// Do other work
print("3. Doing other work...")

// Wait for result
let result = await longTask()
print("4. Avatar result:", result)
print("5. Finished")

// Output:
// 1. Starting
// 2. Avatar started
// 3. Doing other work...
// [~2 seconds later]
// 4. Avatar result: Done!
// 5. Finished
```

## Best Practices

### 1. Use Avatars for I/O Operations

```kuyil
// Good: I/O-bound operations
avatar fn fetchDataFromAPI() {
    return http.get("https://api.example.com/data")
}

avatar fn readLargeFile(path) {
    return fileio.readFile(path)
}

avatar fn queryDatabase(query) {
    return db.execute(query)
}

// Not necessary: CPU-bound operations (but won't hurt)
avatar fn calculateSum(numbers) {
    let sum = 0
    for i = 0; i < 1000000; i++ {
        sum += numbers[i]
    }
    return sum
}
```

### 2. Error Handling

```kuyil
avatar fn safeHttpRequest(url) {
    let response = http.get(url)
    
    if response.status >= 200 and response.status < 300 {
        return {
            success: true,
            data: response.json()
        }
    } else {
        return {
            success: false,
            error: "HTTP error: " + response.status
        }
    }
}

// Usage
let result = await safeHttpRequest("https://api.example.com")

if result.success {
    print("Data:", result.data)
} else {
    print("Error:", result.error)
}
```

### 3. Avoid await in Loops (When Possible)

```kuyil
// Bad: Sequential awaits (slow)
let results = []
for i = 0; i < 10; i++ {
    let data = await fetchData(i)  // Waits for each one
    results.push(data)
}

// Better: Start all, then await all
let promises = []
for i = 0; i < 10; i++ {
    promises.push(fetchData(i))  // Start all
}

let results = []
for i = 0; i < 10; i++ {
    results.push(await promises[i])  // Wait for results
}
```

### 4. Timeout Pattern

```kuyil
avatar fn withTimeout(timeout_ms, task_fn) {
    let start = system.currentTimeMillis()
    let result = await task_fn()
    let elapsed = system.currentTimeMillis() - start
    
    if elapsed > timeout_ms {
        return {timeout: true}
    }
    
    return {timeout: false, result: result}
}

// Usage
avatar fn slowOperation() {
    system.sleep(5000)
    return "Done"
}

let result = await withTimeout(3000, slowOperation)

if result.timeout {
    print("Operation timed out!")
} else {
    print("Result:", result.result)
}
```

### 5. Retry Pattern

```kuyil
avatar fn retry(fn, max_attempts) {
    let attempts = 0
    
    while attempts < max_attempts {
        let result = await fn()
        
        if result.success {
            return result
        }
        
        attempts++
        if attempts < max_attempts {
            system.sleep(1000 * attempts)  // Exponential backoff
        }
    }
    
    return {success: false, error: "Max retries exceeded"}
}

// Usage
avatar fn unreliableOperation() {
    let response = http.get("https://flaky-api.example.com")
    return {success: response.status == 200, data: response.json()}
}

let result = await retry(unreliableOperation, 3)
print(result)
```

## Real-World Examples

### HTTP API Calls

```kuyil
avatar fn fetchWeather(city) {
    let url = "https://api.weather.com/v1/weather?city=" + city
    let response = http.get(url)
    return response.json()
}

avatar fn fetchForecast(city, days) {
    let url = "https://api.weather.com/v1/forecast?city=" + city + "&days=" + days
    let response = http.get(url)
    return response.json()
}

// Get weather and forecast concurrently
let weather = await fetchWeather("London")
let forecast = await fetchForecast("London", 7)

print("Current:", weather.temp)
print("Forecast:", forecast)
```

### Database Operations

```kuyil
avatar fn getUserById(user_id) {
    let query = "SELECT * FROM users WHERE id = " + user_id
    return db.execute(query)
}

avatar fn getUserOrders(user_id) {
    let query = "SELECT * FROM orders WHERE user_id = " + user_id
    return db.execute(query)
}

// Fetch user and their orders
let user = await getUserById(123)
let orders = await getUserOrders(123)

print("User:", user.name)
print("Order count:", orders.length)
```

### File Processing

```kuyil
avatar fn processFile(filename) {
    let content = fileio.readFile(filename)
    let processed = transform(content)
    fileio.writeFile(filename + ".out", processed)
    return {filename: filename, size: content.length}
}

// Process multiple files
let files = ["file1.txt", "file2.txt", "file3.txt"]
let results = []

for i = 0; i < files.length; i++ {
    let result = await processFile(files[i])
    results.push(result)
    print("Processed:", result.filename)
}
```

## Common Patterns

### Promise-like Pattern

```kuyil
avatar fn fetchData() {
    return http.get("https://api.example.com/data").json()
}

avatar fn processData(data) {
    // Process data
    return data.processed
}

// Chain avatars
let data = await fetchData()
let processed = await processData(data)
print(processed)
```

### Parallel Fetch

```kuyil
avatar fn fetchAll(urls) {
    let results = []
    
    // Start all requests
    for i = 0; i < urls.length; i++ {
        results.push(http.get(urls[i]))
    }
    
    // Wait for all
    let data = []
    for i = 0; i < results.length; i++ {
        data.push(await results[i].json())
    }
    
    return data
}

// Usage
let urls = [
    "https://api.example.com/data1",
    "https://api.example.com/data2",
    "https://api.example.com/data3"
]

let allData = await fetchAll(urls)
print("Fetched", allData.length, "items")
```

## See Also

- [Functions](functions.md) - Regular function basics
- [HTTP Library](libraries/http.md) - HTTP operations with avatars
- [System Library](libraries/system.md) - Sleep and timing functions
- [Event Loop](libraries/eventloop.md) - Event-driven programming
