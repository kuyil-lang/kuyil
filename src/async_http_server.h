// Async HTTP Server with Route Integration
// Integrates route_find, route_extract_params with async HTTP handling

#ifndef ASYNC_HTTP_SERVER_H
#define ASYNC_HTTP_SERVER_H

#include "vm.h"
#include "bytecode.h"
#include <stdbool.h>
#include <pthread.h>

// Multipart form field
typedef struct {
    char* name;
    char* filename;      // NULL if not a file
    char* content_type;  // NULL if not specified
    char* data;
    size_t data_length;
} MultipartField;

// Multipart form data
typedef struct {
    MultipartField* fields;
    int field_count;
} MultipartFormData;

// HTTP request structure
typedef struct {
    char* method;
    char* path;
    char* query_string;
    char* body;
    char** header_keys;
    char** header_values;
    int header_count;
    MultipartFormData* multipart;  // NULL if not multipart
} HttpRequest;

// HTTP response structure
typedef struct {
    int status_code;
    char* body;
    char** header_keys;
    char** header_values;
    int header_count;
} HttpResponse;

// Async HTTP server
typedef struct AsyncHttpServer AsyncHttpServer;

// Create async HTTP server
AsyncHttpServer* async_http_server_create(int port);

// Destroy async HTTP server
void async_http_server_destroy(AsyncHttpServer* server);

// Start server (non-blocking, runs in background thread)
bool async_http_server_start(AsyncHttpServer* server);

// Stop server
void async_http_server_stop(AsyncHttpServer* server);

// Set VM for handler callbacks
void async_http_server_set_vm(AsyncHttpServer* server, VM* vm);

// Built-in function: start_server(port)
Value builtin_http_start_server(int arg_count, Value* args);

// Built-in function: stop_server()
Value builtin_http_stop_server(int arg_count, Value* args);

// Get global server instance
AsyncHttpServer* async_http_server_get_global();

// Configure maximum body size (default: 10MB)
void async_http_server_set_max_body_size(AsyncHttpServer* server, size_t max_size);

// Get current maximum body size
size_t async_http_server_get_max_body_size(AsyncHttpServer* server);

#endif // ASYNC_HTTP_SERVER_H
