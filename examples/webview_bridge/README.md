# Working WebView Bridge Demo

This is the fully functional webview bridge implementation that demonstrates bidirectional communication between Kuyil and JavaScript.

## Features

✅ **Push-based responses** - No polling needed, uses `eval()` to push responses immediately
✅ **Avatar spawning** - Background workers run in avatars for non-blocking execution
✅ **Multiple sequential requests** - Handles continuous requests without corruption
✅ **Clean architecture** - Webview callbacks set flags, main loop spawns avatars

## Architecture

```
JavaScript              Kuyil Main Loop           Avatar Workers
   |                         |                          |
   |--requestFetch()-------->|                          |
   |<----"QUEUED"------------|                          |
   |                         |                          |
   |                         |--spawn avatar---------->|
   |                         |                          |
   |                         |<--await result-----------|
   |                         |                          |
   |<--eval(handleResponse)-|                          |
   ✅ Response received!
```

## How It Works

1. **JavaScript calls** `window.requestFetch(id + '|' + url)`
2. **Kuyil callback** stores request in global variables (flags)
3. **Main loop** detects pending request and spawns avatar
4. **Avatar worker** performs work (fetch, calculation, etc.)
5. **Main loop** awaits avatar completion (non-blocking)
6. **Push response** via `eval(window, "window.handleKuyilResponse(...)")`
7. **JavaScript handler** receives response immediately

## Key Implementation Details

### Fixed Issues

1. **VM Stack Corruption**: Fixed by backing up/restoring stack in nested calls
2. **Interface Loading**: Implemented `vm_dlopen_only()` for new syntax
3. **Push Mechanism**: Use `eval()` instead of polling
4. **Array Operations**: Added `+` operator support for array concatenation

### Global State Variables

```kuyil
let window_global = nil          // WebView window reference
let pending_request_id = ""      // Request ID from JavaScript
let pending_request_url = ""     // URL to fetch
let pending_avatar_handle = nil  // Avatar handle
let pending_avatar_id = ""       // ID for current avatar
let response_id = ""             // Response ID (for fallback polling)
let response_data = ""           // Response data (for fallback polling)
```

### JavaScript Bridge API

```javascript
// Call Kuyil function
window.requestFetch(combined)  // Returns "QUEUED"

// Receive pushed responses
window.handleKuyilResponse = function(id, data) {
    console.log('Response:', id, data);
    // Handle response...
}
```

## Running the Demo

```bash
./kuyil examples/webview_bridge/webview_bridge.kyl
```

The demo will:
1. Open a webview window with test UI
2. Auto-trigger test requests every second
3. Show real-time responses
4. Demonstrate multiple sequential requests working correctly

## Testing

The demo includes:
- ✅ Auto-triggering test after 1 second
- ✅ Continuous requests (auto-triggers next after each response)
- ✅ Timeout fallback (10 seconds)
- ✅ Visual feedback for each request/response

## Files

- `webview_bridge.kyl` - Main working demo with inline HTML
- `README.md` - This file

## Code Quality

- **No VM corruption** - Stack properly managed in nested calls
- **Clean separation** - Callbacks only set flags, main loop does work
- **Non-blocking** - Avatar spawning and awaiting doesn't block UI
- **Reliable** - Successfully tested with multiple sequential requests

## Next Steps

For building your own webview apps, use the framework:
```kuyil
import "interfaces/interface_webview_app.kyl"

fn myHandler(args) {
    return "result"
}

fn main() {
    registerHandler("myHandler", myHandler)
    runWebViewApp("My App", htmlContent)
}
```

See `examples/webview_app_simple.kyl` for complete example.
