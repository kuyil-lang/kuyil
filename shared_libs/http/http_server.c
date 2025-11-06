#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>

// MIME type detection for static files
const char* http_get_mime_type(const char* file_path) {
    if (!file_path) return "application/octet-stream";
    
    const char* ext = strrchr(file_path, '.');
    if (!ext) return "application/octet-stream";
    
    // Convert to lowercase for comparison
    char lower_ext[16];
    int i;
    for (i = 0; i < 15 && ext[i+1] != '\0'; i++) {
        lower_ext[i] = tolower(ext[i+1]);
    }
    lower_ext[i] = '\0';
    
    // Common MIME types
    if (strcmp(lower_ext, "html") == 0 || strcmp(lower_ext, "htm") == 0) return "text/html";
    if (strcmp(lower_ext, "css") == 0) return "text/css";
    if (strcmp(lower_ext, "js") == 0) return "application/javascript";
    if (strcmp(lower_ext, "json") == 0) return "application/json";
    if (strcmp(lower_ext, "xml") == 0) return "application/xml";
    if (strcmp(lower_ext, "txt") == 0) return "text/plain";
    
    // Images
    if (strcmp(lower_ext, "png") == 0) return "image/png";
    if (strcmp(lower_ext, "jpg") == 0 || strcmp(lower_ext, "jpeg") == 0) return "image/jpeg";
    if (strcmp(lower_ext, "gif") == 0) return "image/gif";
    if (strcmp(lower_ext, "svg") == 0) return "image/svg+xml";
    if (strcmp(lower_ext, "ico") == 0) return "image/x-icon";
    if (strcmp(lower_ext, "webp") == 0) return "image/webp";
    
    // Documents
    if (strcmp(lower_ext, "pdf") == 0) return "application/pdf";
    if (strcmp(lower_ext, "doc") == 0) return "application/msword";
    if (strcmp(lower_ext, "docx") == 0) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    
    // Archives
    if (strcmp(lower_ext, "zip") == 0) return "application/zip";
    if (strcmp(lower_ext, "tar") == 0) return "application/x-tar";
    if (strcmp(lower_ext, "gz") == 0) return "application/gzip";
    
    // Media
    if (strcmp(lower_ext, "mp3") == 0) return "audio/mpeg";
    if (strcmp(lower_ext, "mp4") == 0) return "video/mp4";
    if (strcmp(lower_ext, "avi") == 0) return "video/x-msvideo";
    
    // Fonts
    if (strcmp(lower_ext, "woff") == 0) return "font/woff";
    if (strcmp(lower_ext, "woff2") == 0) return "font/woff2";
    if (strcmp(lower_ext, "ttf") == 0) return "font/ttf";
    if (strcmp(lower_ext, "otf") == 0) return "font/otf";
    
    return "application/octet-stream";
}

// Path traversal protection
bool http_is_safe_path(const char* path) {
    if (!path) return false;
    
    // Check for path traversal attempts
    if (strstr(path, "..") != NULL) return false;
    if (strstr(path, "//") != NULL) return false;
    if (path[0] == '/') return false; // Absolute paths not allowed
    
    // Check for dangerous characters
    if (strchr(path, '\0') != path + strlen(path)) return false;
    if (strchr(path, '\n') != NULL) return false;
    if (strchr(path, '\r') != NULL) return false;
    
    return true;
}

// HTTP Response callback for curl
static size_t write_callback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
    size_t realsize = size * nmemb;
    
    char* ptr = realloc(response->data, response->size + realsize + 1);
    if (ptr == NULL) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;
    
    return realsize;
}

// Initialize HTTP subsystem
void http_init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_cleanup() {
    curl_global_cleanup();
}

// Development Helper Services Implementation

#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>

DevHelper* dev_helper_create(HttpServer* server) {
    DevHelper* helper = malloc(sizeof(DevHelper));
    if (!helper) return NULL;
    
    helper->watch_files = NULL;
    helper->watch_count = 0;
    helper->watch_capacity = 0;
    helper->last_modified = NULL;
    helper->auto_reload = false;
    helper->server = server;
    helper->reload_callback = NULL;
    
    return helper;
}

void dev_helper_watch_file(DevHelper* helper, const char* filepath) {
    if (!helper || !filepath) return;
    
    // Expand capacity if needed
    if (helper->watch_count >= helper->watch_capacity) {
        int new_capacity = helper->watch_capacity == 0 ? 8 : helper->watch_capacity * 2;
        helper->watch_files = realloc(helper->watch_files, sizeof(char*) * new_capacity);
        helper->last_modified = realloc(helper->last_modified, sizeof(time_t) * new_capacity);
        helper->watch_capacity = new_capacity;
    }
    
    // Add file to watch list
    helper->watch_files[helper->watch_count] = strdup(filepath);
    
    // Get initial modification time
    struct stat file_stat;
    if (stat(filepath, &file_stat) == 0) {
        helper->last_modified[helper->watch_count] = file_stat.st_mtime;
    } else {
        helper->last_modified[helper->watch_count] = 0;
    }
    
    helper->watch_count++;
    printf("[DEV] Watching file: %s\n", filepath);
}

void dev_helper_watch_directory(DevHelper* helper, const char* dirpath) {
    if (!helper || !dirpath) return;
    
    DIR* dir = opendir(dirpath);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == 8) { // DT_REG - Regular file
            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
            
            // Watch .e files and common web files
            char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".e") == 0 || 
                       strcmp(ext, ".js") == 0 || 
                       strcmp(ext, ".css") == 0 || 
                       strcmp(ext, ".html") == 0)) {
                dev_helper_watch_file(helper, fullpath);
            }
        }
    }
    closedir(dir);
}

bool dev_helper_check_changes(DevHelper* helper) {
    if (!helper) return false;
    
    bool changed = false;
    struct stat file_stat;
    
    for (int i = 0; i < helper->watch_count; i++) {
        if (stat(helper->watch_files[i], &file_stat) == 0) {
            if (file_stat.st_mtime > helper->last_modified[i]) {
                printf("[DEV] File changed: %s\n", helper->watch_files[i]);
                helper->last_modified[i] = file_stat.st_mtime;
                changed = true;
            }
        }
    }
    
    return changed;
}

static void* watch_thread(void* arg) {
    DevHelper* helper = (DevHelper*)arg;
    
    printf("[DEV] Starting file watcher thread...\n");
    
    while (helper->auto_reload) {
        if (dev_helper_check_changes(helper)) {
            printf("[DEV] Changes detected! Triggering reload...\n");
            
            if (helper->reload_callback) {
                helper->reload_callback();
            }
            
            // Brief pause to avoid rapid reloads
            sleep(1);
        }
        
        // Check every 500ms
        usleep(500000);
    }
    
    printf("[DEV] File watcher thread stopped.\n");
    return NULL;
}

void dev_helper_start_watching(DevHelper* helper) {
    if (!helper || helper->auto_reload) return;
    
    helper->auto_reload = true;
    
    pthread_t watch_pthread;
    if (pthread_create(&watch_pthread, NULL, watch_thread, helper) != 0) {
        printf("[DEV] Failed to start file watcher thread\n");
        helper->auto_reload = false;
    } else {
        pthread_detach(watch_pthread);
        printf("[DEV] File watcher started\n");
    }
}

void dev_helper_stop_watching(DevHelper* helper) {
    if (!helper) return;
    
    helper->auto_reload = false;
    printf("[DEV] File watcher stopped\n");
}

void dev_helper_set_reload_callback(DevHelper* helper, void (*callback)(void)) {
    if (!helper) return;
    helper->reload_callback = callback;
}

void dev_helper_free(DevHelper* helper) {
    if (!helper) return;
    
    dev_helper_stop_watching(helper);
    
    for (int i = 0; i < helper->watch_count; i++) {
        free(helper->watch_files[i]);
    }
    free(helper->watch_files);
    free(helper->last_modified);
    free(helper);
}

// HTTP Client implementation
HttpResponse* http_get(const char* url) {
    CURL* curl;
    CURLcode res;
    
    HttpResponse* response = malloc(sizeof(HttpResponse));
    response->data = malloc(1);
    response->size = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kuyil/1.0");
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    return response;
}

HttpResponse* http_post(const char* url, const char* data) {
    CURL* curl;
    CURLcode res;
    
    HttpResponse* response = malloc(sizeof(HttpResponse));
    response->data = malloc(1);
    response->size = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kuyil/1.0");
        
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    return response;
}

HttpResponse* http_put(const char* url, const char* data) {
    CURL* curl;
    CURLcode res;
    
    HttpResponse* response = malloc(sizeof(HttpResponse));
    response->data = malloc(1);
    response->size = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kuyil/1.0");
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    return response;
}

HttpResponse* http_delete(const char* url) {
    CURL* curl;
    CURLcode res;
    
    HttpResponse* response = malloc(sizeof(HttpResponse));
    response->data = malloc(1);
    response->size = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kuyil/1.0");
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    return response;
}

void http_client_response_free(HttpResponse* response) {
    if (response) {
        free(response->data);
        free(response);
    }
}

// Simple HTTP Server implementation

// Structure to pass to client handler thread
typedef struct {
    int client_fd;
    HttpServer* server;
} ClientHandlerArgs;

// Static mount entry
typedef struct {
    char* prefix;   // URL prefix (e.g., "/", "/api/files")
    char* root;     // Filesystem root directory
} StaticMount;

struct HttpServer {
    int port;
    bool running;
    int socket_fd;
    Route* routes;
    int route_count;
    int route_capacity;
    // Legacy single static root/prefix (deprecated but kept for compatibility)
    char* static_root;
    bool serve_static;
    char* static_prefix;
    // Dynamic array of static mounts (supports 16-128 growing)
    StaticMount* static_mounts;
    int static_mount_count;
    int static_mount_capacity;
    // URL prefixes to bypass static serving and SPA fallback (e.g., "/api")
    char** static_bypass_prefixes;
    int static_bypass_count;
    int static_bypass_capacity;
};

// HTTP Request/Response helper functions implementation

HttpRequest* http_request_create() {
    HttpRequest* req = calloc(1, sizeof(HttpRequest));
    return req;
}

void http_request_free(HttpRequest* req) {
    if (!req) return;
    
    free(req->method);
    free(req->path);
    free(req->query_string);
    free(req->body);
    
    for (int i = 0; i < req->header_count; i++) {
        free(req->header_names[i]);
        free(req->header_values[i]);
    }
    free(req->header_names);
    free(req->header_values);
    
    for (int i = 0; i < req->param_count; i++) {
        free(req->param_names[i]);
        free(req->param_values[i]);
    }
    free(req->param_names);
    free(req->param_values);
    
    free(req);
}

void http_request_add_header(HttpRequest* req, const char* name, const char* value) {
    if (!req || !name || !value) return;
    
    req->header_names = realloc(req->header_names, sizeof(char*) * (req->header_count + 1));
    req->header_values = realloc(req->header_values, sizeof(char*) * (req->header_count + 1));
    
    req->header_names[req->header_count] = strdup(name);
    req->header_values[req->header_count] = strdup(value);
    req->header_count++;
}

void http_request_add_param(HttpRequest* req, const char* name, const char* value) {
    if (!req || !name || !value) return;
    
    req->param_names = realloc(req->param_names, sizeof(char*) * (req->param_count + 1));
    req->param_values = realloc(req->param_values, sizeof(char*) * (req->param_count + 1));
    
    req->param_names[req->param_count] = strdup(name);
    req->param_values[req->param_count] = strdup(value);
    req->param_count++;
}

const char* http_request_get_header(HttpRequest* req, const char* name) {
    if (!req || !name) return NULL;
    
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->header_names[i], name) == 0) {
            return req->header_values[i];
        }
    }
    return NULL;
}

const char* http_request_get_param(HttpRequest* req, const char* name) {
    if (!req || !name) return NULL;
    
    for (int i = 0; i < req->param_count; i++) {
        if (strcmp(req->param_names[i], name) == 0) {
            return req->param_values[i];
        }
    }
    return NULL;
}

HttpResponseBuilder* http_response_create() {
    HttpResponseBuilder* res = calloc(1, sizeof(HttpResponseBuilder));
    res->status_code = 200;
    res->content_type = strdup("text/plain");
    return res;
}

void http_response_builder_free(HttpResponseBuilder* res) {
    if (!res) return;
    
    free(res->body);
    free(res->content_type);
    
    for (int i = 0; i < res->header_count; i++) {
        free(res->header_names[i]);
        free(res->header_values[i]);
    }
    free(res->header_names);
    free(res->header_values);
    
    free(res);
}

void http_response_set_status(HttpResponseBuilder* res, int status_code) {
    if (res) res->status_code = status_code;
}

void http_response_set_body(HttpResponseBuilder* res, const char* body, size_t length) {
    if (!res || !body) return;
    
    free(res->body);
    res->body = malloc(length + 1);
    memcpy(res->body, body, length);
    res->body[length] = '\0';
    res->body_length = length;
}

void http_response_set_json(HttpResponseBuilder* res, const char* json) {
    if (!res || !json) return;
    
    http_response_set_body(res, json, strlen(json));
    http_response_set_content_type(res, "application/json");
}

void http_response_add_header(HttpResponseBuilder* res, const char* name, const char* value) {
    if (!res || !name || !value) return;
    
    res->header_names = realloc(res->header_names, sizeof(char*) * (res->header_count + 1));
    res->header_values = realloc(res->header_values, sizeof(char*) * (res->header_count + 1));
    
    res->header_names[res->header_count] = strdup(name);
    res->header_values[res->header_count] = strdup(value);
    res->header_count++;
}

void http_response_set_content_type(HttpResponseBuilder* res, const char* content_type) {
    if (!res || !content_type) return;
    
    free(res->content_type);
    res->content_type = strdup(content_type);
}

const char* http_status_text(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

char* http_response_build(HttpResponseBuilder* res, size_t* total_length) {
    if (!res) return NULL;
    
    // Calculate total size needed
    size_t header_size = 256; // Status line + basic headers
    for (int i = 0; i < res->header_count; i++) {
        header_size += strlen(res->header_names[i]) + strlen(res->header_values[i]) + 4; // ": \r\n"
    }
    header_size += strlen(res->content_type) + 16; // "Content-Type: \r\n"
    
    size_t body_len = res->body ? res->body_length : 0;
    size_t total = header_size + body_len + 128; // Extra space for Content-Length, etc.
    
    char* response = malloc(total);
    if (!response) return NULL;
    
    // Build status line
    int offset = snprintf(response, total, "HTTP/1.1 %d %s\r\n", 
                         res->status_code, http_status_text(res->status_code));
    
    // Add Content-Type
    offset += snprintf(response + offset, total - offset, 
                      "Content-Type: %s\r\n", res->content_type);
    
    // Add Content-Length
    offset += snprintf(response + offset, total - offset, 
                      "Content-Length: %zu\r\n", body_len);
    
    // Add custom headers
    for (int i = 0; i < res->header_count; i++) {
        offset += snprintf(response + offset, total - offset, 
                          "%s: %s\r\n", res->header_names[i], res->header_values[i]);
    }
    
    // End headers
    offset += snprintf(response + offset, total - offset, "\r\n");
    
    // Add body
    if (res->body && body_len > 0) {
        memcpy(response + offset, res->body, body_len);
        offset += body_len;
    }
    
    if (total_length) *total_length = offset;
    return response;
}

// Path parameter matching (supports :param syntax)
static bool match_route_path(const char* route_pattern, const char* request_path, HttpRequest* req) {
    const char* r = route_pattern;
    const char* p = request_path;
    
    while (*r && *p) {
        if (*r == ':') {
            // Extract parameter name
            r++; // Skip ':'
            const char* param_start = r;
            while (*r && *r != '/') r++;
            
            size_t param_name_len = r - param_start;
            char* param_name = strndup(param_start, param_name_len);
            
            // Extract parameter value
            const char* value_start = p;
            while (*p && *p != '/') p++;
            
            size_t value_len = p - value_start;
            char* param_value = strndup(value_start, value_len);
            
            http_request_add_param(req, param_name, param_value);
            
            free(param_name);
            free(param_value);
        } else if (*r == *p) {
            r++;
            p++;
        } else {
            return false;
        }
    }
    
    return (*r == '\0' && *p == '\0');
}

// Parse HTTP headers from request buffer
static void parse_http_headers(const char* buffer, size_t buffer_len, HttpRequest* req) {
    const char* line_start = buffer;
    const char* line_end;
    
    // Skip request line
    line_end = strstr(line_start, "\r\n");
    if (!line_end) return;
    line_start = line_end + 2;
    
    // Parse headers
    while ((line_end = strstr(line_start, "\r\n")) != NULL && line_end != line_start) {
        // Find the colon separator
        const char* colon = strchr(line_start, ':');
        if (colon && colon < line_end) {
            size_t name_len = colon - line_start;
            char* name = strndup(line_start, name_len);
            
            // Skip colon and whitespace
            const char* value_start = colon + 1;
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
                value_start++;
            }
            
            size_t value_len = line_end - value_start;
            char* value = strndup(value_start, value_len);
            
            http_request_add_header(req, name, value);
            
            free(name);
            free(value);
        }
        
        line_start = line_end + 2;
    }
    
    // Parse body if present (after empty line)
    if (line_end) {
        const char* body_start = line_end + 2;
        if (*body_start && body_start < buffer + buffer_len) {
            // Get Content-Length header to handle binary data correctly
            const char* content_length_str = http_request_get_header(req, "Content-Length");
            size_t content_length = content_length_str ? (size_t)atoll(content_length_str) : 0;
            
            // Calculate maximum possible body length from buffer
            size_t max_body_len = buffer_len - (body_start - buffer);
            size_t actual_body_len = content_length > 0 && content_length <= max_body_len ? content_length : max_body_len;
            
            // Use minimum of Content-Length and available buffer data
            if (actual_body_len > max_body_len) actual_body_len = max_body_len;
            
            req->body = malloc(actual_body_len + 1);
            if (req->body) {
                memcpy(req->body, body_start, actual_body_len);
                req->body[actual_body_len] = '\0'; // Null terminate for safety
                req->body_length = actual_body_len;
            }
        }
    }
}

static void* handle_client(void* arg) {
    ClientHandlerArgs* args = (ClientHandlerArgs*)arg;
    int client_fd = args->client_fd;
    HttpServer* server = args->server;
    free(arg);
    
    // Initial buffer for headers (should be enough for most cases)
    char initial_buffer[8192];
    ssize_t initial_read = read(client_fd, initial_buffer, sizeof(initial_buffer) - 1);
    
    if (initial_read <= 0) {
        close(client_fd);
        return NULL;
    }
    
    initial_buffer[initial_read] = '\0';
    
    // Find Content-Length in headers to determine if we need to read more
    const char* cl_header = strstr(initial_buffer, "Content-Length: ");
    size_t content_length = 0;
    if (cl_header) {
        content_length = (size_t)atoll(cl_header + 16);
    }
    
    // Find end of headers (empty line)
    const char* headers_end = strstr(initial_buffer, "\r\n\r\n");
    if (!headers_end) headers_end = strstr(initial_buffer, "\n\n");
    
    size_t headers_size = 0;
    if (headers_end) {
        headers_size = headers_end - initial_buffer + 4; // Include the \r\n\r\n
    } else {
        headers_size = initial_read; // No body separator found
    }
    
    // Calculate how much body we already have and how much more we need
    size_t body_in_initial = initial_read - headers_size;
    size_t total_needed = headers_size + content_length;
    
    char* buffer = NULL;
    ssize_t bytes_read = initial_read;
    
    // If we need more data, allocate a larger buffer and continue reading
    if (content_length > 0 && total_needed > (size_t)initial_read) {
        buffer = malloc(total_needed + 1);
        if (!buffer) {
            close(client_fd);
            return NULL;
        }
        
        // Copy initial data
        memcpy(buffer, initial_buffer, initial_read);
        bytes_read = initial_read;
        
        // Read remaining body data
        while (bytes_read < (ssize_t)total_needed) {
            ssize_t chunk = read(client_fd, buffer + bytes_read, total_needed - bytes_read);
            if (chunk <= 0) break; // Connection closed or error
            bytes_read += chunk;
        }
        
        buffer[bytes_read] = '\0';
    } else {
        // All data fit in initial read, use stack buffer
        buffer = initial_buffer;
    }
    
    if (bytes_read > 0) {
        // Create HTTP request object
        HttpRequest* req = http_request_create();
        
        // Parse HTTP request line
        char method[16], path[512], version[16];
        sscanf(buffer, "%s %s %s", method, path, version);
        
        req->method = strdup(method);
        
        // Split path and query string
        char* query_start = strchr(path, '?');
        if (query_start) {
            *query_start = '\0';
            req->query_string = strdup(query_start + 1);
        }
        
        // URL decode path
        char decoded_path[512];
        int j = 0;
        for (int i = 0; path[i] && j < sizeof(decoded_path) - 1; i++) {
            if (path[i] == '%' && path[i+1] && path[i+2]) {
                int hex_val;
                sscanf(&path[i+1], "%2x", &hex_val);
                decoded_path[j++] = (char)hex_val;
                i += 2;
            } else if (path[i] == '+') {
                decoded_path[j++] = ' ';
            } else {
                decoded_path[j++] = path[i];
            }
        }
        decoded_path[j] = '\0';
        
        req->path = strdup(decoded_path);
        
        // Parse headers and body
        parse_http_headers(buffer, bytes_read, req);
        
        // Check if this is a static file request (supports multiple mounts)
        // Support both GET and HEAD methods for static files; HEAD sends headers only
        if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) && server->serve_static && server->static_mount_count > 0) {
            const char* original_path = decoded_path;

            // IMPORTANT: Never attempt static handling for paths under configured bypass prefixes
            // When mounting SPA at '/', static probing for certain prefixes (e.g., '/api') can interfere with dynamic routes.
            // Skip static handling early for any path beginning with a configured bypass prefix and let dynamic routes handle it.
            int should_bypass_static = 0;
            for (int bp = 0; bp < server->static_bypass_count; bp++) {
                const char* pref = server->static_bypass_prefixes[bp];
                if (pref && pref[0] != '\0') {
                    size_t plen = strlen(pref);
                    if (strncmp(original_path, pref, plen) == 0) {
                        should_bypass_static = 1;
                        break;
                    }
                }
            }
            if (should_bypass_static) {
                goto AFTER_STATIC_CHECK;
            }
            
            // Try each static mount in order
            for (int mount_idx = 0; mount_idx < server->static_mount_count; mount_idx++) {
                StaticMount* mount = &server->static_mounts[mount_idx];
                const char* urlpath = original_path;
                
                // Check if URL matches this mount's prefix
                if (mount->prefix && mount->prefix[0] != '\0') {
                    size_t prefix_len = strlen(mount->prefix);
                    if (strncmp(urlpath, mount->prefix, prefix_len) != 0) {
                        // Doesn't match this prefix, try next mount
                        continue;
                    }
                    // Strip the prefix
                    urlpath += prefix_len;
                }
                
                // Trim leading slash after prefix stripping
                if (urlpath[0] == '/') urlpath++;
                
                // Determine file path (add index.html for directories)
                const char* file_path = NULL;
                char index_path[512];
                if (strlen(urlpath) == 0 || urlpath[strlen(urlpath)-1] == '/') {
                    snprintf(index_path, sizeof(index_path), "%sindex.html", urlpath);
                    file_path = index_path;
                } else {
                    file_path = urlpath;
                }
                
                // Try to serve the file from this mount
                char* saved_root = server->static_root;
                server->static_root = mount->root;
                bool served = http_server_serve_file(server, file_path, client_fd, req);
                server->static_root = saved_root;
                
                if (served) {
                    // Successfully served from this mount
                    http_request_free(req);
                    close(client_fd);
                    return NULL;
                }
                
                // File not found in this mount, check if SPA fallback applies
                // SPA fallback: for the first mount (typically the frontend).
                // If prefix is empty (mounted at '/'), also allow SPA fallback as long as request is not for '/api'.
                if (mount_idx == 0 && ((mount->prefix && mount->prefix[0] != '\0') || (mount->prefix && mount->prefix[0] == '\0'))) {
                    fprintf(stderr, "[HTTP] SPA fallback: trying index.html for %s\n", original_path);
                    char* saved_root2 = server->static_root;
                    server->static_root = mount->root;
                    bool served2 = http_server_serve_file(server, "index.html", client_fd, req);
                    server->static_root = saved_root2;
                    if (served2) {
                        fprintf(stderr, "[HTTP] Served index.html for SPA route: %s\n", original_path);
                        http_request_free(req);
                        close(client_fd);
                        return NULL;
                    } else {
                        fprintf(stderr, "[HTTP] Failed to serve index.html for SPA route: %s\n", original_path);
                    }
                }
                
                // Continue to next mount if prefix matched but file not found
            }
        }
AFTER_STATIC_CHECK:
        
        // Check for dynamic route matches
        bool route_found = false;
        for (int i = 0; i < server->route_count; i++) {
            Route* route = &server->routes[i];
            if (strcmp(method, route->method) == 0 && match_route_path(route->path, decoded_path, req)) {
                // Route found - call callback if available
                if (route->callback) {
                    HttpResponseBuilder* res = http_response_create();
                    
                    // Call the route handler
                    route->callback(req, res, route->user_data);
                    
                    // Build and send response
                    size_t response_length;
                    char* response_str = http_response_build(res, &response_length);
                    if (response_str) {
                        write(client_fd, response_str, response_length);
                        free(response_str);
                    }
                    
                    http_response_builder_free(res);
                } else {
                    // Legacy handler - send basic success response
                    const char* response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 33\r\n"
                        "\r\n"
                        "{\"message\": \"Route handler found\"}";
                    
                    write(client_fd, response, strlen(response));
                }
                route_found = true;
                break;
            }
        }
        
        // If no route found, try SPA fallback before sending 404
        if (!route_found) {
            bool did_spa_fallback = false;
            // Only fallback for non-API GET/HEAD requests when static mounts are present
            if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) && server->static_mount_count > 0) {
                const char* original_path = decoded_path;
                // Never fallback for configured bypass prefixes
                int is_bypass = 0;
                for (int bp = 0; bp < server->static_bypass_count; bp++) {
                    const char* pref = server->static_bypass_prefixes[bp];
                    if (pref && pref[0] != '\0') {
                        size_t plen = strlen(pref);
                        if (strncmp(original_path, pref, plen) == 0) {
                            is_bypass = 1;
                            break;
                        }
                    }
                }
                if (!is_bypass) {
                    // Serve index.html from the SPA root.
                    // Prefer server->static_root (last configured mount), else try to find a mount with prefix "/" or empty.
                    const char* spa_root = server->static_root;
                    if (!spa_root) {
                        for (int m = server->static_mount_count - 1; m >= 0; m--) {
                            StaticMount* mm = &server->static_mounts[m];
                            if (mm->prefix && (mm->prefix[0] == '\0' || strcmp(mm->prefix, "/") == 0)) {
                                spa_root = mm->root;
                                break;
                            }
                        }
                        // As a last resort, use the last mount's root
                        if (!spa_root && server->static_mount_count > 0) {
                            spa_root = server->static_mounts[server->static_mount_count - 1].root;
                        }
                    }
                    char* saved_root = server->static_root;
                    fprintf(stderr, "[HTTP] SPA 404-fallback: serving index.html for %s\n", original_path);
                    if (spa_root) {
                        // Build an explicit path to avoid relying on transient server->static_root state
                        char spa_index[1024];
                        snprintf(spa_index, sizeof(spa_index), "%s/%s", spa_root, "index.html");
                        server->static_root = NULL; // ensure http_server_serve_file uses provided path as-is
                        did_spa_fallback = http_server_serve_file(server, spa_index, client_fd, req);
                        server->static_root = saved_root;
                    } else {
                        // No SPA root identified; fall back to default behavior
                        did_spa_fallback = false;
                    }
                    if (did_spa_fallback) {
                        http_request_free(req);
                        // Free dynamically allocated buffer if we used one
                        if (buffer != initial_buffer) { free(buffer); }
                        close(client_fd);
                        return NULL;
                    }
                }
            }

            // Default 404 JSON for APIs or when SPA fallback isn't applicable
            HttpResponseBuilder* res = http_response_create();
            http_response_set_status(res, 404);
            http_response_set_json(res, "{\"error\": \"Route not found\"}");
            
            size_t response_length;
            char* response_str = http_response_build(res, &response_length);
            if (response_str) {
                write(client_fd, response_str, response_length);
                free(response_str);
            }
            
            http_response_builder_free(res);
        }
        
        http_request_free(req);
    }
    
    // Free dynamically allocated buffer if we used one
    if (buffer != initial_buffer) {
        free(buffer);
    }
    
    close(client_fd);
    return NULL;
}

HttpServer* http_server_create(int port) {
    HttpServer* server = malloc(sizeof(HttpServer));
    server->port = port;
    server->running = false;
    server->socket_fd = -1;
    server->routes = NULL;
    server->route_count = 0;
    server->route_capacity = 0;
    server->static_root = NULL;
    server->serve_static = false;
    server->static_prefix = NULL;
    // Initialize dynamic static mounts array with initial capacity of 16
    server->static_mounts = malloc(sizeof(StaticMount) * 16);
    server->static_mount_count = 0;
    server->static_mount_capacity = 16;
    // Initialize bypass prefixes
    server->static_bypass_prefixes = NULL;
    server->static_bypass_count = 0;
    server->static_bypass_capacity = 0;
    
    return server;
}

void http_server_get(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        int new_capacity = server->route_capacity == 0 ? 128 : server->route_capacity * 2;
        Route* new_routes = realloc(server->routes, sizeof(Route) * new_capacity);
        if (!new_routes) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for routes (capacity: %d)\n", new_capacity);
            return;
        }
        server->routes = new_routes;
        server->route_capacity = new_capacity;
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("GET");
    route->path = strdup(path);
    route->handler = handler;
    route->callback = NULL;
    route->user_data = NULL;
}

void http_server_post(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        int new_capacity = server->route_capacity == 0 ? 128 : server->route_capacity * 2;
        Route* new_routes = realloc(server->routes, sizeof(Route) * new_capacity);
        if (!new_routes) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for routes (capacity: %d)\n", new_capacity);
            return;
        }
        server->routes = new_routes;
        server->route_capacity = new_capacity;
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("POST");
    route->path = strdup(path);
    route->handler = handler;
    route->callback = NULL;
    route->user_data = NULL;
}

void http_server_put(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        int new_capacity = server->route_capacity == 0 ? 128 : server->route_capacity * 2;
        Route* new_routes = realloc(server->routes, sizeof(Route) * new_capacity);
        if (!new_routes) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for routes (capacity: %d)\n", new_capacity);
            return;
        }
        server->routes = new_routes;
        server->route_capacity = new_capacity;
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("PUT");
    route->path = strdup(path);
    route->handler = handler;
    route->callback = NULL;
    route->user_data = NULL;
}

void http_server_delete(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        int new_capacity = server->route_capacity == 0 ? 128 : server->route_capacity * 2;
        Route* new_routes = realloc(server->routes, sizeof(Route) * new_capacity);
        if (!new_routes) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for routes (capacity: %d)\n", new_capacity);
            return;
        }
        server->routes = new_routes;
        server->route_capacity = new_capacity;
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("DELETE");
    route->path = strdup(path);
    route->handler = handler;
    route->callback = NULL;
    route->user_data = NULL;
}

// New callback-based route registration
void http_server_register_route(HttpServer* server, const char* method, const char* path,
                                 RouteHandlerFunc callback, void* user_data) {
    if (!server || !method || !path || !callback) return;
    
    if (server->route_count >= server->route_capacity) {
        int new_capacity = server->route_capacity == 0 ? 128 : server->route_capacity * 2;
        Route* new_routes = realloc(server->routes, sizeof(Route) * new_capacity);
        if (!new_routes) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for routes (capacity: %d)\n", new_capacity);
            return;
        }
        server->routes = new_routes;
        server->route_capacity = new_capacity;
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup(method);
    route->path = strdup(path);
    route->handler = NULL;
    route->callback = callback;
    route->user_data = user_data;
}

void http_server_listen(HttpServer* server) {
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Create socket
    if ((server->socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }
    
    // Set socket options
    if (setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt))) {
        perror("setsockopt");
        return;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server->port);
    
    // Bind socket
    if (bind(server->socket_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return;
    }
    
    // Listen for connections
    if (listen(server->socket_fd, 3) < 0) {
        perror("listen");
        return;
    }
    
    server->running = true;
    printf("HTTP server listening on port %d\n", server->port);
    
    while (server->running) {
        ClientHandlerArgs* args = malloc(sizeof(ClientHandlerArgs));
        if ((args->client_fd = accept(server->socket_fd, (struct sockaddr*)&address,
                                (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            free(args);
            continue;
        }
        
        args->server = server;
        
        // Handle client in a new thread
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, args);
        pthread_detach(thread);
    }
}

void http_server_stop(HttpServer* server) {
    server->running = false;
    if (server->socket_fd >= 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
    }
}

bool http_server_is_running(HttpServer* server) {
    return server ? server->running : false;
}

void http_server_free(HttpServer* server) {
    if (server) {
        http_server_stop(server);
        
        for (int i = 0; i < server->route_count; i++) {
            free(server->routes[i].method);
            free(server->routes[i].path);
        }
        free(server->routes);
        
        if (server->static_root) {
            free(server->static_root);
        }
        if (server->static_prefix) {
            free(server->static_prefix);
        }
        
        // Free all static mounts
        for (int i = 0; i < server->static_mount_count; i++) {
            free(server->static_mounts[i].prefix);
            free(server->static_mounts[i].root);
        }
        free(server->static_mounts);

        // Free bypass prefixes
        if (server->static_bypass_prefixes) {
            for (int i = 0; i < server->static_bypass_count; i++) {
                free(server->static_bypass_prefixes[i]);
            }
            free(server->static_bypass_prefixes);
        }
        
        free(server);
    }
}

// Static file serving functions

void http_server_set_static_root(HttpServer* server, const char* root_path) {
    if (!server || !root_path) return;
    
    // Free existing static root
    if (server->static_root) {
        free(server->static_root);
    }
    
    server->static_root = strdup(root_path);
    server->serve_static = true;
    
    printf("[HTTP] Static file serving enabled from: %s\n", root_path);
}

void http_server_serve_static(HttpServer* server, const char* url_path) {
    if (!server || !url_path) return;
    
    // Enable static file serving for the specified URL path
    server->serve_static = true;
    
    printf("[HTTP] Static files will be served from URL path: %s\n", url_path);
}

void http_server_set_static_mount(HttpServer* server, const char* url_prefix, const char* root_path) {
    if (!server || !url_prefix || !root_path) return;
    
    // Add to dynamic mounts array (grows automatically)
    http_server_add_static_mount(server, url_prefix, root_path);
    
    // Also update legacy fields for backward compatibility
    if (server->static_root) free(server->static_root);
    server->static_root = strdup(root_path);
    if (server->static_prefix) free(server->static_prefix);
    server->static_prefix = strdup(url_prefix);
    server->serve_static = true;
    printf("[HTTP] Static mount %s -> %s\n", url_prefix, root_path);
}

// Additional secondary static mount (e.g., "/api/files" -> ".")
void http_server_add_static_mount(HttpServer* server, const char* url_prefix, const char* root_path) {
    if (!server || !url_prefix || !root_path) return;
    
    // Check if we need to grow the array (double capacity when full, max 128)
    if (server->static_mount_count >= server->static_mount_capacity) {
        int new_capacity = server->static_mount_capacity * 2;
        if (new_capacity > 128) new_capacity = 128;
        if (server->static_mount_count >= 128) {
            fprintf(stderr, "[HTTP] Static mount limit (128) reached, ignoring mount %s\n", url_prefix);
            return;
        }
        StaticMount* new_mounts = realloc(server->static_mounts, sizeof(StaticMount) * new_capacity);
        if (!new_mounts) {
            fprintf(stderr, "[HTTP] Failed to allocate memory for static mounts\n");
            return;
        }
        server->static_mounts = new_mounts;
        server->static_mount_capacity = new_capacity;
    }
    
    // Add the new mount
    server->static_mounts[server->static_mount_count].prefix = strdup(url_prefix);
    server->static_mounts[server->static_mount_count].root = strdup(root_path);
    server->static_mount_count++;
    server->serve_static = true;
    
    printf("[HTTP] Static mount %s -> %s (total: %d)\n", url_prefix, root_path, server->static_mount_count);
}

// Configure a URL prefix to bypass static handling and SPA fallback
void http_server_add_static_bypass_prefix(HttpServer* server, const char* url_prefix) {
    if (!server || !url_prefix) return;
    // Initialize array on first use
    if (server->static_bypass_capacity == 0) {
        server->static_bypass_capacity = 8;
        server->static_bypass_prefixes = (char**)malloc(sizeof(char*) * server->static_bypass_capacity);
        server->static_bypass_count = 0;
    }
    // Grow if needed
    if (server->static_bypass_count >= server->static_bypass_capacity) {
        int new_cap = server->static_bypass_capacity * 2;
        if (new_cap > 256) new_cap = 256; // reasonable guard
        char** new_arr = (char**)realloc(server->static_bypass_prefixes, sizeof(char*) * new_cap);
        if (!new_arr) return;
        server->static_bypass_prefixes = new_arr;
        server->static_bypass_capacity = new_cap;
    }
    server->static_bypass_prefixes[server->static_bypass_count++] = strdup(url_prefix);
    fprintf(stderr, "[HTTP] Static bypass prefix added: %s (total: %d)\n", url_prefix, server->static_bypass_count);
}

void http_server_clear_static_bypass_prefixes(HttpServer* server) {
    if (!server) return;
    if (server->static_bypass_prefixes) {
        for (int i = 0; i < server->static_bypass_count; i++) {
            free(server->static_bypass_prefixes[i]);
        }
        free(server->static_bypass_prefixes);
        server->static_bypass_prefixes = NULL;
    }
    server->static_bypass_count = 0;
    server->static_bypass_capacity = 0;
    fprintf(stderr, "[HTTP] Static bypass prefixes cleared\n");
}

bool http_server_serve_file(HttpServer* server, const char* file_path, int client_fd, HttpRequest* req) {
    if (!server || !file_path || client_fd < 0) {
        fprintf(stderr, "[HTTP] serve_file: invalid params (server=%p, path=%s, fd=%d)\n", 
                (void*)server, file_path ? file_path : "NULL", client_fd);
        return false;
    }
    
    // Security check
    if (!http_is_safe_path(file_path)) {
        fprintf(stderr, "[HTTP] serve_file: unsafe path: %s\n", file_path);
        // Do not write a response here; let the caller decide (enables SPA fallback)
        return false;
    }
    
    // Build full file path
    char full_path[1024];
    if (server->static_root) {
        snprintf(full_path, sizeof(full_path), "%s/%s", server->static_root, file_path);
    } else {
        strncpy(full_path, file_path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }
    
    fprintf(stderr, "[HTTP] serve_file: trying to serve %s\n", full_path);
    
    // Check if file exists and is readable
    struct stat file_stat;
    if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        fprintf(stderr, "[HTTP] serve_file: file not found or not regular: %s\n", full_path);
        // Do not write a response here; caller may perform SPA fallback
        return false;
    }
    
    // Open file
    FILE* file = fopen(full_path, "rb");
    if (!file) {
        fprintf(stderr, "[HTTP] serve_file: failed to open file: %s\n", full_path);
        // Defer error response to caller (avoid double headers)
        return false;
    }
    
    // Get MIME type
    const char* mime_type = http_get_mime_type(full_path);
    
    // Determine method and Range header
    int is_head = 0;
    const char* method = NULL;
    if (req && req->method) {
        method = req->method;
        if (strcasecmp(method, "HEAD") == 0) {
            is_head = 1;
        }
    }
    const char* range_hdr = req ? http_request_get_header(req, "Range") : NULL;
    long long total_size = (long long)file_stat.st_size;
    long long start = 0;
    long long end = total_size > 0 ? (total_size - 1) : 0;
    int use_range = 0;
    
    // Parse simple Range header: bytes=START-END | bytes=START- | bytes=-SUFFIX
    if (range_hdr && strncasecmp(range_hdr, "bytes=", 6) == 0) {
        const char* spec = range_hdr + 6;
        // Only support single range
        const char* comma = strchr(spec, ',');
        if (comma) {
            // Multiple ranges not supported -> respond with 200 ignoring Range per simplicity
        } else {
            const char* dash = strchr(spec, '-');
            if (dash) {
                if (dash == spec) {
                    // bytes=-SUFFIX
                    long long suffix = atoll(dash + 1);
                    if (suffix > 0) {
                        if (suffix >= total_size) {
                            start = 0;
                        } else {
                            start = total_size - suffix;
                        }
                        end = total_size > 0 ? (total_size - 1) : 0;
                        use_range = 1;
                    }
                } else {
                    // bytes=START- or bytes=START-END
                    long long s = atoll(spec);
                    if (*(dash + 1) == '\0') {
                        // open-ended
                        if (s >= 0 && s < total_size) {
                            start = s;
                            end = total_size > 0 ? (total_size - 1) : 0;
                            use_range = 1;
                        }
                    } else {
                        long long e = atoll(dash + 1);
                        if (s >= 0 && s < total_size && e >= s) {
                            if (e >= total_size) e = total_size - 1;
                            start = s;
                            end = e;
                            use_range = 1;
                        }
                    }
                }
            }
        }
    }
    
    // If invalid range (e.g., start >= total), return 416
    if (range_hdr && !use_range) {
        char headers[1024];
        int n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 416 Range Not Satisfiable\r\n"
            "Content-Range: bytes */%lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n",
            total_size);
        write(client_fd, headers, n);
        fclose(file);
        fprintf(stderr, "[HTTP] Invalid Range requested for %s: %s\n", full_path, range_hdr);
        return true;
    }
    
    // Build headers and optionally send body
    char headers[1024];
    if (use_range) {
        long long content_len = (end >= start) ? (end - start + 1) : 0;
        int n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Cache-Control: public, max-age=3600\r\n"
            "Connection: close\r\n"
            "\r\n",
            mime_type, content_len, start, end, total_size);
        write(client_fd, headers, n);
        
        if (!is_head && content_len > 0) {
            // Send only the requested range
            if (fseeko(file, (off_t)start, SEEK_SET) != 0) {
                // Fallback: read and discard until start
                long long skip = start;
                char discard[4096];
                while (skip > 0) {
                    size_t r = fread(discard, 1, (skip > (long long)sizeof(discard)) ? sizeof(discard) : (size_t)skip, file);
                    if (r == 0) break;
                    skip -= r;
                }
            }
            long long remaining = content_len;
            char buffer[8192];
            while (remaining > 0) {
                size_t to_read = (remaining > (long long)sizeof(buffer)) ? sizeof(buffer) : (size_t)remaining;
                size_t r = fread(buffer, 1, to_read, file);
                if (r == 0) break;
                write(client_fd, buffer, r);
                remaining -= r;
            }
        }
        fclose(file);
        printf("[HTTP] Served static file (range): %s (MIME: %s, Range: %lld-%lld/%lld)\n",
               full_path, mime_type, start, end, total_size);
        return true;
    } else {
        int n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: public, max-age=3600\r\n"
            "Connection: close\r\n"
            "\r\n",
            mime_type, total_size);
        write(client_fd, headers, n);
        
        if (!is_head) {
            // Send full file content
            char buffer[8192];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                write(client_fd, buffer, bytes_read);
            }
        }
        fclose(file);
        printf("[HTTP] Served static file: %s (MIME: %s, Size: %lld bytes)%s\n", 
               full_path, mime_type, total_size, is_head ? " [HEAD]" : "");
        return true;
    }
}