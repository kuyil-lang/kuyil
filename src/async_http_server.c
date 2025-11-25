// Async HTTP Server Implementation with Route Integration

#define _GNU_SOURCE  // For strcasestr
#include "async_http_server.h"
#include "route_decorator.h"
#include "request_response.h"
#include "vm_call_shared.h"
#include "avatar_runtime.h"
#include "thread_pool.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_CONNECTIONS 100
#define BUFFER_SIZE 8192
#define MAX_BODY_SIZE (10 * 1024 * 1024)  // 10MB max body size
#define MAX_STATIC_MOUNTS 10

typedef struct StaticMount {
    char* url_prefix;      // URL prefix (e.g., "/")
    char* directory;       // File system directory (e.g., "kuyil-ide/assets")
    bool active;
} StaticMount;

struct AsyncHttpServer {
    int port;
    int server_fd;
    bool running;
    pthread_t thread;
    VM* vm;
    pthread_mutex_t vm_lock;
    size_t max_body_size;  // Maximum request body size (default: 10MB)
    StaticMount static_mounts[MAX_STATIC_MOUNTS];
    int static_mount_count;
    ThreadPool* io_pool;  // Thread pool for async I/O (file reads, body reads)
};

// Global server instance
static AsyncHttpServer* g_server = NULL;

// Context for async client handling
typedef struct {
    AsyncHttpServer* server;
    int client_fd;
} ClientContext;

// Forward declarations - must be before any functions
static HttpResponse* http_response_create(int status_code, const char* body);
static char* get_multipart_boundary(const char* headers);
static MultipartFormData* parse_multipart_form_data(const char* body, const char* boundary);
static void multipart_form_data_free(MultipartFormData* form);

// Response builder with reference counting (must match kyl_response_functions.c)
typedef struct KylResponseBuilder {
    int status_code;
    char* body;
    size_t body_length;
    char** header_names;
    char** header_values;
    int header_count;
    char* content_type;
    int ref_count;
} KylResponseBuilder;

// Reference counting functions
static void response_builder_retain(KylResponseBuilder* builder) {
    if (builder) {
        builder->ref_count++;
        printf("[ResponseBuilder] Retained, ref_count=%d\n", builder->ref_count);
    }
}

static void response_builder_release(KylResponseBuilder* builder) {
    if (!builder) return;
    
    builder->ref_count--;
    printf("[ResponseBuilder] Released, ref_count=%d\n", builder->ref_count);
    
    if (builder->ref_count <= 0) {
        printf("[ResponseBuilder] Freeing builder (ref_count=%d)\n", builder->ref_count);
        
        // Free all allocated memory
        if (builder->body) free(builder->body);
        
        if (builder->header_names) {
            for (int i = 0; i < builder->header_count; i++) {
                if (builder->header_names[i]) free(builder->header_names[i]);
                if (builder->header_values[i]) free(builder->header_values[i]);
            }
            free(builder->header_names);
            free(builder->header_values);
        }
        
        if (builder->content_type) free(builder->content_type);
        
        free(builder);
    }
}

// Get MIME type from file extension
static const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    
    ext++; // Skip the dot
    
    // Common web types
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, "css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, "js") == 0) return "application/javascript";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "xml") == 0) return "application/xml";
    
    // Images
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, "ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, "webp") == 0) return "image/webp";
    
    // Fonts
    if (strcasecmp(ext, "woff") == 0) return "font/woff";
    if (strcasecmp(ext, "woff2") == 0) return "font/woff2";
    if (strcasecmp(ext, "ttf") == 0) return "font/ttf";
    if (strcasecmp(ext, "otf") == 0) return "font/otf";
    
    // Other common types
    if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
    if (strcasecmp(ext, "txt") == 0) return "text/plain";
    if (strcasecmp(ext, "md") == 0) return "text/markdown";
    if (strcasecmp(ext, "zip") == 0) return "application/zip";
    
    return "application/octet-stream";
}

// Serve static file from disk
static HttpResponse* serve_static_file(const char* file_path) {
    FILE* f = fopen(file_path, "rb");
    if (!f) {
        return NULL;  // File not found
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Check file size limit (10MB)
    if (file_size > 10 * 1024 * 1024) {
        fclose(f);
        return http_response_create(413, "File too large");
    }
    
    // Read file content
    char* content = malloc(file_size + 1);
    if (!content) {
        fclose(f);
        return http_response_create(500, "Memory allocation failed");
    }
    
    size_t bytes_read = fread(content, 1, file_size, f);
    fclose(f);
    content[bytes_read] = '\0';
    
    // Create response with MIME type
    HttpResponse* response = malloc(sizeof(HttpResponse));
    response->status_code = 200;
    response->body = content;
    
    // Add Content-Type header
    const char* mime_type = get_mime_type(file_path);
    response->header_count = 1;
    response->header_keys = malloc(sizeof(char*));
    response->header_values = malloc(sizeof(char*));
    response->header_keys[0] = strdup("Content-Type");
    response->header_values[0] = strdup(mime_type);
    
    printf("[HTTP] Served static file: %s (%ld bytes, %s)\n", file_path, file_size, mime_type);
    
    return response;
}

// Try to serve static file from configured mounts
static HttpResponse* try_serve_static(AsyncHttpServer* server, const char* path) {
    if (!path || path[0] != '/') return NULL;
    
    // Try each static mount in order
    for (int i = 0; i < server->static_mount_count; i++) {
        StaticMount* mount = &server->static_mounts[i];
        if (!mount->active) continue;
        
        // Check if path starts with mount prefix
        size_t prefix_len = strlen(mount->url_prefix);
        if (strncmp(path, mount->url_prefix, prefix_len) == 0) {
            // Build file path
            const char* relative_path = path + prefix_len;
            
            // Remove leading slash from relative path
            while (*relative_path == '/') relative_path++;
            
            // If no file specified, try index.html
            char file_path[1024];
            if (*relative_path == '\0') {
                snprintf(file_path, sizeof(file_path), "%s/index.html", mount->directory);
            } else {
                snprintf(file_path, sizeof(file_path), "%s/%s", mount->directory, relative_path);
            }
            
            // Try to serve the file
            HttpResponse* response = serve_static_file(file_path);
            if (response) {
                return response;
            }
            
            // If it was a directory request without trailing slash, try with /index.html
            if (*relative_path != '\0' && path[strlen(path) - 1] != '/') {
                snprintf(file_path, sizeof(file_path), "%s/%s/index.html", mount->directory, relative_path);
                response = serve_static_file(file_path);
                if (response) {
                    return response;
                }
            }
        }
    }
    
    return NULL;  // No static file found
}

// Extract Content-Length from headers
static long get_content_length(const char* headers) {
    const char* cl_header = strcasestr(headers, "Content-Length:");
    if (!cl_header) return -1;
    
    long length = 0;
    if (sscanf(cl_header + 15, "%ld", &length) == 1) {
        return length;
    }
    return -1;
}

// Check if Transfer-Encoding: chunked
static bool is_chunked_encoding(const char* headers) {
    const char* te_header = strcasestr(headers, "Transfer-Encoding:");
    if (!te_header) return false;
    
    return strcasestr(te_header, "chunked") != NULL;
}

// Read request body from socket with streaming support
static char* read_request_body(int client_fd, const char* headers, size_t already_read, const char* initial_body, size_t max_body_size) {
    long content_length = get_content_length(headers);
    bool chunked = is_chunked_encoding(headers);
    
    // If no body expected
    if (content_length == 0) {
        return strdup("");
    }
    
    // Handle chunked encoding
    if (chunked) {
        // Simplified chunked reading - accumulate chunks
        size_t total_size = 0;
        size_t capacity = BUFFER_SIZE;
        char* body = malloc(capacity);
        if (!body) return strdup("");
        
        size_t offset = 0;
        if (already_read > 0 && initial_body) {
            size_t copy_len = (already_read < capacity) ? already_read : capacity - 1;
            memcpy(body, initial_body, copy_len);
            offset = copy_len;
        }
        
        char buffer[BUFFER_SIZE];
        while (true) {
            ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) break;
            
            // Expand buffer if needed
            if (offset + bytes >= capacity) {
                capacity *= 2;
                if (capacity > max_body_size) {
                    free(body);
                    printf("[HTTP] Body too large (chunked): %zu > %zu bytes\n", capacity, max_body_size);
                    return strdup("");
                }
                char* new_body = realloc(body, capacity);
                if (!new_body) {
                    free(body);
                    return strdup("");
                }
                body = new_body;
            }
            
            memcpy(body + offset, buffer, bytes);
            offset += bytes;
            
            // Check for end of chunks (0\r\n\r\n)
            if (offset >= 5 && memcmp(body + offset - 5, "0\r\n\r\n", 5) == 0) {
                break;
            }
        }
        
        body[offset] = '\0';
        return body;
    }
    
    // Handle Content-Length
    if (content_length > 0) {
        if ((size_t)content_length > max_body_size) {
            printf("[HTTP] Body too large: %ld > %zu bytes\n", content_length, max_body_size);
            return strdup("");  // Body too large
        }
        
        char* body = malloc(content_length + 1);
        if (!body) return strdup("");
        
        size_t offset = 0;
        
        // Copy any already-read body data
        if (already_read > 0 && initial_body) {
            size_t copy_len = (already_read < (size_t)content_length) ? already_read : (size_t)content_length;
            memcpy(body, initial_body, copy_len);
            offset = copy_len;
        }
        
        // Read remaining body
        while (offset < (size_t)content_length) {
            ssize_t bytes = read(client_fd, body + offset, content_length - offset);
            if (bytes <= 0) break;
            offset += bytes;
        }
        
        body[offset] = '\0';
        return body;
    }
    
    // No Content-Length, no chunked - return what we have
    if (already_read > 0 && initial_body) {
        return strndup(initial_body, already_read);
    }
    
    return strdup("");
}

// Parse HTTP request (headers only, body read separately)
static HttpRequest* parse_http_request(const char* raw_request, int client_fd, size_t max_body_size) {
    HttpRequest* req = calloc(1, sizeof(HttpRequest));
    if (!req) return NULL;
    
    // Parse request line: GET /path?query HTTP/1.1
    char method[16], path[2048], version[16];
    if (sscanf(raw_request, "%15s %2047s %15s", method, path, version) != 3) {
        free(req);
        return NULL;
    }
    
    req->method = strdup(method);
    
    // Split path and query string
    char* query_start = strchr(path, '?');
    if (query_start) {
        *query_start = '\0';
        req->query_string = strdup(query_start + 1);
        req->path = strdup(path);
        *query_start = '?';  // Restore
    } else {
        req->path = strdup(path);
        req->query_string = strdup("");
    }
    
    // Parse headers
    req->header_count = 0;
    req->header_keys = NULL;
    req->header_values = NULL;
    
    // Find body start (after \r\n\r\n)
    const char* body_start = strstr(raw_request, "\r\n\r\n");
    const char* headers_start = strchr(raw_request, '\n');  // Skip request line
    
    if (headers_start && body_start && body_start > headers_start) {
        headers_start++;  // Move past the newline
        
        // Count headers first
        const char* line_ptr = headers_start;
        int header_capacity = 16;
        req->header_keys = malloc(sizeof(char*) * header_capacity);
        req->header_values = malloc(sizeof(char*) * header_capacity);
        
        while (line_ptr < body_start) {
            // Skip empty lines
            if (*line_ptr == '\r' || *line_ptr == '\n') {
                line_ptr++;
                continue;
            }
            
            // Find colon separator
            const char* colon = strchr(line_ptr, ':');
            if (!colon || colon >= body_start) break;
            
            // Find line end
            const char* line_end = strstr(line_ptr, "\r\n");
            if (!line_end || line_end >= body_start) break;
            
            // Extract key
            size_t key_len = colon - line_ptr;
            char* key = strndup(line_ptr, key_len);
            
            // Extract value (skip spaces after colon)
            const char* value_start = colon + 1;
            while (*value_start == ' ' || *value_start == '\t') value_start++;
            size_t value_len = line_end - value_start;
            char* value = strndup(value_start, value_len);
            
            // Expand arrays if needed
            if (req->header_count >= header_capacity) {
                header_capacity *= 2;
                req->header_keys = realloc(req->header_keys, sizeof(char*) * header_capacity);
                req->header_values = realloc(req->header_values, sizeof(char*) * header_capacity);
            }
            
            // Store header
            req->header_keys[req->header_count] = key;
            req->header_values[req->header_count] = value;
            req->header_count++;
            
            // Move to next line
            line_ptr = line_end + 2;
        }
    }
    
    size_t already_read = 0;
    const char* initial_body = NULL;
    
    if (body_start) {
        body_start += 4;
        initial_body = body_start;
        already_read = strlen(body_start);
    }
    
    // Read body with streaming support
    req->body = read_request_body(client_fd, raw_request, already_read, initial_body, max_body_size);
    req->multipart = NULL;  // Will be parsed if needed
    
    // Parse multipart if Content-Type indicates it
    char* boundary = get_multipart_boundary(raw_request);
    if (boundary && req->body) {
        req->multipart = parse_multipart_form_data(req->body, boundary);
        free(boundary);
        if (req->multipart) {
            printf("[HTTP] Parsed %d multipart fields\n", req->multipart->field_count);
        }
    }
    
    return req;
}

// Extract boundary from Content-Type header
static char* get_multipart_boundary(const char* headers) {
    const char* ct_header = strcasestr(headers, "Content-Type:");
    if (!ct_header) return NULL;
    
    const char* boundary_start = strcasestr(ct_header, "boundary=");
    if (!boundary_start) return NULL;
    
    boundary_start += 9;  // Skip "boundary="
    
    // Handle quoted boundary
    if (*boundary_start == '"') {
        boundary_start++;
        const char* end = strchr(boundary_start, '"');
        if (!end) return NULL;
        return strndup(boundary_start, end - boundary_start);
    }
    
    // Unquoted boundary (until \r\n or ;)
    const char* end = boundary_start;
    while (*end && *end != '\r' && *end != '\n' && *end != ';') {
        end++;
    }
    return strndup(boundary_start, end - boundary_start);
}

// Parse multipart form data
static MultipartFormData* parse_multipart_form_data(const char* body, const char* boundary) {
    if (!body || !boundary) return NULL;
    
    MultipartFormData* form = calloc(1, sizeof(MultipartFormData));
    if (!form) return NULL;
    
    form->fields = NULL;
    form->field_count = 0;
    
    // Build delimiter strings
    char delimiter[256];
    char end_delimiter[256];
    snprintf(delimiter, sizeof(delimiter), "--%s\r\n", boundary);
    snprintf(end_delimiter, sizeof(end_delimiter), "--%s--", boundary);
    
    const char* pos = body;
    int capacity = 4;
    form->fields = malloc(sizeof(MultipartField) * capacity);
    if (!form->fields) {
        free(form);
        return NULL;
    }
    
    // Find first delimiter
    pos = strstr(pos, delimiter);
    if (!pos) {
        free(form->fields);
        free(form);
        return NULL;
    }
    pos += strlen(delimiter);
    
    while (pos && *pos) {
        // Check for end delimiter
        if (strncmp(pos, end_delimiter, strlen(end_delimiter)) == 0) {
            break;
        }
        
        // Expand array if needed
        if (form->field_count >= capacity) {
            capacity *= 2;
            MultipartField* new_fields = realloc(form->fields, sizeof(MultipartField) * capacity);
            if (!new_fields) break;
            form->fields = new_fields;
        }
        
        MultipartField* field = &form->fields[form->field_count];
        memset(field, 0, sizeof(MultipartField));
        
        // Parse headers until \r\n\r\n
        const char* headers_end = strstr(pos, "\r\n\r\n");
        if (!headers_end) break;
        
        // Extract name from Content-Disposition header
        const char* cd_header = strcasestr(pos, "Content-Disposition:");
        if (cd_header && cd_header < headers_end) {
            const char* name_start = strcasestr(cd_header, "name=\"");
            if (name_start && name_start < headers_end) {
                name_start += 6;
                const char* name_end = strchr(name_start, '"');
                if (name_end && name_end < headers_end) {
                    field->name = strndup(name_start, name_end - name_start);
                }
            }
            
            // Extract filename if present
            const char* filename_start = strcasestr(cd_header, "filename=\"");
            if (filename_start && filename_start < headers_end) {
                filename_start += 10;
                const char* filename_end = strchr(filename_start, '"');
                if (filename_end && filename_end < headers_end) {
                    field->filename = strndup(filename_start, filename_end - filename_start);
                }
            }
        }
        
        // Extract Content-Type if present
        const char* ct_header = strcasestr(pos, "Content-Type:");
        if (ct_header && ct_header < headers_end) {
            ct_header += 13;
            while (*ct_header == ' ') ct_header++;
            const char* ct_end = ct_header;
            while (*ct_end && *ct_end != '\r' && *ct_end != '\n') ct_end++;
            field->content_type = strndup(ct_header, ct_end - ct_header);
        }
        
        // Find data start (after \r\n\r\n)
        const char* data_start = headers_end + 4;
        
        // Find next delimiter or end delimiter
        const char* next_delim = strstr(data_start, delimiter);
        const char* end_delim = strstr(data_start, end_delimiter);
        const char* data_end = NULL;
        
        if (next_delim && (!end_delim || next_delim < end_delim)) {
            data_end = next_delim - 2;  // Remove trailing \r\n
            pos = next_delim + strlen(delimiter);
        } else if (end_delim) {
            data_end = end_delim - 2;  // Remove trailing \r\n
            pos = NULL;  // Stop after this field
        } else {
            break;  // No delimiter found
        }
        
        // Copy field data
        if (data_end > data_start) {
            field->data_length = data_end - data_start;
            field->data = malloc(field->data_length + 1);
            if (field->data) {
                memcpy(field->data, data_start, field->data_length);
                field->data[field->data_length] = '\0';
            }
        } else {
            field->data = strdup("");
            field->data_length = 0;
        }
        
        form->field_count++;
    }
    
    return form;
}

// Free multipart form data
static void multipart_form_data_free(MultipartFormData* form) {
    if (!form) return;
    
    for (int i = 0; i < form->field_count; i++) {
        MultipartField* field = &form->fields[i];
        free(field->name);
        free(field->filename);
        free(field->content_type);
        free(field->data);
    }
    free(form->fields);
    free(form);
}

// Free HTTP request
static void http_request_free(HttpRequest* req) {
    if (!req) return;
    free(req->method);
    free(req->path);
    free(req->query_string);
    free(req->body);
    for (int i = 0; i < req->header_count; i++) {
        free(req->header_keys[i]);
        free(req->header_values[i]);
    }
    free(req->header_keys);
    free(req->header_values);
    multipart_form_data_free(req->multipart);
    free(req);
}

// Create HTTP response
static HttpResponse* http_response_create(int status_code, const char* body) {
    HttpResponse* res = calloc(1, sizeof(HttpResponse));
    if (!res) return NULL;
    
    res->status_code = status_code;
    res->body = body ? strdup(body) : strdup("");
    res->header_count = 0;
    res->header_keys = NULL;
    res->header_values = NULL;
    
    return res;
}

// Free HTTP response
static void http_response_free(HttpResponse* res) {
    if (!res) return;
    free(res->body);
    for (int i = 0; i < res->header_count; i++) {
        free(res->header_keys[i]);
        free(res->header_values[i]);
    }
    free(res->header_keys);
    free(res->header_values);
    free(res);
}

// Send HTTP response to client
static void send_http_response(int client_fd, HttpResponse* res) {
    const char* status_text = (res->status_code == 200) ? "OK" :
                              (res->status_code == 201) ? "Created" :
                              (res->status_code == 204) ? "No Content" :
                              (res->status_code == 302) ? "Found" :
                              (res->status_code == 400) ? "Bad Request" :
                              (res->status_code == 401) ? "Unauthorized" :
                              (res->status_code == 403) ? "Forbidden" :
                              (res->status_code == 404) ? "Not Found" :
                              (res->status_code == 413) ? "Payload Too Large" :
                              (res->status_code == 500) ? "Internal Server Error" :
                              (res->status_code == 503) ? "Service Unavailable" :
                              (res->status_code == 504) ? "Gateway Timeout" : "Unknown";
    
    size_t body_len = res->body ? strlen(res->body) : 0;
    bool use_chunked = body_len > 8192;  // Use chunked for large responses
    
    // Build response headers
    char header_buffer[4096];
    int offset = snprintf(header_buffer, sizeof(header_buffer),
        "HTTP/1.1 %d %s\r\n",
        res->status_code, status_text);
    
    // Add custom headers from response first
    bool has_content_type = false;
    for (int i = 0; i < res->header_count; i++) {
        if (strcasecmp(res->header_keys[i], "Content-Type") == 0) {
            has_content_type = true;
        }
        offset += snprintf(header_buffer + offset, sizeof(header_buffer) - offset,
            "%s: %s\r\n", res->header_keys[i], res->header_values[i]);
    }
    
    // Add default Content-Type if not set
    if (!has_content_type) {
        offset += snprintf(header_buffer + offset, sizeof(header_buffer) - offset,
            "Content-Type: application/json\r\n");
    }
    
    // Add Content-Length or Transfer-Encoding
    if (use_chunked) {
        offset += snprintf(header_buffer + offset, sizeof(header_buffer) - offset,
            "Transfer-Encoding: chunked\r\n");
    } else {
        offset += snprintf(header_buffer + offset, sizeof(header_buffer) - offset,
            "Content-Length: %zu\r\n", body_len);
    }
    
    // Add Connection: close
    offset += snprintf(header_buffer + offset, sizeof(header_buffer) - offset,
        "Connection: close\r\n\r\n");
    
    // Send headers
    ssize_t wr = write(client_fd, header_buffer, offset);
    (void)wr;
    
    // Send body
    if (body_len > 0 && res->body) {
        if (use_chunked) {
            // Send in chunks
            const char* data = res->body;
            size_t remaining = body_len;
            const size_t chunk_size = 8192;
            
            while (remaining > 0) {
                size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
                
                // Send chunk size in hex
                char chunk_header[32];
                snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", to_send);
                wr = write(client_fd, chunk_header, strlen(chunk_header));
                (void)wr;
                
                // Send chunk data
                wr = write(client_fd, data, to_send);
                (void)wr;
                
                // Send trailing CRLF
                wr = write(client_fd, "\r\n", 2);
                (void)wr;
                
                data += to_send;
                remaining -= to_send;
            }
            
            // Send final chunk (zero-length)
            wr = write(client_fd, "0\r\n\r\n", 5);
            (void)wr;
        } else {
            // Send entire body at once
            wr = write(client_fd, res->body, body_len);
            (void)wr;
        }
    }
}

// Forward declaration
extern RouteRegistry* get_global_registry();

// Handle HTTP request with route system
static HttpResponse* handle_request(AsyncHttpServer* server, HttpRequest* req) {
    // Try to serve static file first (for GET requests only)
    if (strcmp(req->method, "GET") == 0) {
        HttpResponse* static_response = try_serve_static(server, req->path);
        if (static_response) {
            return static_response;
        }
    }
    
    RouteRegistry* registry = get_global_registry();
    
    // Find matching route
    RouteEntry* route = route_find(registry, req->method, req->path);
    
    if (!route) {
        return http_response_create(404, "Not Found");
    }
    
    printf("[HTTP] Route matched: %s %s -> %s\n", req->method, req->path, route->handler_name);
    
    // Parse multipart form data if present
    // Note: headers are in raw_request, we need to pass them to parse_http_request
    // For now, check if body starts with multipart boundary pattern
    if (req->body && strstr(req->body, "Content-Disposition:")) {
        // This is likely multipart data - would need boundary from headers
        // Full implementation requires storing headers in HttpRequest
        printf("[HTTP] Multipart form data detected\n");
    }
    
    // Extract path parameters
    RouteParams* params = route_extract_params(route->path, req->path);
    
    // Create Kuyil Request object
    Value params_obj;
    memset(&params_obj, 0, sizeof(Value));
    params_obj.type = VALUE_OBJECT;
    
    if (params && params->count > 0) {
        params_obj.as.object.count = params->count;
        params_obj.as.object.keys = malloc(sizeof(char*) * params->count);
        params_obj.as.object.values = malloc(sizeof(Value) * params->count);
        
        for (int i = 0; i < params->count; i++) {
            params_obj.as.object.keys[i] = strdup(params->keys[i]);
            params_obj.as.object.values[i].type = VALUE_STRING;
            params_obj.as.object.values[i].as.string = strdup(params->values[i]);
        }
    } else {
        params_obj.as.object.count = 0;
        params_obj.as.object.keys = NULL;
        params_obj.as.object.values = NULL;
    }
    
    // Parse query string into object
    Value query_obj;
    memset(&query_obj, 0, sizeof(Value));
    query_obj.type = VALUE_OBJECT;
    
    if (req->query_string && strlen(req->query_string) > 0) {
        // Parse query string: key1=value1&key2=value2
        const char* qs = req->query_string;
        
        // Count parameters
        int param_count = 1;
        for (const char* p = qs; *p; p++) {
            if (*p == '&') param_count++;
        }
        
        query_obj.as.object.count = param_count;
        query_obj.as.object.keys = malloc(sizeof(char*) * param_count);
        query_obj.as.object.values = malloc(sizeof(Value) * param_count);
        
        // Parse key=value pairs
        char* qs_copy = strdup(qs);
        char* saveptr = NULL;
        char* token = strtok_r(qs_copy, "&", &saveptr);
        int idx = 0;
        
        while (token && idx < param_count) {
            char* eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                query_obj.as.object.keys[idx] = strdup(token);
                
                Value val;
                memset(&val, 0, sizeof(Value));
                val.type = VALUE_STRING;
                val.as.string = strdup(eq + 1);
                query_obj.as.object.values[idx] = val;
                idx++;
            }
            token = strtok_r(NULL, "&", &saveptr);
        }
        
        query_obj.as.object.count = idx;
        free(qs_copy);
    } else {
        query_obj.as.object.count = 0;
        query_obj.as.object.keys = NULL;
        query_obj.as.object.values = NULL;
    }
    
    // Create headers object from parsed HTTP headers
    Value headers_obj;
    memset(&headers_obj, 0, sizeof(Value));
    headers_obj.type = VALUE_OBJECT;
    headers_obj.as.object.count = req->header_count;
    
    if (req->header_count > 0) {
        headers_obj.as.object.keys = malloc(sizeof(char*) * req->header_count);
        headers_obj.as.object.values = malloc(sizeof(Value) * req->header_count);
        
        for (int i = 0; i < req->header_count; i++) {
            // Copy header key
            headers_obj.as.object.keys[i] = strdup(req->header_keys[i]);
            
            // Create string value for header
            Value header_val;
            memset(&header_val, 0, sizeof(Value));
            header_val.type = VALUE_STRING;
            header_val.as.string = strdup(req->header_values[i]);
            headers_obj.as.object.values[i] = header_val;
        }
    } else {
        headers_obj.as.object.keys = NULL;
        headers_obj.as.object.values = NULL;
    }
    
    // Create body value
    Value body_val;
    memset(&body_val, 0, sizeof(Value));
    body_val.type = VALUE_STRING;
    body_val.as.string = strdup(req->body);
    
    // Create Request object
    Value request_obj = create_request_object(
        req->method,
        req->path,
        params_obj,
        query_obj,
        headers_obj,
        body_val
    );
    
    // Call handler function
    pthread_mutex_lock(&server->vm_lock);
    
    Value handler_fn;
    memset(&handler_fn, 0, sizeof(Value));
    bool found = false;
    
    // Look up handler function in VM globals
    if (server->vm) {
        for (int i = 0; i < server->vm->global_count; i++) {
            if (strcmp(server->vm->globals[i].name, route->handler_name) == 0) {
                handler_fn = server->vm->globals[i].value;
                if (handler_fn.type == VALUE_FUNCTION) {
                    found = true;
                }
                break;
            }
        }
    }
    
    HttpResponse* http_res;
    
    printf("[HTTP] Handler lookup: found=%d, type=%d\n", found, handler_fn.type);
    
    if (found && handler_fn.type == VALUE_FUNCTION) {
        printf("[HTTP] Calling handler via avatar: %s\n", route->handler_name);
        fflush(stdout);
        
        // Submit handler to avatar runtime for execution
        if (!server->vm->avatar_runtime) {
            printf("[HTTP] ERROR: Avatar runtime not available\n");
            pthread_mutex_unlock(&server->vm_lock);
            
            http_res = malloc(sizeof(HttpResponse));
            http_res->status_code = 500;
            http_res->body = strdup("{\"error\":\"Avatar runtime not initialized\"}");
            http_res->header_keys = NULL;
            http_res->header_values = NULL;
            http_res->header_count = 0;
            return http_res;
        }
        
        // Create response builder and register it
        KylResponseBuilder* response_builder = calloc(1, sizeof(KylResponseBuilder));
        response_builder->status_code = 200;  // Default status
        response_builder->ref_count = 1;  // Initial reference
        
        printf("[HTTP] Created response builder, ref_count=%d\n", response_builder->ref_count);
        
        // Register response builder in kyl_aio registry
        extern int kyl_aio_register_ptr(void*);
        int response_id = kyl_aio_register_ptr(response_builder);
        
        // Create response object with __id
        // NOTE: We cannot use vm_object_create_with_field here because avatar_runtime_submit
        // does a shallow copy (memcpy), which means both the original and the copy will have
        // the same pointers to keys/values arrays. When avatar cleans up, it will try to free
        // them, causing a double-free.
        // Instead, we manually create the object with heap-allocated arrays that we control.
        Value response_obj_with_id;
        response_obj_with_id.type = VALUE_OBJECT;
        response_obj_with_id.as.object.count = 1;
        
        // Allocate arrays that will be copied by avatar_runtime_submit
        // We'll free these after the avatar completes
        char** keys = malloc(sizeof(char*));
        Value* values = malloc(sizeof(Value));
        keys[0] = strdup("__id");
        values[0].type = VALUE_NUMBER;
        values[0].as.number = (double)response_id;
        
        response_obj_with_id.as.object.keys = keys;
        response_obj_with_id.as.object.values = values;
        
        // Prepare arguments for avatar: request and response objects
        Value* avatar_args = malloc(sizeof(Value) * 2);
        avatar_args[0] = request_obj;
        avatar_args[1] = response_obj_with_id;
        
        // Retain the response builder for the avatar
        // Now ref_count=2: one for server, one for avatar
        response_builder_retain(response_builder);
        printf("[HTTP] Retained builder for avatar, ref_count=%d\n", response_builder->ref_count);
        
        // Submit to avatar runtime
        AvatarHandle* handle = avatar_runtime_submit(
            server->vm->avatar_runtime,
            handler_fn.as.function.function,
            avatar_args,
            2,  // 2 arguments (request, response)
            server->vm,
            NULL,  // No completion callback
            NULL
        );
        
        if (!handle) {
            printf("[HTTP] ERROR: Failed to submit avatar\n");
            free(avatar_args);
            pthread_mutex_unlock(&server->vm_lock);
            
            http_res = malloc(sizeof(HttpResponse));
            http_res->status_code = 500;
            http_res->body = strdup("{\"error\":\"Failed to submit avatar\"}");
            http_res->header_keys = NULL;
            http_res->header_values = NULL;
            http_res->header_count = 0;
            return http_res;
        }
        
        printf("[HTTP] Avatar submitted, polling for completion...\n");
        
        // Poll for completion (non-blocking with VM unlock between checks)
        Value result;
        bool call_success = false;
        int attempts = 0;
        int max_attempts = 30000;  // 5 minutes (300 seconds) to match HTTP client timeout
        
        while (!call_success && attempts < max_attempts) {
            // Process avatar completions
            avatar_runtime_process_completions(server->vm->avatar_runtime, 10);
            
            // Check if complete
            if (avatar_runtime_is_complete(handle)) {
                if (avatar_runtime_has_error(handle)) {
                    const char* err = avatar_runtime_get_error(handle);
                    printf("[HTTP] Avatar error: %s\n", err);
                    free(avatar_args);
                    pthread_mutex_unlock(&server->vm_lock);
                    
                    http_res = malloc(sizeof(HttpResponse));
                    http_res->status_code = 500;
                    char* err_body = malloc(512);
                    snprintf(err_body, 512, "{\"error\":\"%s\"}", err ? err : "Handler execution failed");
                    http_res->body = err_body;
                    http_res->header_keys = NULL;
                    http_res->header_values = NULL;
                    http_res->header_count = 0;
                    return http_res;
                }
                
                result = avatar_runtime_get_result(handle);
                call_success = true;
                break;
            }
            
            // Small sleep
            pthread_mutex_unlock(&server->vm_lock);
            usleep(10000);  // 10ms
            pthread_mutex_lock(&server->vm_lock);
            attempts++;
        }
        
        // Free the response object's keys/values arrays that we allocated
        // (avatar has its own copy from memcpy, so this is safe)
        free(response_obj_with_id.as.object.keys[0]);  // free "__id" string
        free(response_obj_with_id.as.object.keys);
        free(response_obj_with_id.as.object.values);
        
        free(avatar_args);
        
        if (!call_success) {
            printf("[HTTP] ERROR: Avatar timeout - cancelling task\n");
            
            // Cancel the avatar task (best-effort)
            avatar_runtime_cancel(handle);
            
            // Get the response builder to release our reference
            extern void* kyl_aio_get_ptr(int);
            extern void kyl_aio_unregister_ptr(int);
            
            KylResponseBuilder* timed_out_builder = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
            if (timed_out_builder) {
                // Unregister from kyl_aio registry
                kyl_aio_unregister_ptr(response_id);
                
                // Release our reference (avatar may still hold a reference)
                response_builder_release(timed_out_builder);
            }
            
            pthread_mutex_unlock(&server->vm_lock);
            
            http_res = malloc(sizeof(HttpResponse));
            http_res->status_code = 504;
            http_res->body = strdup("{\"error\":\"Handler timeout\"}");
            http_res->header_keys = NULL;
            http_res->header_values = NULL;
            http_res->header_count = 0;
            return http_res;
        }
        
        printf("[HTTP] Avatar completed successfully\n");
        
        // Extract response from response builder
        extern void* kyl_aio_get_ptr(int);
        extern void kyl_aio_unregister_ptr(int);
        
        KylResponseBuilder* built_response = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
        
        if (!built_response) {
            printf("[HTTP] ERROR: Response builder not found after avatar completion\n");
            // Release avatar's reference even though builder is gone
            // (this shouldn't happen, but be safe)
            pthread_mutex_unlock(&server->vm_lock);
            
            http_res = malloc(sizeof(HttpResponse));
            http_res->status_code = 500;
            http_res->body = strdup("{\"error\":\"Response builder lost\"}");
            http_res->header_keys = NULL;
            http_res->header_values = NULL;
            http_res->header_count = 0;
            return http_res;
        }
        

        
        printf("[HTTP] Response builder status=%d, body_length=%zu, headers=%d\n",
               built_response->status_code, built_response->body_length, built_response->header_count);
        
        // Create HTTP response from builder
        http_res = malloc(sizeof(HttpResponse));
        http_res->status_code = built_response->status_code;
        http_res->body = built_response->body ? strdup(built_response->body) : strdup("");
        http_res->header_count = built_response->header_count;
        
        if (built_response->header_count > 0) {
            http_res->header_keys = malloc(sizeof(char*) * built_response->header_count);
            http_res->header_values = malloc(sizeof(char*) * built_response->header_count);
            
            for (int i = 0; i < built_response->header_count; i++) {
                http_res->header_keys[i] = strdup(built_response->header_names[i]);
                http_res->header_values[i] = strdup(built_response->header_values[i]);
            }
        } else {
            http_res->header_keys = NULL;
            http_res->header_values = NULL;
        }
        
        // Clean up response builder using reference counting
        // Unregister from kyl_aio registry first
        kyl_aio_unregister_ptr(response_id);
        
        // Release avatar's reference first (avatar completed successfully)
        printf("[HTTP] Releasing avatar's reference after completion\n");
        response_builder_release(built_response);
        
        // Release server's reference (will free if ref_count reaches 0)
        printf("[HTTP] Releasing server's reference after copying data\n");
        response_builder_release(built_response);
        
        pthread_mutex_unlock(&server->vm_lock);
        // avatar_args already freed earlier (line 1031), don't free again!
        
        if (params) route_params_free(params);
        
        return http_res;
        
        // OLD CODE - Handler returns JSON string directly (like webview bridge)
        // Convert result to HTTP response
        /*if (result.type == VALUE_OBJECT) {
            // Check if it's a Response object with status, body, headers
            int status_code = 200;
            char* body = NULL;
            int header_count = 0;
            char** header_keys = NULL;
            char** header_values = NULL;
            
            // Look for status property
            for (int i = 0; i < result.as.object.count; i++) {
                if (strcmp(result.as.object.keys[i], "status") == 0) {
                    if (result.as.object.values[i].type == VALUE_NUMBER) {
                        status_code = (int)result.as.object.values[i].as.number;
                    }
                }
                else if (strcmp(result.as.object.keys[i], "body") == 0) {
                    if (result.as.object.values[i].type == VALUE_STRING) {
                        body = strdup(result.as.object.values[i].as.string);
                    }
                }
                else if (strcmp(result.as.object.keys[i], "headers") == 0) {
                    if (result.as.object.values[i].type == VALUE_OBJECT) {
                        Value headers_obj = result.as.object.values[i];
                        header_count = headers_obj.as.object.count;
                        
                        if (header_count > 0) {
                            header_keys = malloc(sizeof(char*) * header_count);
                            header_values = malloc(sizeof(char*) * header_count);
                            
                            for (int j = 0; j < header_count; j++) {
                                header_keys[j] = strdup(headers_obj.as.object.keys[j]);
                                if (headers_obj.as.object.values[j].type == VALUE_STRING) {
                                    header_values[j] = strdup(headers_obj.as.object.values[j].as.string);
                                } else {
                                    header_values[j] = strdup("");
                                }
                            }
                        }
                    }
                }
            }*/ // END OLD CODE - now using response builder
        
        // This code path should not be reached anymore
        printf("[HTTP] WARNING: Unexpected code path - handler didn't use response builder\n");
        pthread_mutex_unlock(&server->vm_lock);
        free(avatar_args);
        
        http_res = malloc(sizeof(HttpResponse));
        http_res->status_code = 500;
        http_res->body = strdup("{\"error\":\"Handler didn't use response builder\"}");
        http_res->header_keys = NULL;
        http_res->header_values = NULL;
        http_res->header_count = 0;
        
        if (params) route_params_free(params);
        return http_res;
    } else {
        printf("[HTTP] Handler function not found: %s\n", route->handler_name);
        http_res = http_response_create(500, "Handler function not found");
    }
    
    pthread_mutex_unlock(&server->vm_lock);
    
    if (params) route_params_free(params);
    
    return http_res;
}

// Handle client connection (runs in thread pool)
static void* handle_client_async(void* user_data) {
    ClientContext* ctx = (ClientContext*)user_data;
    AsyncHttpServer* server = ctx->server;
    int client_fd = ctx->client_fd;
    free(ctx);  // Free context immediately
    
    printf("[HTTP] handle_client_async called, fd=%d\n", client_fd);
    fflush(stdout);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    printf("[HTTP] read %zd bytes\n", bytes_read);
    fflush(stdout);
    
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    
    HttpRequest* req = parse_http_request(buffer, client_fd, server->max_body_size);
    printf("[HTTP] parsed request: %p\n", (void*)req);
    fflush(stdout);
    if (!req) {
        close(client_fd);
        return NULL;
    }
    
    HttpResponse* res = handle_request(server, req);
    
    // Only send response if handler didn't already send it
    if (res) {
        send_http_response(client_fd, res);
        http_response_free(res);
    }
    
    http_request_free(req);
    close(client_fd);
    return NULL;  // Thread pool expects void* return
}

// Server thread function
static void* server_thread(void* arg) {
    AsyncHttpServer* server = (AsyncHttpServer*)arg;
    
    printf("[HTTP] Server listening on port %d\n", server->port);
    
    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        
        // Submit to thread pool for async handling
        if (server->io_pool) {
            ClientContext* ctx = malloc(sizeof(ClientContext));
            ctx->server = server;
            ctx->client_fd = client_fd;
            
            if (!thread_pool_submit(server->io_pool, handle_client_async, ctx, NULL, NULL)) {
                fprintf(stderr, "[HTTP] Failed to submit client to thread pool, handling synchronously\n");
                free(ctx);
                // Fallback: handle synchronously on accept thread (blocking)
                ClientContext sync_ctx = {server, client_fd};
                handle_client_async(&sync_ctx);
            }
        } else {
            // No thread pool, handle synchronously (blocking)
            ClientContext ctx = {server, client_fd};
            handle_client_async(&ctx);
        }
    }
    
    return NULL;
}

// Create server
AsyncHttpServer* async_http_server_create(int port) {
    AsyncHttpServer* server = calloc(1, sizeof(AsyncHttpServer));
    if (!server) return NULL;
    
    server->port = port;
    server->running = false;
    server->vm = NULL;
    server->max_body_size = MAX_BODY_SIZE;  // Default: 10MB
    server->static_mount_count = 0;  // Initialize static mounts
    pthread_mutex_init(&server->vm_lock, NULL);
    
    // Create thread pool for async I/O (4 worker threads for file/body reads)
    server->io_pool = thread_pool_create(4);
    if (!server->io_pool) {
        fprintf(stderr, "[HTTP] Warning: Failed to create I/O thread pool, will use synchronous I/O\n");
    }
    
    // Create socket
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        perror("[HTTP] socket() failed");
        free(server);
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[HTTP] bind() failed");
        close(server->server_fd);
        free(server);
        return NULL;
    }
    
    // Listen
    if (listen(server->server_fd, MAX_CONNECTIONS) < 0) {
        perror("[HTTP] listen() failed");
        close(server->server_fd);
        free(server);
        return NULL;
    }
    
    return server;
}

// Start server
bool async_http_server_start(AsyncHttpServer* server) {
    if (!server || server->running) return false;
    
    server->running = true;
    
    if (pthread_create(&server->thread, NULL, server_thread, server) != 0) {
        server->running = false;
        return false;
    }
    
    return true;
}

// Stop server
void async_http_server_stop(AsyncHttpServer* server) {
    if (!server || !server->running) return;
    
    server->running = false;
    shutdown(server->server_fd, SHUT_RDWR);
    pthread_join(server->thread, NULL);
}

// Destroy server
void async_http_server_destroy(AsyncHttpServer* server) {
    if (!server) return;
    
    async_http_server_stop(server);
    close(server->server_fd);
    pthread_mutex_destroy(&server->vm_lock);
    
    // Destroy I/O thread pool
    if (server->io_pool) {
        thread_pool_destroy(server->io_pool);
    }
    
    // Free static mounts
    for (int i = 0; i < server->static_mount_count; i++) {
        free(server->static_mounts[i].url_prefix);
        free(server->static_mounts[i].directory);
    }
    
    free(server);
}

// Set VM
void async_http_server_set_vm(AsyncHttpServer* server, VM* vm) {
    if (!server) return;
    pthread_mutex_lock(&server->vm_lock);
    server->vm = vm;
    pthread_mutex_unlock(&server->vm_lock);
}

// Built-in: http_start_server(port)
Value builtin_http_start_server(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[HTTP] start_server requires port number\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    int port = (int)args[0].as.number;
    
    if (g_server) {
        fprintf(stderr, "[HTTP] Server already running\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    g_server = async_http_server_create(port);
    if (!g_server) {
        fprintf(stderr, "[HTTP] Failed to create server\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    // Note: VM will be set externally via async_http_server_set_vm
    
    if (!async_http_server_start(g_server)) {
        async_http_server_destroy(g_server);
        g_server = NULL;
        fprintf(stderr, "[HTTP] Failed to start server\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Built-in: http_stop_server()
Value builtin_http_stop_server(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    if (!g_server) {
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    async_http_server_destroy(g_server);
    g_server = NULL;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Get global server instance (for setting VM)
AsyncHttpServer* async_http_server_get_global() {
    return g_server;
}

// Configure maximum body size
void async_http_server_set_max_body_size(AsyncHttpServer* server, size_t max_size) {
    if (!server) return;
    server->max_body_size = max_size;
    printf("[HTTP] Max body size set to %zu bytes (%.2f MB)\n", 
           max_size, max_size / (1024.0 * 1024.0));
}

// Get current maximum body size
size_t async_http_server_get_max_body_size(AsyncHttpServer* server) {
    if (!server) return MAX_BODY_SIZE;
    return server->max_body_size;
}

// Built-in: http_static_add(url_prefix, directory)
Value builtin_http_static_add(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[HTTP] http_static_add requires (url_prefix:string, directory:string)\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    if (!g_server) {
        fprintf(stderr, "[HTTP] Server not running\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    if (g_server->static_mount_count >= MAX_STATIC_MOUNTS) {
        fprintf(stderr, "[HTTP] Maximum static mounts reached (%d)\n", MAX_STATIC_MOUNTS);
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    const char* url_prefix = args[0].as.string;
    const char* directory = args[1].as.string;
    
    // Add new static mount
    StaticMount* mount = &g_server->static_mounts[g_server->static_mount_count];
    mount->url_prefix = strdup(url_prefix);
    mount->directory = strdup(directory);
    mount->active = true;
    g_server->static_mount_count++;
    
    printf("[HTTP] Static mount added: %s -> %s\n", url_prefix, directory);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Wait for server (blocks but doesn't use VM)
Value builtin_http_server_wait(int arg_count, Value* args) {
    if (!g_server || !g_server->running) {
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    // Wait for server thread to finish (it won't until server is stopped)
    pthread_join(g_server->thread, NULL);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
