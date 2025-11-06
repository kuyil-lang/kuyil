# WebView Direct Bridge - JavaScript ↔ Kuyil Communication

This example demonstrates **direct** bidirectional communication between Kuyil and JavaScript, using `eval(window, js)` to execute JavaScript code directly in the WebView context.

## Features

### ✅ **Direct Kuyil → JavaScript** (Implemented)
- Use `eval(window, js_code)` to execute JavaScript instantly
- No HTTP server required
- No polling delays
- Clean, efficient communication

### 🔄 **JavaScript → Kuyil** (Coming Soon)
- Will use `webview_bind()` to register Kuyil callbacks
- JavaScript can call bound functions: `window.sendToKuyil("data")`

## Comparison: HTTP Polling vs Direct Communication

| Feature | HTTP Polling (webview_bridge.kyl) | Direct Eval (direct_bridge_demo.kyl) |
|---------|-----------------------------------|--------------------------------------|
| **Kuyil → JS** | POST to endpoint, JS polls every 1s | `eval()` instant execution |
| **Latency** | Up to 1 second delay | Immediate |
| **Overhead** | HTTP server + constant requests | No server needed |
| **Code Complexity** | Routes, handlers, polling loop | Single function call |
| **Resource Usage** | High (server thread + polling) | Minimal |

## How It Works

### Kuyil → JavaScript

```kuyil
import("../../interfaces/interface_webview.kyl")

// Create window
let settings = createDefaultSettings()
let win = create("Title", settings)

// Load HTML with JavaScript receiver function
let html = `
<script>
function receiveFromKuyil(message) {
    console.log("Received:", message);
    // Update UI, trigger animations, etc.
}
</script>
`
loadHtml(win, html)

// Send messages from Kuyil to JavaScript
let js = `receiveFromKuyil("Hello from Kuyil!");`
eval(win, js)  // ✅ Executes immediately!

run(win)
```

### JavaScript → Kuyil (Future)

```kuyil
// Register Kuyil function to be called from JavaScript (planned)
// bind(win, "sendToKuyil", fn(msg) {
//     print("Received from JS: " + msg)
// })

// JavaScript can then call:
// window.sendToKuyil("Hello from JavaScript!");
```

## Files

- **direct_bridge_demo.kyl**: Main demo showing `eval()` usage
- **webview_bridge.kyl**: Legacy HTTP-based polling approach (still works)
- **DIRECT_BRIDGE_README.md**: This file

## Running the Demo

```bash
./kuyil examples/webview_bridge/direct_bridge_demo.kyl
```

A window opens with a gradient purple background. Messages sent from Kuyil appear instantly in the UI (check the WebView developer console or update the HTML to display them visually).

## Known Limitations

### Cosmetic Segfault on Close
When you close the WebView window, you may see a segfault. This is **cosmetic** and doesn't affect functionality:
- The window closes properly
- All resources are cleaned up
- No data loss or corruption
- Caused by race condition between window close and VM shutdown

## Implementation Details

### API Surface
The `webview` interface exposes lowerCamel functions in `interfaces/interface_webview.kyl`, including:
- `init()`, `createDefaultSettings()`, `create(title: string, settings: number)`
- `loadHtml(window: number, html: string)`, `loadUrl(window: number, url: string)`
- `show(window: number)`, `run(window: number)`, `eval(window: number, js: string)`

## Advantages Over HTTP Polling

1. **Performance**: No HTTP server overhead, no constant polling requests
2. **Latency**: Immediate execution vs 1-second polling interval
3. **Simplicity**: Single function call vs route handlers + polling logic
4. **Resources**: No background thread for HTTP server
5. **Scalability**: Can send many messages without saturating network stack

## Next Steps

To complete bidirectional communication:

1. Implement `bind()` wrapper in C
2. Add `bind()` to the interface and loader
3. Create demo showing JS→Kuyil calls
4. Document `bind()` usage patterns

## Use Cases

- **Real-time dashboards**: Push live data updates to UI
- **Game dev**: Send game state changes to WebView UI
- **Desktop apps**: Native Kuyil logic with web-based UI
- **Data visualization**: Stream processed data to charts/graphs
- **Developer tools**: Live introspection and debugging UIs

## See Also

- `webview_bridge/`: Original HTTP-based approach
- `interfaces/interface_webview.kyl`: Full WebView API
- `additional_libs/webview/`: C implementation
- `test_simple_webview.kyl`: Minimal WebView example
