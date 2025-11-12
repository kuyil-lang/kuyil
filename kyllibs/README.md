# Kuyil Standard Library (kyllibs/)

This folder contains the standard library for Kuyil.

## ⚠️ Important: Current Limitation

**loadlib from imported files doesn't register functions globally**. This is a fundamental limitation of how Kuyil's import and loadlib work together.

**What this means**:
- `import("asyncio")` will execute kyllibs/asyncio.kyl
- But the `loadlib()` call inside that file registers functions in the import's scope only
- Your script won't have access to those functions
  
**Workaround**: Call `loadlib()` directly in your script.

## Usage

```kuyil
// Load the library directly in your script
loadlib("libs/libkylasyncio.so")

// Now you can use the functions
let req = kyl_asyncio_httpGet("https://example.com")
let result = kyl_asyncio_waitResult(req, 5000)
```

## Available Libraries

### asyncio

Asynchronous I/O operations with HTTP support.

**Setup**:
```kuyil
loadlib("libs/libkylasyncio.so")
```

**Available Functions**:
- `kyl_asyncio_httpGet(url)` - Simple GET request → returns requestId
- `kyl_asyncio_httpPost(url, body)` - Simple POST request → returns requestId  
- `kyl_asyncio_httpGetEx(url, headers, dns_ms, connect_ms, read_ms, write_ms)` - GET with options → returns requestId
- `kyl_asyncio_httpPostEx(url, body, headers, dns_ms, connect_ms, read_ms, write_ms)` - POST with options → returns requestId
- `kyl_asyncio_waitResult(requestId, timeout_ms)` - Wait for result → returns byte array
- `kyl_asyncio_isComplete(requestId)` - Check if done → returns bool
- `kyl_asyncio_getResult(requestId)` - Get result without waiting → returns byte array or nil
- `kyl_asyncio_fileRead(path)` - Async file read → returns requestId
- `kyl_asyncio_fileWrite(path, data)` - Async file write → returns requestId

**Example - Simple GET**:
```kuyil
loadlib("libs/libkylasyncio.so")

let req = kyl_asyncio_httpGet("https://httpbin.org/get")
let result = kyl_asyncio_waitResult(req, 5000)  // 5 second timeout
print("Response:", result)
```

**Example - GET with Headers & Timeouts**:
```kuyil
loadlib("libs/libkylasyncio.so")

let headers = [
    "Authorization: Bearer token123",
    "Accept: application/json",
    "User-Agent: Kuyil/1.0"
]

let req = kyl_asyncio_httpGetEx(
    "https://api.example.com/data",
    headers,
    0,      // dns_timeout (0 = use default)
    1000,   // connect_timeout (1 second)
    3000,   // read_timeout (3 seconds)
    500     // write_timeout (500ms)
)

let result = kyl_asyncio_waitResult(req, 5000)
print("Got response:", len(result), "bytes")
```

**Example - POST with JSON**:
```kuyil
loadlib("libs/libkylasyncio.so")

let body = '{"name": "Kuyil", "action": "test"}'
let headers = [
    "Content-Type: application/json",
    "Accept: application/json"
]

let req = kyl_asyncio_httpPostEx(
    "https://httpbin.org/post",
    body,
    headers,
    0, 1000, 3000, 500  // timeouts
)

let result = kyl_asyncio_waitResult(req, 5000)
print("POST successful")
```

**Example - Multiple Concurrent Requests**:
```kuyil
loadlib("libs/libkylasyncio.so")

// Start all requests immediately (non-blocking)
let req1 = kyl_asyncio_httpGet("https://httpbin.org/delay/1")
let req2 = kyl_asyncio_httpGet("https://httpbin.org/delay/1")
let req3 = kyl_asyncio_httpGet("https://httpbin.org/delay/1")

// Wait for all to complete
let res1 = kyl_asyncio_waitResult(req1, 5000)
let res2 = kyl_asyncio_waitResult(req2, 5000)
let res3 = kyl_asyncio_waitResult(req3, 5000)

print("All 3 requests completed!")
```

## Technical Notes

### Why Direct loadlib?

Kuyil's import mechanism executes imported files in an isolated scope. When `loadlib()` is called from within an imported file, the function registrations happen in that isolated scope and don't propagate to the calling script.

The only way to make library functions available is to call `loadlib()` directly in your script.

### Resolution Order

The kyllibs folder exists for future use when this limitation is resolved. Currently:

1. Documentation and examples go in kyllibs/
2. Users must call `loadlib()` directly in their scripts
3. Function names use the `kyl_<library>_` prefix (e.g., `kyl_asyncio_httpGet`)

### Future Improvements

Potential solutions (not yet implemented):
- Make loadlib register functions globally regardless of call context
- Add a "prelude" system that auto-executes before user scripts
- Modify import to support explicit exports
- Create a package system with proper namespacing
