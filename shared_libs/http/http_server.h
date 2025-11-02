#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>
#include <time.h>
#include "../kuyil_types.h"

#ifdef _WIN32
    // Windows doesn't need curl headers for basic HTTP (using WinHTTP instead)
    // or we can skip curl entirely for Windows builds
    #ifndef CURL_H_
        typedef void CURL;
        typedef int CURLcode;
    #endif
#else
    #include <curl/curl.h>
#endif

typedef struct {
    char* data;
    size_t size;
} HttpResponse;

// Forward declarations
typedef struct HttpServer HttpServer;
typedef struct HttpRequest HttpRequest;
typedef struct HttpResponseBuilder HttpResponseBuilder;

// HTTP Request structure with full request details
struct HttpRequest {
    char* method;           // GET, POST, PUT, DELETE, etc.
    char* path;             // URL path
    char* query_string;     // Query parameters
    char* body;             // Request body
    size_t body_length;     // Length of body
    char** header_names;    // Header names
    char** header_values;   // Header values
    int header_count;       // Number of headers
    char** param_names;     // URL path parameters (e.g., :id)
    char** param_values;    // URL path parameter values
    int param_count;        // Number of parameters
};

// HTTP Response Builder structure
struct HttpResponseBuilder {
    int status_code;        // HTTP status code (200, 404, etc.)
    char* body;             // Response body
    size_t body_length;     // Length of response body
    char** header_names;    // Header names
    char** header_values;   // Header values
    int header_count;       // Number of headers
    char* content_type;     // Content-Type header
};

// Callback function signature for route handlers
// Takes HttpRequest* and HttpResponseBuilder*, returns void
typedef void (*RouteHandlerFunc)(HttpRequest* req, HttpResponseBuilder* res, void* user_data);

// Route structure with callback support
typedef struct {
    char* method;
    char* path;
    void* handler;                  // Legacy handler (unused)
    RouteHandlerFunc callback;      // New callback-based handler
    void* user_data;                // User data passed to callback (Kuyil Value/function)
} Route;

// HTTP Client functions
HttpResponse* http_get(const char* url);
HttpResponse* http_post(const char* url, const char* data);
HttpResponse* http_put(const char* url, const char* data);
HttpResponse* http_delete(const char* url);
void http_client_response_free(HttpResponse* response);

// HTTP Server functions
HttpServer* http_server_create(int port);
void http_server_get(HttpServer* server, const char* path, void* handler);
void http_server_post(HttpServer* server, const char* path, void* handler);
void http_server_put(HttpServer* server, const char* path, void* handler);
void http_server_delete(HttpServer* server, const char* path, void* handler);

// New callback-based route registration
void http_server_register_route(HttpServer* server, const char* method, const char* path, 
                                 RouteHandlerFunc callback, void* user_data);

void http_server_listen(HttpServer* server);
void http_server_stop(HttpServer* server);
void http_server_free(HttpServer* server);

// HTTP Request/Response helper functions
HttpRequest* http_request_create();
void http_request_free(HttpRequest* req);
void http_request_add_header(HttpRequest* req, const char* name, const char* value);
void http_request_add_param(HttpRequest* req, const char* name, const char* value);
const char* http_request_get_header(HttpRequest* req, const char* name);
const char* http_request_get_param(HttpRequest* req, const char* name);

HttpResponseBuilder* http_response_create();
void http_response_builder_free(HttpResponseBuilder* res);
void http_response_set_status(HttpResponseBuilder* res, int status_code);
void http_response_set_body(HttpResponseBuilder* res, const char* body, size_t length);
void http_response_set_json(HttpResponseBuilder* res, const char* json);
void http_response_add_header(HttpResponseBuilder* res, const char* name, const char* value);
void http_response_set_content_type(HttpResponseBuilder* res, const char* content_type);
char* http_response_build(HttpResponseBuilder* res, size_t* total_length);

// Static file serving functions
void http_server_set_static_root(HttpServer* server, const char* root_path);
void http_server_serve_static(HttpServer* server, const char* url_path);
// Mount static files under a URL prefix (e.g., "/uploads") served from a filesystem root
void http_server_set_static_mount(HttpServer* server, const char* url_prefix, const char* root_path);
// Additional secondary static mount (e.g., "/api/files" -> ".")
void http_server_add_static_mount(HttpServer* server, const char* url_prefix, const char* root_path);
// Configure URL prefixes that must bypass static serving and SPA fallback (e.g., "/api").
// Any request path starting with one of these prefixes will be routed only through dynamic routes.
void http_server_add_static_bypass_prefix(HttpServer* server, const char* url_prefix);
// Clear all configured static bypass prefixes
void http_server_clear_static_bypass_prefixes(HttpServer* server);
// Serve a static file with support for GET/HEAD and Range requests.
// Returns true if a response was written (including 200/206/404/416),
// false if the caller should attempt other handling (e.g., SPA fallback).
bool http_server_serve_file(HttpServer* server, const char* file_path, int client_fd, HttpRequest* req);
const char* http_get_mime_type(const char* file_path);
bool http_is_safe_path(const char* path);

// Initialize HTTP subsystem
void http_init();
void http_cleanup();

// Development Helper Services
typedef struct {
    char** watch_files;
    int watch_count;
    int watch_capacity;
    time_t* last_modified;
    bool auto_reload;
    HttpServer* server;
    void (*reload_callback)(void);
} DevHelper;

DevHelper* dev_helper_create(HttpServer* server);
void dev_helper_watch_file(DevHelper* helper, const char* filepath);
void dev_helper_watch_directory(DevHelper* helper, const char* dirpath);
bool dev_helper_check_changes(DevHelper* helper);
void dev_helper_start_watching(DevHelper* helper);
void dev_helper_stop_watching(DevHelper* helper);
void dev_helper_set_reload_callback(DevHelper* helper, void (*callback)(void));
void dev_helper_free(DevHelper* helper);

#endif