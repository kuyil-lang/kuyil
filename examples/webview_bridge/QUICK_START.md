# Quick Start - Working WebView Bridge

## Run the Demo

```bash
cd /home/vmukumar/akish/kuyil
./kuyil examples/webview_bridge/webview_bridge.kyl
```

## What You'll See

1. **WebView window opens** with a green "Fetch httpbin.org/json" button
2. **Auto-triggers** test request after 1 second
3. **Shows responses** in real-time
4. **Continues** making requests automatically

## Expected Output

```
=== Working Bridge Demo ===
🔧 Init...
📄 Load HTML...
⏳ Wait for load...
📝 Bind...
✅ JS bridge registered for 'requestFetch'
✅ JS bridge registered for 'pollResponse'
👁️ Show...
🚀 Event loop

[Bridge] requestFetch: req_0|https://httpbin.org/json
[Bridge] ID: req_0, URL: https://httpbin.org/json
[Main] Spawning avatar for: req_0
[Worker] Test with simple return
[Worker] About to return 'SUCCESS'
[Main] Avatar completed with result: SUCCESS
[Main] Pushing to JS: window.handleKuyilResponse('req_0', 'SUCCESS');
```

## Key Features Demonstrated

✅ **JavaScript → Kuyil** communication via `window.requestFetch()`
✅ **Kuyil → JavaScript** responses pushed via `eval()`
✅ **Avatar workers** running in background (non-blocking)
✅ **Multiple sequential requests** without corruption
✅ **Clean architecture** with flag-based state management

## How It Works

1. JavaScript calls `window.requestFetch(id + '|' + url)`
2. Kuyil callback sets global flags
3. Main loop spawns avatar worker
4. Avatar completes work
5. Main loop pushes response to JavaScript
6. JavaScript handler receives result instantly

## Troubleshooting

### "requestFetch not bound!"
- **Cause**: Import paths incorrect
- **Fix**: Use `../../interfaces/interface_*.kyl` for imports

### Window closes immediately
- **Cause**: Main loop exited too early
- **Fix**: Ensure `while is_open(window)` loop continues

### No responses
- **Cause**: Avatar not spawning or awaiting incorrectly
- **Fix**: Check avatar handle is not nil before awaiting

## Next Steps

To build your own webview app, use the framework:

```kuyil
import "../../interfaces/interface_webview_app.kyl"

fn myHandler(args) {
    // Your logic here
    return "result"
}

fn main() {
    registerHandler("myHandler", myHandler)
    
    let html = "<html>...</html>"
    runWebViewApp("My App", html)
}
```

See `../webview_app_simple.kyl` for complete example.
