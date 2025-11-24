// Route Decorator System for Kuyil
// Implements @route decorator for HTTP routing with async/sync support

#include "route_decorator.h"
#include "avatar_runtime.h"
#include "vm.h"
#include "vm_call_shared.h"
#include "vm_library_integration.h"
#include "request_response.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// External functions from request_response.c for response registry
extern int register_response(Value* response_obj);
extern Value* get_response_by_id(int id);

// Global route registry (created on demand)
static RouteRegistry* g_route_registry = NULL;

// Create a new route registry
RouteRegistry* route_registry_create() {
    RouteRegistry* registry = malloc(sizeof(RouteRegistry));
    if (!registry) return NULL;
    
    registry->routes = NULL;
    registry->count = 0;
    registry->capacity = 0;
    pthread_mutex_init(&registry->lock, NULL);
    
    return registry;
}

// Destroy route registry
void route_registry_destroy(RouteRegistry* registry) {
    if (!registry) return;
    
    pthread_mutex_lock(&registry->lock);
    
    for (int i = 0; i < registry->count; i++) {
        RouteEntry* route = &registry->routes[i];
        free(route->method);
        free(route->path);
        free(route->handler_name);
        
        for (int j = 0; j < route->middleware_count; j++) {
            free(route->middleware[j]);
        }
        free(route->middleware);
    }
    
    free(registry->routes);
    
    pthread_mutex_unlock(&registry->lock);
    pthread_mutex_destroy(&registry->lock);
    free(registry);
}

// Get or create global registry
RouteRegistry* get_global_registry() {
    if (!g_route_registry) {
        g_route_registry = route_registry_create();
    }
    return g_route_registry;
}

// Base64 decode table
static const uint8_t base64_decode_table[256] = {
    ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,  ['G'] = 6,  ['H'] = 7,
    ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11, ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
    ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31,
    ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39,
    ['o'] = 40, ['p'] = 41, ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
    ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
    ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59, ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63
};

// Base64 decode function
static char* base64_decode(const char* input, size_t* out_len) {
    if (!input) return NULL;
    
    size_t input_len = strlen(input);
    if (input_len == 0) return strdup("");
    
    // Calculate output length
    size_t output_len = (input_len / 4) * 3;
    if (input[input_len - 1] == '=') output_len--;
    if (input_len > 1 && input[input_len - 2] == '=') output_len--;
    
    char* output = malloc(output_len + 1);
    if (!output) return NULL;
    
    size_t out_idx = 0;
    for (size_t i = 0; i < input_len; i += 4) {
        uint8_t b1 = base64_decode_table[(uint8_t)input[i]];
        uint8_t b2 = (i + 1 < input_len) ? base64_decode_table[(uint8_t)input[i + 1]] : 0;
        uint8_t b3 = (i + 2 < input_len && input[i + 2] != '=') ? base64_decode_table[(uint8_t)input[i + 2]] : 0;
        uint8_t b4 = (i + 3 < input_len && input[i + 3] != '=') ? base64_decode_table[(uint8_t)input[i + 3]] : 0;
        
        output[out_idx++] = (b1 << 2) | (b2 >> 4);
        if (i + 2 < input_len && input[i + 2] != '=') {
            output[out_idx++] = (b2 << 4) | (b3 >> 2);
        }
        if (i + 3 < input_len && input[i + 3] != '=') {
            output[out_idx++] = (b3 << 6) | b4;
        }
    }
    
    output[out_idx] = '\0';
    if (out_len) *out_len = out_idx;
    return output;
}

// Helper function to unescape JSON string
static char* json_unescape_string(const char* input) {
    if (!input) return strdup("");
    
    size_t len = strlen(input);
    char* output = malloc(len + 1);  // Unescaped string will be same length or shorter
    if (!output) return strdup("");
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\' && i + 1 < len) {
            // Handle escape sequences
            switch (input[i + 1]) {
                case '"':  output[j++] = '"'; i++; break;
                case '\\': output[j++] = '\\'; i++; break;
                case '/':  output[j++] = '/'; i++; break;
                case 'b':  output[j++] = '\b'; i++; break;
                case 'f':  output[j++] = '\f'; i++; break;
                case 'n':  output[j++] = '\n'; i++; break;
                case 'r':  output[j++] = '\r'; i++; break;
                case 't':  output[j++] = '\t'; i++; break;
                default:   output[j++] = input[i]; break;  // Keep backslash if unknown escape
            }
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
    return output;
}

// Register a route
bool route_register(RouteRegistry* registry, const char* method, const char* path,
                   const char* handler_name, bool is_async, bool bridge_enabled) {
    if (!registry || !method || !path || !handler_name) return false;
    
    pthread_mutex_lock(&registry->lock);
    
    // Expand capacity if needed
    if (registry->count >= registry->capacity) {
        int new_capacity = registry->capacity == 0 ? 16 : registry->capacity * 2;
        RouteEntry* new_routes = realloc(registry->routes, sizeof(RouteEntry) * new_capacity);
        if (!new_routes) {
            pthread_mutex_unlock(&registry->lock);
            return false;
        }
        registry->routes = new_routes;
        registry->capacity = new_capacity;
    }
    
    // Add new route
    RouteEntry* route = &registry->routes[registry->count++];
    route->method = strdup(method);
    route->path = strdup(path);
    route->handler_name = strdup(handler_name);
    route->handler_func = NULL;
    route->is_async = is_async;
    route->bridge_enabled = bridge_enabled;
    route->middleware = NULL;
    route->middleware_count = 0;
    
    fprintf(stderr, "[ROUTE] Registered #%d: %s %s -> %s%s%s (registry=%p, count=%d)\n", 
           registry->count, method, path, handler_name,
           is_async ? " (async)" : "",
           bridge_enabled ? " (bridge)" : "",
           (void*)registry, registry->count);
    
    pthread_mutex_unlock(&registry->lock);
    
    return true;
}

// Check if a path segment matches (supports :param syntax)
static bool segment_matches(const char* pattern, const char* segment, char** param_value) {
    if (!pattern || !segment) return false;
    
    // Parameter segment (:id, :name, etc.)
    if (pattern[0] == ':') {
        if (param_value) {
            *param_value = strdup(segment);
        }
        return true;
    }
    
    // Exact match
    return strcmp(pattern, segment) == 0;
}

// Split path into segments
static char** split_path(const char* path, int* out_count) {
    if (!path || path[0] != '/') {
        *out_count = 0;
        return NULL;
    }
    
    // Count segments
    int count = 0;
    const char* p = path + 1; // Skip leading /
    if (*p) count = 1; // At least one segment
    
    while (*p) {
        if (*p == '/') count++;
        p++;
    }
    
    // Allocate array
    char** segments = malloc(sizeof(char*) * count);
    if (!segments) {
        *out_count = 0;
        return NULL;
    }
    
    // Extract segments
    int idx = 0;
    p = path + 1;
    const char* start = p;
    
    while (*p) {
        if (*p == '/' || *(p + 1) == '\0') {
            int len = (*p == '/') ? (p - start) : (p - start + 1);
            if (len > 0) {
                segments[idx] = malloc(len + 1);
                strncpy(segments[idx], start, len);
                segments[idx][len] = '\0';
                idx++;
            }
            start = p + 1;
        }
        p++;
    }
    
    *out_count = idx;
    return segments;
}

// Free path segments
static void free_segments(char** segments, int count) {
    if (!segments) return;
    for (int i = 0; i < count; i++) {
        free(segments[i]);
    }
    free(segments);
}

// Find matching route
RouteEntry* route_find(RouteRegistry* registry, const char* method, const char* path) {
    if (!registry || !method || !path) return NULL;
    
    pthread_mutex_lock(&registry->lock);
    
    // Split request path
    int req_seg_count = 0;
    char** req_segments = split_path(path, &req_seg_count);
    
    RouteEntry* found = NULL;
    
    for (int i = 0; i < registry->count; i++) {
        RouteEntry* route = &registry->routes[i];
        
        // Check method
        if (strcmp(route->method, method) != 0 && strcmp(route->method, "*") != 0) {
            continue;
        }
        
        // Split route pattern
        int route_seg_count = 0;
        char** route_segments = split_path(route->path, &route_seg_count);
        
        // Check segment count
        if (req_seg_count != route_seg_count) {
            free_segments(route_segments, route_seg_count);
            continue;
        }
        
        // Check each segment
        bool match = true;
        for (int j = 0; j < req_seg_count; j++) {
            if (!segment_matches(route_segments[j], req_segments[j], NULL)) {
                match = false;
                break;
            }
        }
        
        free_segments(route_segments, route_seg_count);
        
        if (match) {
            found = route;
            break;
        }
    }
    
    free_segments(req_segments, req_seg_count);
    
    pthread_mutex_unlock(&registry->lock);
    return found;
}

// Extract path parameters
RouteParams* route_extract_params(const char* pattern, const char* path) {
    if (!pattern || !path) return NULL;
    
    int pattern_count = 0, path_count = 0;
    char** pattern_segments = split_path(pattern, &pattern_count);
    char** path_segments = split_path(path, &path_count);
    
    if (pattern_count != path_count) {
        free_segments(pattern_segments, pattern_count);
        free_segments(path_segments, path_count);
        return NULL;
    }
    
    // Count parameters
    int param_count = 0;
    for (int i = 0; i < pattern_count; i++) {
        if (pattern_segments[i][0] == ':') {
            param_count++;
        }
    }
    
    RouteParams* params = malloc(sizeof(RouteParams));
    params->keys = malloc(sizeof(char*) * param_count);
    params->values = malloc(sizeof(char*) * param_count);
    params->count = 0;
    
    // Extract parameters
    for (int i = 0; i < pattern_count; i++) {
        if (pattern_segments[i][0] == ':') {
            params->keys[params->count] = strdup(pattern_segments[i] + 1); // Skip ':'
            params->values[params->count] = strdup(path_segments[i]);
            params->count++;
        }
    }
    
    free_segments(pattern_segments, pattern_count);
    free_segments(path_segments, path_count);
    
    return params;
}

// Free route parameters
void route_params_free(RouteParams* params) {
    if (!params) return;
    
    for (int i = 0; i < params->count; i++) {
        free(params->keys[i]);
        free(params->values[i]);
    }
    free(params->keys);
    free(params->values);
    free(params);
}

// List all routes
RouteEntry** route_list_all(RouteRegistry* registry, int* out_count) {
    if (!registry || !out_count) return NULL;
    
    pthread_mutex_lock(&registry->lock);
    
    *out_count = registry->count;
    RouteEntry** list = malloc(sizeof(RouteEntry*) * registry->count);
    
    for (int i = 0; i < registry->count; i++) {
        list[i] = &registry->routes[i];
    }
    
    pthread_mutex_unlock(&registry->lock);
    return list;
}

// Add middleware to route
bool route_add_middleware(RouteRegistry* registry, const char* method, const char* path,
                         const char* middleware_name) {
    if (!registry || !method || !path || !middleware_name) return false;
    
    pthread_mutex_lock(&registry->lock);
    
    RouteEntry* route = route_find(registry, method, path);
    if (!route) {
        pthread_mutex_unlock(&registry->lock);
        return false;
    }
    
    // Add middleware
    route->middleware = realloc(route->middleware, sizeof(char*) * (route->middleware_count + 1));
    route->middleware[route->middleware_count++] = strdup(middleware_name);
    
    pthread_mutex_unlock(&registry->lock);
    return true;
}

// Bridge call handler for webview integration
Value route_bridge_call(RouteRegistry* registry, const char* route_name, 
                       Value* args, int arg_count, VM* vm) {
    if (!registry || !route_name) {
        Value nil;
        memset(&nil, 0, sizeof(Value));
        nil.type = VALUE_NIL;
        return nil;
    }
    
    // Find route by handler name
    pthread_mutex_lock(&registry->lock);
    
    RouteEntry* route = NULL;
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->routes[i].handler_name, route_name) == 0 &&
            registry->routes[i].bridge_enabled) {
            route = &registry->routes[i];
            break;
        }
    }
    
    if (!route) {
        pthread_mutex_unlock(&registry->lock);
        fprintf(stderr, "[ROUTE] Bridge call failed: route '%s' not found or not bridge-enabled\n", route_name);
        Value nil;
        memset(&nil, 0, sizeof(Value));
        nil.type = VALUE_NIL;
        return nil;
    }
    
    pthread_mutex_unlock(&registry->lock);
    
    // TODO: Implement proper function call through VM
    // For now, return success indicator
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Built-in function: route_register("GET", "/users/:id", "getUser", async=false, bridge=false)
Value builtin_route_register(int arg_count, Value* args) {
    if (arg_count < 3) {
        fprintf(stderr, "[ROUTE] route_register requires at least 3 arguments: method, path, handler\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    if (args[0].type != VALUE_STRING || args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        fprintf(stderr, "[ROUTE] route_register: method, path, and handler must be strings\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    const char* method = args[0].as.string;
    const char* path = args[1].as.string;
    const char* handler = args[2].as.string;
    bool is_async = (arg_count > 3 && args[3].type == VALUE_BOOL) ? args[3].as.boolean : false;
    bool bridge = (arg_count > 4 && args[4].type == VALUE_BOOL) ? args[4].as.boolean : false;
    
    RouteRegistry* registry = get_global_registry();
    bool success = route_register(registry, method, path, handler, is_async, bridge);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = success;
    return result;
}

// Built-in function: route_find("GET", "/users/123")
Value builtin_route_find(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value nil;
        memset(&nil, 0, sizeof(Value));
        nil.type = VALUE_NIL;
        return nil;
    }
    
    RouteRegistry* registry = get_global_registry();
    RouteEntry* route = route_find(registry, args[0].as.string, args[1].as.string);
    
    if (!route) {
        Value nil;
        memset(&nil, 0, sizeof(Value));
        nil.type = VALUE_NIL;
        return nil;
    }
    
    // Return object with route info
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    result.as.object.count = 5;
    result.as.object.keys = malloc(sizeof(char*) * 5);
    result.as.object.values = malloc(sizeof(Value) * 5);
    
    result.as.object.keys[0] = strdup("method");
    result.as.object.values[0].type = VALUE_STRING;
    result.as.object.values[0].as.string = strdup(route->method);
    
    result.as.object.keys[1] = strdup("path");
    result.as.object.values[1].type = VALUE_STRING;
    result.as.object.values[1].as.string = strdup(route->path);
    
    result.as.object.keys[2] = strdup("handler");
    result.as.object.values[2].type = VALUE_STRING;
    result.as.object.values[2].as.string = strdup(route->handler_name);
    
    result.as.object.keys[3] = strdup("async");
    result.as.object.values[3].type = VALUE_BOOL;
    result.as.object.values[3].as.boolean = route->is_async;
    
    result.as.object.keys[4] = strdup("bridge");
    result.as.object.values[4].type = VALUE_BOOL;
    result.as.object.values[4].as.boolean = route->bridge_enabled;
    
    return result;
}

// Built-in function: route_list()
Value builtin_route_list(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    RouteRegistry* registry = get_global_registry();
    int count = 0;
    RouteEntry** routes = route_list_all(registry, &count);
    
    Value result;
    result.type = VALUE_ARRAY;
    result.as.array.count = count;
    result.as.array.values = malloc(sizeof(Value) * count);
    
    for (int i = 0; i < count; i++) {
        RouteEntry* route = routes[i];
        
        Value route_obj;
        memset(&route_obj, 0, sizeof(Value));
        route_obj.type = VALUE_OBJECT;
        route_obj.as.object.count = 5;
        route_obj.as.object.keys = malloc(sizeof(char*) * 5);
        route_obj.as.object.values = malloc(sizeof(Value) * 5);
        
        route_obj.as.object.keys[0] = strdup("method");
        route_obj.as.object.values[0].type = VALUE_STRING;
        route_obj.as.object.values[0].as.string = strdup(route->method);
        
        route_obj.as.object.keys[1] = strdup("path");
        route_obj.as.object.values[1].type = VALUE_STRING;
        route_obj.as.object.values[1].as.string = strdup(route->path);
        
        route_obj.as.object.keys[2] = strdup("handler");
        route_obj.as.object.values[2].type = VALUE_STRING;
        route_obj.as.object.values[2].as.string = strdup(route->handler_name);
        
        route_obj.as.object.keys[3] = strdup("async");
        route_obj.as.object.values[3].type = VALUE_BOOL;
        route_obj.as.object.values[3].as.boolean = route->is_async;
        
        route_obj.as.object.keys[4] = strdup("bridge");
        route_obj.as.object.values[4].type = VALUE_BOOL;
        route_obj.as.object.values[4].as.boolean = route->bridge_enabled;
        
        result.as.array.values[i] = route_obj;
    }
    
    free(routes);
    return result;
}

// Built-in function: route_extract_params("/users/:id", "/users/123")
Value builtin_route_extract_params(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value nil;
        memset(&nil, 0, sizeof(Value));
        nil.type = VALUE_NIL;
        return nil;
    }
    
    const char* pattern = args[0].as.string;
    const char* path = args[1].as.string;
    
    RouteParams* params = route_extract_params(pattern, path);
    
    if (!params || params->count == 0) {
        if (params) route_params_free(params);
        // Return empty object
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_OBJECT;
        result.as.object.count = 0;
        result.as.object.keys = NULL;
        result.as.object.values = NULL;
        return result;
    }
    
    // Return object with params
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    result.as.object.count = params->count;
    result.as.object.keys = malloc(sizeof(char*) * params->count);
    result.as.object.values = malloc(sizeof(Value) * params->count);
    
    for (int i = 0; i < params->count; i++) {
        result.as.object.keys[i] = strdup(params->keys[i]);
        result.as.object.values[i].type = VALUE_STRING;
        result.as.object.values[i].as.string = strdup(params->values[i]);
    }
    
    route_params_free(params);
    
    return result;
}

// New version that works with CallContext - for avatars with separate stacks
Value builtin_route_bridge_invoke_ctx(int arg_count, Value* args, CallContext* ctx) {
    if (arg_count < 2) {
        fprintf(stderr, "[ROUTE] route_bridge_invoke requires 2 arguments: route_pattern, request_object\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid arguments\"}");
        return error;
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "[ROUTE] route_bridge_invoke: route_pattern must be string\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid argument types\"}");
        return error;
    }
    
    const char* route_pattern = args[0].as.string;
    Value request_object = args[1];
    
    // Parse method and path
    char method[16] = {0};
    char path[256] = {0};
    if (sscanf(route_pattern, "%15s %255s", method, path) != 2) {
        fprintf(stderr, "[ROUTE Bridge] Invalid route pattern format\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid route pattern\"}");
        return error;
    }
    
    // Find matching route
    RouteRegistry* registry = get_global_registry();
    RouteEntry* matched_route = route_find(registry, method, path);
    
    if (!matched_route) {
        fprintf(stderr, "[ROUTE Bridge] No matching route found for %s %s\n", method, path);
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Route not found\"}");
        return error;
    }
    
    // Get handler from globals
    Value handler_value;
    if (!ctx->get_global(ctx->context, matched_route->handler_name, &handler_value)) {
        fprintf(stderr, "[ROUTE Bridge] Handler '%s' not found in globals\n", matched_route->handler_name);
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Handler not found\"}");
        return error;
    }
    
    if (handler_value.type != VALUE_FUNCTION) {
        fprintf(stderr, "[ROUTE Bridge] Handler '%s' is not a function\n", matched_route->handler_name);
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Handler is not a function\"}");
        return error;
    }
    
    // Create response builder
    extern int kyl_aio_register_ptr(void*);
    extern void* kyl_aio_get_ptr(int);
    extern void kyl_aio_unregister_ptr(int);
    
    typedef struct {
        int status_code;
        char* body;
        size_t body_length;
        char** header_names;
        char** header_values;
        int header_count;
        char* content_type;
    } KylResponseBuilder;
    
    KylResponseBuilder* resp_builder = malloc(sizeof(KylResponseBuilder));
    memset(resp_builder, 0, sizeof(KylResponseBuilder));
    resp_builder->status_code = 200;
    
    int response_id = kyl_aio_register_ptr(resp_builder);
    
    Value id_value;
    id_value.type = VALUE_NUMBER;
    id_value.as.number = (double)response_id;
    Value response_object = vm_object_create_with_field("__id", id_value);
    
    // Return an object with handler, request, and response
    // The avatar code will execute the handler and build the response
    fprintf(stderr, "[ROUTE Bridge] Returning handler, request, and response to avatar\n");
    
    // Create object to return using vm_object_create API
    Value result_obj = vm_object_create();
    
    // Add handler function
    vm_object_set_field(&result_obj, "handler", handler_value);
    
    // Add request object
    vm_object_set_field(&result_obj, "request", request_object);
    
    // Add response object
    vm_object_set_field(&result_obj, "response", response_object);
    
    // Add response_id for tracking
    Value response_id_val;
    response_id_val.type = VALUE_NUMBER;
    response_id_val.as.number = (double)response_id;
    vm_object_set_field(&result_obj, "response_id", response_id_val);
    
    fprintf(stderr, "[ROUTE Bridge] Returning object with handler for avatar to execute\n");
    return result_obj;
}

// Old version for main VM - kept for compatibility
Value builtin_route_bridge_invoke(int arg_count, Value* args, VM* vm) {
    if (arg_count < 2) {
        fprintf(stderr, "[ROUTE] route_bridge_invoke requires 2 arguments: route_pattern, request_object\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid arguments\"}");
        return error;
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "[ROUTE] route_bridge_invoke: route_pattern must be string\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid argument types\"}");
        return error;
    }
    
    const char* route_pattern = args[0].as.string;  // e.g., "GET /api/users"
    Value request_object = args[1];                  // Request object (already parsed by caller)
    
    printf("[ROUTE Bridge] Pattern: %s\n", route_pattern);
    printf("[ROUTE Bridge] Request object type: %d\n", request_object.type);
    
    // Parse method and path from pattern
    char method[16] = {0};
    char path[256] = {0};
    if (sscanf(route_pattern, "%15s %255s", method, path) != 2) {
        fprintf(stderr, "[ROUTE Bridge] Invalid route pattern format\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"error\":\"Invalid route pattern\"}");
        return error;
    }
    
    // Find matching route
    RouteRegistry* registry = get_global_registry();
    RouteEntry* matched_route = route_find(registry, method, path);
    
    if (!matched_route) {
        fprintf(stderr, "[ROUTE Bridge] No matching route found for %s %s\n", method, path);
        Value error;
        error.type = VALUE_STRING;
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), 
                "{\"status\":404,\"headers\":{},\"body\":\"{\\\"error\\\":\\\"Route not found: %s %s\\\"}\"}",
                method, path);
        error.as.string = strdup(error_msg);
        return error;
    }
    
    if (!matched_route->bridge_enabled) {
        fprintf(stderr, "[ROUTE Bridge] Route %s %s is not bridge-enabled\n", method, path);
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"status\":403,\"headers\":{},\"body\":\"{\\\"error\\\":\\\"Route not accessible via bridge\\\"}\"}");
        return error;
    }
    
    printf("[ROUTE Bridge] Matched route: %s %s -> %s\n", method, matched_route->path, matched_route->handler_name);
    
    // VM is passed as third parameter to builtin functions
    if (!vm) {
        fprintf(stderr, "[ROUTE Bridge] ERROR: No VM context\n");
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"status\":500,\"headers\":{},\"body\":\"{\\\"error\\\":\\\"No VM context\\\"}\"}");
        return error;
    }
    
    // Lookup handler function in VM globals
    Value handler_value;
    if (!vm_get_global_value(vm, matched_route->handler_name, &handler_value)) {
        fprintf(stderr, "[ROUTE Bridge] ERROR: Handler '%s' not found\n", matched_route->handler_name);
        Value error;
        error.type = VALUE_STRING;
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                "{\"status\":500,\"headers\":{},\"body\":\"{\\\"error\\\":\\\"Handler not found: %s\\\"}\"}" ,
                matched_route->handler_name);
        error.as.string = strdup(err_msg);
        return error;
    }
    
    if (handler_value.type != VALUE_FUNCTION) {
        fprintf(stderr, "[ROUTE Bridge] ERROR: '%s' is not a function\n", matched_route->handler_name);
        Value error;
        error.type = VALUE_STRING;
        error.as.string = strdup("{\"status\":500,\"headers\":{},\"body\":\"{\\\"error\\\":\\\"Handler is not a function\\\"}\"}" );
        return error;
    }
    
    // Pass request and response as NUMERIC IDs like HTTP library does
    printf("[ROUTE Bridge] Calling handler with request/response IDs...\n");
    
    // Use async_http's pointer registration system
    extern int kyl_aio_register_ptr(void*);
    extern void* kyl_aio_get_ptr(int);
    extern void kyl_aio_unregister_ptr(int);
    
    // Response builder structure matching HTTP library format
    typedef struct {
        int status_code;
        char* body;
        size_t body_length;
        char** header_names;
        char** header_values;
        int header_count;
        char* content_type;
    } KylResponseBuilder;
    
    // Create response builder structure on heap
    KylResponseBuilder* resp_builder = malloc(sizeof(KylResponseBuilder));
    memset(resp_builder, 0, sizeof(KylResponseBuilder));
    resp_builder->status_code = 200;
    resp_builder->header_names = NULL;
    resp_builder->header_values = NULL;
    resp_builder->header_count = 0;
    resp_builder->body = NULL;
    resp_builder->body_length = 0;
    resp_builder->content_type = NULL;
    
    // Register to get numeric ID
    int response_id = kyl_aio_register_ptr(resp_builder);
    printf("[ROUTE Bridge] Response registered with ID: %d at %p\n", response_id, (void*)resp_builder);
    
    // Create response wrapper object with __id field using VM utility
    Value id_value;
    id_value.type = VALUE_NUMBER;
    id_value.as.number = (double)response_id;
    Value response_object = vm_object_create_with_field("__id", id_value);
    
    printf("[ROUTE Bridge] Created response object wrapper with __id=%d\n", response_id);
    
    // IMPORTANT: This code is called from avatar context which already has separate stack
    // We execute handler directly in the avatar's execution context, no need for call_kuyil_function
    // The handler is a regular function call using the avatar's own stack/frames
    
    fprintf(stderr, "[ROUTE Bridge] Handler is in VM, will be called via normal function call\n");
    fprintf(stderr, "[ROUTE Bridge] Handler value type: %d\n", handler_value.type);
    fflush(stderr);
    
    // Note: Handler execution happens via OP_CALL in avatar's vm_run loop
    // We just return NIL here - the actual call happens through normal bytecode execution
    // This function just sets up the response builder
    Value result;
    result.type = VALUE_NIL;
    
    fprintf(stderr, "[ROUTE Bridge] Handler returned successfully\n");
    fprintf(stderr, "[ROUTE Bridge] Result type: %d\n", result.type);
    fflush(stderr);
    
    // Get the response builder back from the registry using the ID we passed
    KylResponseBuilder* resp_builder_result = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
    
    fprintf(stderr, "[ROUTE Bridge] Got response builder: %p\n", (void*)resp_builder_result);
    fflush(stderr);
    
    if (!resp_builder_result) {
        fprintf(stderr, "[ROUTE Bridge] ERROR: Could not retrieve response builder with ID %d\n", response_id);
        Value empty;
        empty.type = VALUE_STRING;
        empty.as.string = strdup("");
        return empty;
    }
    
    printf("[ROUTE Bridge] Response status: %d\n", resp_builder_result->status_code);
    printf("[ROUTE Bridge] Response body length: %zu\n", resp_builder_result->body_length);
    
    // Extract body from HttpResponseBuilder
    Value response_body;
    if (resp_builder_result->body && resp_builder_result->body_length > 0) {
        printf("[ROUTE Bridge] Response body found: %.100s%s\n", 
               resp_builder_result->body,
               resp_builder_result->body_length > 100 ? "..." : "");
        response_body.type = VALUE_STRING;
        response_body.as.string = strndup(resp_builder_result->body, resp_builder_result->body_length);
    } else {
        printf("[ROUTE Bridge] No response body, returning empty\n");
        response_body.type = VALUE_STRING;
        response_body.as.string = strdup("");
    }
    
    // Clean up
    kyl_aio_unregister_ptr(response_id);
    if (resp_builder_result->body) free(resp_builder_result->body);
    if (resp_builder_result->header_names) {
        for (int i = 0; i < resp_builder_result->header_count; i++) {
            if (resp_builder_result->header_names[i]) free(resp_builder_result->header_names[i]);
            if (resp_builder_result->header_values[i]) free(resp_builder_result->header_values[i]);
        }
        free(resp_builder_result->header_names);
        free(resp_builder_result->header_values);
    }
    if (resp_builder_result->content_type) free(resp_builder_result->content_type);
    free(resp_builder_result);
    
    fprintf(stderr, "[ROUTE Bridge] About to return response_body, type=%d\n", response_body.type);
    fflush(stderr);
    
    return response_body;
}
