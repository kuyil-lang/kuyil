#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>
#include <time.h>
#include "../kuyil_types.h"
#include <curl/curl.h>

typedef struct {
    char* data;
    size_t size;
} HttpResponse;

// Forward declaration for opaque type
typedef struct HttpServer HttpServer;

// Route structure  
typedef struct {
    char* method;
    char* path;
    void* handler;
} Route;

// HTTP Client functions
HttpResponse* http_get(const char* url);
HttpResponse* http_post(const char* url, const char* data);
HttpResponse* http_put(const char* url, const char* data);
HttpResponse* http_delete(const char* url);
void http_response_free(HttpResponse* response);

// HTTP Server functions
HttpServer* http_server_create(int port);
void http_server_get(HttpServer* server, const char* path, void* handler);
void http_server_post(HttpServer* server, const char* path, void* handler);
void http_server_put(HttpServer* server, const char* path, void* handler);
void http_server_delete(HttpServer* server, const char* path, void* handler);
void http_server_listen(HttpServer* server);
void http_server_stop(HttpServer* server);
void http_server_free(HttpServer* server);

// Static file serving functions
void http_server_set_static_root(HttpServer* server, const char* root_path);
void http_server_serve_static(HttpServer* server, const char* url_path);
bool http_server_serve_file(HttpServer* server, const char* file_path, int client_fd);
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