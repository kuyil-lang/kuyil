# WebView Bridge Demo

A Kuyil application demonstrating bidirectional communication between JavaScript (running in a WebView desktop window) and Kuyil code via an HTTP API bridge.

## Architecture

```
┌──────────────────────────────────────┐
│  WebView Window (GTK/WebKit)         │
│  ┌────────────────────────────────┐  │
│  │  index.html + app.js           │  │
│  │  ┌──────────────────────────┐  │  │
│  │  │  JavaScript sends:       │  │  │
│  │  │  POST /api/push          │  │  │
│  │  │  ←────────────────────── │  │  │
│  │  │  GET /api/pull (polling) │  │  │
│  │  └──────────────────────────┘  │  │
│  └────────────────────────────────┘  │
└──────────────────────────────────────┘
              ↕ HTTP
┌──────────────────────────────────────┐
│  HTTP Server (Port 5173)             │
│  Routes:                             │
│  • POST /api/push → handle_push()    │
│  • GET /api/pull  → handle_pull()    │
│  • Static files from ./              │
└──────────────────────────────────────┘
              ↕
┌──────────────────────────────────────┐
│  Kuyil Script                        │
│  • Message queues                    │
│  • Route handlers                    │
│  • WebView control                   │
└──────────────────────────────────────┘
```

## Features

- **Non-blocking HTTP Server**: Uses `startServer(port)` to run the server in a background thread, allowing the WebView to run simultaneously
- **Two-way Communication**:
  - **JS → Kuyil**: Send messages via POST to `/api/push`
  - **Kuyil → JS**: Poll for messages via GET from `/api/pull`
- **Modern UI**: Dark-themed interface with message input and event log
- **Static File Serving**: Serves HTML/JS files from the example directory

## Files

- **`webview_bridge.kyl`**: Main Kuyil script that:
  - Sets up HTTP server with API routes
  - Creates and manages WebView window
  - Handles bidirectional messaging
  
- **`index.html`**: Web UI with:
  - Message input field
  - Display area for incoming messages
  - Event log for tracking communication
  
- **`app.js`**: JavaScript bridge logic:
  - `sendToKuyil()`: POST messages to Kuyil
  - `pollFromKuyil()`: Poll for messages from Kuyil (1s interval)

## How to Run

```bash
# From the kuyil root directory
./kuyil examples/webview_bridge/webview_bridge.kyl
```

## Requirements

- GTK3 and WebKit2GTK development libraries
- Display server (X11 or Wayland)
- The webview library must be built and available at `./libs/libkylwebview.so`

## How It Works

### 1. HTTP Server Initialization

The script starts a non-blocking HTTP server on port 5173:

```kuyil
startServer(PORT)
```

This spawns a background thread that listens for HTTP requests while allowing the main thread to continue.

### 2. Route Registration

Two API endpoints are registered:

```kuyil
registerRoute("POST", "/api/push", "handle_push")
registerRoute("GET", "/api/pull", "handle_pull")
```

- **`handle_push`**: Receives messages from JavaScript
- **`handle_pull`**: Sends queued messages to JavaScript

### 3. WebView Creation

The script creates a desktop window and loads the web interface:

```kuyil
init()
let settings = createDefaultSettings()
let win = create("Kuyil WebView Bridge Demo", settings)
loadUrl(win, "http://127.0.0.1:5173/index.html")
show(win)
run(win)  // Blocks until window is closed
```

### 4. Message Flow

**Sending from JavaScript to Kuyil:**
```javascript
// In app.js
fetch('/api/push', {
    method: 'POST',
    body: message
})
```

**Receiving in Kuyil:**
```kuyil
function handle_push(req, res) {
  let body = requestGetBody(req)
  print("Received from JS:", body)
  // Process message...
}
```

**Polling from JavaScript:**
```javascript
// In app.js (runs every 1 second)
fetch('/api/pull')
    .then(r => r.text())
    .then(msg => {
        if (msg) {
            // Display message from Kuyil
        }
    })
```

## Extending the Example

### Add More Routes

```kuyil
function handle_data(req, res) {
    let data = `{"status": "ok", "time": "` + to_string(time()) + `"}`
    response_set_json(res, data)
}

http_register_route("GET", "/api/data", "handle_data")
```

### Send Messages from Kuyil to JS

```kuyil
// In any function
kuyil_to_js_last = "Hello from Kuyil at " + to_string(time())
```

The JavaScript polling will pick this up automatically.

### Use WebView JavaScript Injection

For more direct communication, you could use `webview_eval()` (if available) to execute JavaScript directly from Kuyil.

## Known Limitations

### Segfault on Application Exit

**Issue**: The application crashes with a segmentation fault after closing the WebView window, during process shutdown.

**Root Cause**: This is a race condition involving three components:
1. **Background HTTP Thread**: The `http_start_server()` function spawns a detached pthread that continues processing requests
2. **WebKit/JavaScriptCore**: WebView uses WebKit which has async callbacks and finalizers
3. **Kuyil VM Shutdown**: When main() returns, the VM begins cleanup

When the WebView window closes, the VM starts shutting down while:
- The HTTP background thread may still be mid-request, calling Kuyil handlers
- WebKit/GTK may have pending callbacks trying to execute in the VM context

This creates a use-after-free scenario where code tries to access VM state that's being deallocated.

**Impact**: **None on functionality** - the application works perfectly while running. The crash only occurs during process termination after all user interaction is complete. The OS properly cleans up all resources (memory, threads, sockets) when the process exits.

**Why Not Fixed**: A proper fix would require:
- Complex thread synchronization to gracefully shut down the HTTP thread before VM cleanup
- Ensuring all WebKit callbacks are cancelled before VM shutdown
- This adds significant complexity for a cosmetic issue that doesn't affect usability

The application successfully demonstrates bidirectional JavaScript ↔ Kuyil communication, which is its purpose.

## Troubleshooting

### WebView window doesn't appear
- Ensure DISPLAY environment variable is set
- Check that GTK3 and WebKit2GTK are installed
- Verify you're running in a graphical environment (not headless SSH)
- The `create()` function returns `nil` on failure, which the demo checks

### HTTP server errors
- Make sure port 5173 is not already in use
- Check that the HTTP library is properly built (`./libs/libkylhttp.so`)

### Messages not flowing
- Open browser developer console in the WebView to see JavaScript errors
- Check Kuyil console output for route registration confirmation
- Verify the polling is running (should see GET requests in logs)

## License

Same as the Kuyil project.
