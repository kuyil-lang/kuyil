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

void http_response_free(HttpResponse* response) {
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

struct HttpServer {
    int port;
    bool running;
    int socket_fd;
    Route* routes;
    int route_count;
    int route_capacity;
    char* static_root;      // Root directory for static files
    bool serve_static;      // Enable static file serving
};

static void* handle_client(void* arg) {
    ClientHandlerArgs* args = (ClientHandlerArgs*)arg;
    int client_fd = args->client_fd;
    HttpServer* server = args->server;
    free(arg);
    
    char buffer[4096];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        // Parse HTTP request line
        char method[16], path[512];
        sscanf(buffer, "%s %s", method, path);
        
        // URL decode path (basic implementation)
        char decoded_path[512];
        int j = 0;
        for (int i = 0; path[i] && j < sizeof(decoded_path) - 1; i++) {
            if (path[i] == '%' && path[i+1] && path[i+2]) {
                // Simple hex decode
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
        
        // Remove query string if present
        char* query_start = strchr(decoded_path, '?');
        if (query_start) {
            *query_start = '\0';
        }
        
        // Check if this is a static file request (GET method and server has static serving enabled)
        if (strcmp(method, "GET") == 0 && server->serve_static) {
            // Remove leading slash for file path
            const char* file_path = decoded_path;
            if (file_path[0] == '/') {
                file_path++;
            }
            
            // If path is empty or ends with /, serve index.html
            char index_path[512];
            if (strlen(file_path) == 0 || file_path[strlen(file_path)-1] == '/') {
                snprintf(index_path, sizeof(index_path), "%sindex.html", file_path);
                file_path = index_path;
            }
            
            // Try to serve static file
            if (http_server_serve_file(server, file_path, client_fd)) {
                close(client_fd);
                return NULL;
            }
        }
        
        // Check for route matches
        bool route_found = false;
        for (int i = 0; i < server->route_count; i++) {
            Route* route = &server->routes[i];
            if (strcmp(method, route->method) == 0 && strcmp(decoded_path, route->path) == 0) {
                // Route found - for now just send a success response
                const char* response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: 33\r\n"
                    "\r\n"
                    "{\"message\": \"Route handler found\"}";
                
                write(client_fd, response, strlen(response));
                route_found = true;
                break;
            }
        }
        
        // If no route found, send 404
        if (!route_found) {
            const char* error_404 = 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 27\r\n"
                "\r\n"
                "{\"error\": \"Route not found\"}";
            
            write(client_fd, error_404, strlen(error_404));
        }
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
    
    return server;
}

void http_server_get(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        server->route_capacity = server->route_capacity == 0 ? 8 : server->route_capacity * 2;
        server->routes = realloc(server->routes, sizeof(Route) * server->route_capacity);
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("GET");
    route->path = strdup(path);
    route->handler = handler;
}

void http_server_post(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        server->route_capacity = server->route_capacity == 0 ? 8 : server->route_capacity * 2;
        server->routes = realloc(server->routes, sizeof(Route) * server->route_capacity);
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("POST");
    route->path = strdup(path);
    route->handler = handler;
}

void http_server_put(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        server->route_capacity = server->route_capacity == 0 ? 8 : server->route_capacity * 2;
        server->routes = realloc(server->routes, sizeof(Route) * server->route_capacity);
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("PUT");
    route->path = strdup(path);
    route->handler = handler;
}

void http_server_delete(HttpServer* server, const char* path, void* handler) {
    if (server->route_count >= server->route_capacity) {
        server->route_capacity = server->route_capacity == 0 ? 8 : server->route_capacity * 2;
        server->routes = realloc(server->routes, sizeof(Route) * server->route_capacity);
    }
    
    Route* route = &server->routes[server->route_count++];
    route->method = strdup("DELETE");
    route->path = strdup(path);
    route->handler = handler;
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

bool http_server_serve_file(HttpServer* server, const char* file_path, int client_fd) {
    if (!server || !file_path || client_fd < 0) return false;
    
    // Security check
    if (!http_is_safe_path(file_path)) {
        const char* error_403 = "HTTP/1.1 403 Forbidden\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 9\r\n"
                                "\r\n"
                                "Forbidden";
        write(client_fd, error_403, strlen(error_403));
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
    
    // Check if file exists and is readable
    struct stat file_stat;
    if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        const char* error_404 = "HTTP/1.1 404 Not Found\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 9\r\n"
                                "\r\n"
                                "Not Found";
        write(client_fd, error_404, strlen(error_404));
        return false;
    }
    
    // Open file
    FILE* file = fopen(full_path, "rb");
    if (!file) {
        const char* error_500 = "HTTP/1.1 500 Internal Server Error\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 21\r\n"
                                "\r\n"
                                "Internal Server Error";
        write(client_fd, error_500, strlen(error_500));
        return false;
    }
    
    // Get MIME type
    const char* mime_type = http_get_mime_type(full_path);
    
    // Send HTTP headers
    char headers[1024];
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Cache-Control: public, max-age=3600\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_type, file_stat.st_size);
    
    write(client_fd, headers, strlen(headers));
    
    // Send file content
    char buffer[8192];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        write(client_fd, buffer, bytes_read);
    }
    
    fclose(file);
    printf("[HTTP] Served static file: %s (MIME: %s, Size: %ld bytes)\n", 
           full_path, mime_type, file_stat.st_size);
    
    return true;
}