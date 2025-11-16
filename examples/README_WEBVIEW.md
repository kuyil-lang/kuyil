# WebView App Examples

## Working Examples

### `webview_app_inline.kyl` ✅
Simple example with inline HTML. Works perfectly.
- HTML/CSS/JS all in one string
- Good for small apps or testing

## Dist Folder Support (TODO)

### `webview_app_from_dist_clean.kyl` ⚠️
Attempts to load from `webview_dist/` folder with separate files:
- `index.html` - Main HTML
- `style.css` - External CSS
- `app.js` - External JavaScript

**Current Issue**: `file_readText()` returns wrong type (not a proper string).
Once file I/O is fixed, this example will work and is the recommended pattern for larger apps.

### Dist Folder Structure
```
webview_dist/
├── index.html    - Main HTML (with <link> and <script> tags)
├── style.css     - Styles
└── app.js        - JavaScript code
```

## Framework Features

Both examples demonstrate:
- ✅ Simplified handler registration (just worker functions)
- ✅ Clean syntax with optional parentheses
- ✅ Increment operators (`i++`, `counter--`)
- ✅ Compound assignments (`value += 10`)
- ✅ Async avatar workers with automatic Promise handling
- ✅ Console log forwarding from JavaScript to Kuyil terminal
