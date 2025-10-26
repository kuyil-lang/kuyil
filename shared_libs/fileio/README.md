# FileIO Shared Library for Kuyil

## Overview

The FileIO shared library provides comprehensive file reading capabilities for the Kuyil programming language, supporting multiple file formats with robust error handling and security features.

## Supported File Formats

### Text Files (.txt, .md, .log)
- Raw text content reading
- UTF-8 encoding support
- Binary-safe reading
- Line ending normalization

### CSV Files (.csv, .tsv)
- Header detection and parsing
- Comma, tab, and custom delimiter support
- Quoted field handling
- Automatic type detection
- Field validation

### JSON Files (.json, .jsonl)
- Complete JSON syntax validation
- Nested object and array support
- Error reporting with line/column information
- Memory-efficient parsing

### YAML Files (.yaml, .yml)
- Full YAML 1.2 specification support
- Configuration file parsing
- Multi-document support
- Schema validation

## Security Features

- **Path Validation**: Prevents directory traversal attacks
- **File Size Limits**: Configurable maximum file size (default: 10MB)
- **Extension Filtering**: Whitelist-based file type validation
- **Sandbox Mode**: Restricts access to relative paths only
- **Permission Checking**: Validates read permissions before access

## API Functions

### Core Reading Functions
```c
// Read complete text file
FileResult* file_read_text(const char* filepath);

// Parse CSV with headers and data rows
CSVResult* file_read_csv(const char* filepath);

// Parse and validate JSON
JSONResult* file_read_json(const char* filepath);

// Parse YAML configuration
FileResult* file_read_yaml(const char* filepath);
```

### Utility Functions
```c
// File system operations
int file_exists(const char* filepath);
size_t file_get_size(const char* filepath);
int file_is_readable(const char* filepath);
char* file_get_extension(const char* filepath);
int file_validate_path(const char* filepath);
```

### Kuyil VM Interface
```c
// Kuyil language bindings
KuyilValue kuyil_file_read_text(int arg_count, KuyilValue* args);
KuyilValue kuyil_file_read_csv(int arg_count, KuyilValue* args);
KuyilValue kuyil_file_read_json(int arg_count, KuyilValue* args);
KuyilValue kuyil_file_read_yaml(int arg_count, KuyilValue* args);
KuyilValue kuyil_file_exists(int arg_count, KuyilValue* args);
KuyilValue kuyil_file_size(int arg_count, KuyilValue* args);
```

## Usage in Kuyil

```kyl
// Read text file
let content = file_read_text("data/readme.txt")
print("Content: " + content)

// Parse CSV data
let csv_info = file_read_csv("data/employees.csv")
print("CSV info: " + csv_info)

// Load JSON configuration
let config = file_read_json("config/settings.json")
print("Config loaded: " + config)

// Parse YAML configuration
let yaml_config = file_read_yaml("config/app.yaml")
print("YAML config: " + yaml_config)

// Check file existence
let exists = file_exists("data/optional.txt")
if (exists) {
    print("File exists")
} else {
    print("File not found")
}

// Get file size
let size = file_size("data/large_file.dat")
print("File size: " + to_string(size) + " bytes")
```

## Error Handling

The library provides comprehensive error reporting with specific error codes:

- `FILE_SUCCESS` (0): Operation completed successfully
- `FILE_ERROR_NOT_FOUND` (1): File does not exist
- `FILE_ERROR_PERMISSION_DENIED` (2): Insufficient permissions
- `FILE_ERROR_TOO_LARGE` (3): File exceeds size limit
- `FILE_ERROR_INVALID_FORMAT` (4): File format validation failed
- `FILE_ERROR_MEMORY_ALLOCATION` (5): Memory allocation failed
- `FILE_ERROR_INVALID_PATH` (6): Path validation failed
- `FILE_ERROR_UNSUPPORTED_EXTENSION` (7): File type not supported
- `FILE_ERROR_PARSE_ERROR` (8): Content parsing failed

## Configuration

```c
// Configure file operations
FileConfig config = {
    .max_file_size = 10 * 1024 * 1024,  // 10MB limit
    .validate_paths = 1,                 // Enable path validation
    .sandbox_mode = 1,                   // Restrict to relative paths
    .allowed_extensions = {"txt", "csv", "json", "yaml"},
    .extension_count = 4
};

file_set_config(&config);
```

## Building

```bash
# Build the shared library
make

# Install to libs directory
make install

# Clean build artifacts
make clean
```

## Dependencies

- **libyaml**: For YAML parsing support
- **Standard C Library**: For file operations and string handling

## Performance

- **Streaming Support**: Large file processing with configurable buffer sizes
- **Memory Efficient**: Minimal memory footprint for large files
- **Concurrent Safe**: Thread-safe operations for multi-threaded environments
- **Caching**: Optional content caching for frequently accessed files

## Integration

The FileIO library integrates seamlessly with the Kuyil VM through the standard shared library interface. Functions are automatically registered and available in Kuyil scripts once the library is loaded.

## Thread Safety

All file operations are thread-safe and can be used concurrently from multiple Kuyil background tasks or green threads without synchronization concerns.