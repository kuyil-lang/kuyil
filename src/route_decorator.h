// Route Decorator System for Kuyil
// Implements @route decorator for HTTP routing with async/sync support

#ifndef ROUTE_DECORATOR_H
#define ROUTE_DECORATOR_H

#include "vm.h"
#include "bytecode.h"
#include <stdbool.h>
#include <pthread.h>

// Route entry structure
typedef struct {
    char* method;           // GET, POST, PUT, DELETE, PATCH, etc.
    char* path;             // /users/:id or /api/data
    char* handler_name;     // Function name
    Function* handler_func; // Cached function pointer
    bool is_async;          // Whether handler is async
    bool bridge_enabled;    // Enable webview bridge access
    char** middleware;      // Middleware function names
    int middleware_count;
} RouteEntry;

// Route registry
typedef struct {
    RouteEntry* routes;
    int count;
    int capacity;
    pthread_mutex_t lock;
} RouteRegistry;

// Global route registry
RouteRegistry* route_registry_create();
void route_registry_destroy(RouteRegistry* registry);
RouteRegistry* get_global_registry();

// Route registration (called by @route decorator)
bool route_register(RouteRegistry* registry, const char* method, const char* path,
                   const char* handler_name, bool is_async, bool bridge_enabled);

// Route lookup
RouteEntry* route_find(RouteRegistry* registry, const char* method, const char* path);
RouteEntry** route_list_all(RouteRegistry* registry, int* out_count);

// Middleware support
bool route_add_middleware(RouteRegistry* registry, const char* method, const char* path,
                         const char* middleware_name);

// Path parameter extraction
typedef struct {
    char** keys;
    char** values;
    int count;
} RouteParams;

RouteParams* route_extract_params(const char* pattern, const char* path);
void route_params_free(RouteParams* params);

// Bridge call handler (for webview integration)
Value route_bridge_call(RouteRegistry* registry, const char* route_name, 
                       Value* args, int arg_count, VM* vm);

// Built-in functions for VM
Value builtin_route_register(int arg_count, Value* args);
Value builtin_route_find(int arg_count, Value* args);
Value builtin_route_list(int arg_count, Value* args);
Value builtin_route_extract_params(int arg_count, Value* args);

#endif // ROUTE_DECORATOR_H
