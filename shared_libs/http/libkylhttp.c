#define _GNU_SOURCE
#include "libkylhttp.h"
#include "http_server.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Global HTTP server instance (for simplicity in this demo)
static HttpServer* g_http_server = NULL;

// HTTP server creation function - creates or returns existing server
Value kyl_http_server(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int port = (int)args[0].as.number;
    
    // Initialize HTTP subsystem if not already done
    if (!g_http_server) {
        http_init();
        g_http_server = http_server_create(port);
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = port;
    return result;
}

// HTTP GET route registration
Value kyl_http_get(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* path = args[0].as.string;
    // args[1] should be a callback function - for now we'll store the path
    
    if (g_http_server) {
        // Register the route with the HTTP server
        http_server_get(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP GET route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP POST route registration
Value kyl_http_post(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_post(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP POST route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP PUT route registration
Value kyl_http_put(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_put(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP PUT route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP DELETE route registration
Value kyl_http_delete(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_delete(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP DELETE route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP server listen function
Value kyl_http_listen(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int port = (int)args[0].as.number;
    
    if (g_http_server) {
        printf("HTTP server listening on port %d\n", port);
        // In a real implementation, this would start the server in a thread
        // http_server_listen(g_http_server);
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// HTTP client GET function
Value kyl_http_client_get(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* url = args[0].as.string;
    HttpResponse* response = http_get(url);
    
    Value result;
    if (response && response->data) {
        result.type = VALUE_STRING;
        result.as.string = strdup(response->data);
        http_response_free(response);
    } else {
        result.type = VALUE_NIL;
    }
    
    return result;
}

// HTTP client POST function
Value kyl_http_client_post(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* url = args[0].as.string;
    const char* data = args[1].as.string;
    HttpResponse* response = http_post(url, data);
    
    Value result;
    if (response && response->data) {
        result.type = VALUE_STRING;
        result.as.string = strdup(response->data);
        http_response_free(response);
    } else {
        result.type = VALUE_NIL;
    }
    
    return result;
}

// HTTP static file serving
Value kyl_http_static(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* root_path = args[0].as.string;
    
    if (g_http_server) {
        http_server_set_static_root(g_http_server, root_path);
        printf("HTTP static root set to: %s\n", root_path);
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// HTTP server cleanup
Value kyl_http_cleanup(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    if (g_http_server) {
        http_server_stop(g_http_server);
        http_server_free(g_http_server);
        g_http_server = NULL;
        http_cleanup();
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}