# Static File Serving in Kuyil

Kuyil HTTP server now includes comprehensive static file serving capabilities with security, performance optimizations, and extensive MIME type support.

## Features

### MIME Type Detection
Automatically detects and serves appropriate MIME types for 25+ file formats:

- **Web Files**: HTML, CSS, JavaScript, JSON, XML
- **Images**: PNG, JPEG, GIF, WebP, SVG, ICO
- **Documents**: PDF, TXT, CSV 
- **Fonts**: WOFF, WOFF2, TTF, OTF
- **Archives**: ZIP, TAR, GZ
- **Media**: MP4, MP3, WAV
- **Binary**: EXE, DLL, SO

### Security Features

- **Path Traversal Protection**: Blocks attempts to access files outside the static root using `../`, `//`, or absolute paths
- **Input Validation**: Sanitizes and validates all file paths and URLs
- **Safe Path Checking**: Prevents access to system files and directories
- **URL Decoding**: Safely decodes URL-encoded paths with validation

### Performance Optimizations

- **Multi-threaded Handling**: Each static file request is handled in a separate thread
- **Caching Headers**: Sends appropriate cache-control headers for better performance
- **Efficient File Streaming**: Streams large files in chunks to minimize memory usage
- **Content-Length Headers**: Proper HTTP headers for file size and type

## API Functions

### Core Static File Functions

```c
// Set the root directory for static files
void http_server_set_static_root(HttpServer* server, const char* root_path);

// Enable static file serving
void http_server_serve_static(HttpServer* server, bool enable);

// Serve a specific file (internal function)
void http_server_serve_file(HttpServer* server, int client_fd, const char* file_path);

// Get MIME type for a file extension
const char* http_get_mime_type(const char* filename);

// Check if a path is safe (no path traversal)
bool http_is_safe_path(const char* path);
```

### Integration with HTTP Server

The static file serving integrates seamlessly with the existing HTTP server:

1. **Automatic Routing**: Static files are served automatically when no specific route matches
2. **Route Priority**: Explicit routes take precedence over static file serving
3. **Default Files**: `/` requests automatically serve `index.html` if it exists
4. **Error Handling**: Returns appropriate 403/404/500 errors for file access issues

## Usage Example

### Basic Setup

```javascript
// Kuyil example (pseudocode - full C integration pending)
var server = http_server_init(8080);
http_server_set_static_root(server, "examples/static/");
http_server_serve_static(server, true);
http_server_start(server);
```

### File Structure

```
examples/static/
├── index.html      // Main page (served at / and /index.html)
├── about.html      // About page
├── style.css       // CSS stylesheet
├── app.js          // JavaScript application
└── logo.svg        // SVG logo
```

### Supported URLs

- `GET /` → `examples/static/index.html`
- `GET /index.html` → `examples/static/index.html`
- `GET /about.html` → `examples/static/about.html`
- `GET /style.css` → `examples/static/style.css` (MIME: text/css)
- `GET /app.js` → `examples/static/app.js` (MIME: application/javascript)
- `GET /logo.svg` → `examples/static/logo.svg` (MIME: image/svg+xml)

## Demo Files

The `examples/static/` directory contains a complete web application demonstration:

### `index.html`
- Responsive HTML5 page with modern design
- Interactive elements and JavaScript integration
- Demonstrates CSS and JavaScript loading
- Mobile-friendly responsive layout

### `style.css`
- Modern CSS with flexbox and grid layouts
- Smooth animations and transitions
- Responsive design with media queries
- Professional styling with gradients and shadows

### `app.js`
- Interactive JavaScript functionality
- Performance monitoring and metrics
- Dynamic content updates
- Event handling and DOM manipulation

### `about.html`
- Comprehensive feature documentation
- Technical details and examples
- Demonstrates multi-page navigation

### `logo.svg`
- Scalable vector graphics demonstration
- Clean, modern logo design
- Proper SVG structure and styling

## Security Considerations

### Path Traversal Prevention
```c
// These paths are BLOCKED:
"../../../etc/passwd"    // Directory traversal
"//dangerous/path"       // Protocol-relative paths
"/etc/passwd"           // Absolute system paths
"unsafe/../file.html"   // Mixed safe/unsafe paths

// These paths are ALLOWED:
"normal/path.html"      // Relative paths within static root
"subfolder/file.css"    // Subdirectory access
"image.png"             // Direct file access
```

### File Access Control
- Only files within the configured static root are accessible
- Hidden files (starting with `.`) are blocked
- System directories are blocked
- Executable files return appropriate MIME types but are served as downloads

## Testing

### Running the Demo

```bash
# Build Kuyil
make kuyil

# Test MIME type detection and security
./kuyil examples/static_demo.kyl

# View complete static server documentation
./kuyil examples/complete_static_server.kyl
```

### Expected Output

The demo shows:
- MIME type detection for various file extensions
- Security validation (safe vs. blocked paths)
- Server configuration details
- Available static files and their routes

## Implementation Details

### MIME Type Database
The MIME type detection uses a comprehensive lookup table with common web file types. Unknown file types default to `application/octet-stream`.

### Multi-threaded Architecture
Static file requests are handled using the `ClientHandlerArgs` structure with pthread-based threading for concurrent request processing.

### Error Handling
- **403 Forbidden**: Path traversal attempts or unsafe paths
- **404 Not Found**: File does not exist or cannot be accessed
- **500 Internal Server Error**: File system errors or server issues

### HTTP Headers
Static files are served with appropriate headers:
- `Content-Type`: Based on MIME type detection
- `Content-Length`: File size for proper download handling
- `Cache-Control`: Caching instructions for performance
- `Connection`: Keep-alive for HTTP/1.1 compatibility

## Future Enhancements

Planned improvements for the static file serving system:

1. **Compression**: Gzip compression for text files
2. **Range Requests**: Support for partial content (HTTP 206)
3. **ETag Support**: Entity tags for better caching
4. **Directory Listing**: Optional directory browsing
5. **File Upload**: Support for POST file uploads
6. **Rate Limiting**: Request rate limiting per client
7. **Access Logging**: Detailed request logging
8. **Configuration**: YAML-based static file configuration

## Integration Notes

The static file serving system integrates with all existing Kuyil features:

- **Logging Framework**: All file operations are logged with appropriate levels
- **Configuration System**: Can be configured via YAML files
- **Development Helper**: Supports file watching and auto-reload
- **FFI System**: Can be extended with custom file processing libraries
- **Error Handling**: Uses consistent error reporting throughout

This makes Kuyil a complete solution for both API services and static web content serving.