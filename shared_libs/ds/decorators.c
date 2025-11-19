// Decorators and HTTP Route Registration
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <regex.h>
#include "../../src/ast.h"

#define MAX_ROUTES 256
#define MAX_PARAMS 16
#define MAX_METADATA 64

// HTTP Route entry
typedef struct {
    char* method;       // GET, POST, etc.
    char* path;         // Exact path or regex pattern
    char* handler;      // Function name
    regex_t regex;      // Compiled regex
    bool use_regex;     // Whether to use regex matching
    char* param_names[MAX_PARAMS]; // Parameter names for regex captures
    int param_count;
} Route;

// Metadata entry for functions
typedef struct {
    char* func_name;
    char* key;
    char* value;
} Metadata;

// Global registries
static Route g_routes[MAX_ROUTES];
static int g_route_count = 0;
static pthread_mutex_t g_route_mutex = PTHREAD_MUTEX_INITIALIZER;

static Metadata g_metadata[MAX_METADATA];
static int g_metadata_count = 0;
static pthread_mutex_t g_metadata_mutex = PTHREAD_MUTEX_INITIALIZER;

// Helper: Create string value
static Value make_string(const char* str) {
    Value v = {VALUE_STRING};
    v.as.string = str ? strdup(str) : NULL;
    return v;
}

// Register HTTP route (simple path)
Value kyl_ds_routeRegister(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    pthread_mutex_lock(&g_route_mutex);
    
    if (g_route_count >= MAX_ROUTES) {
        pthread_mutex_unlock(&g_route_mutex);
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    Route* route = &g_routes[g_route_count];
    route->method = strdup(args[0].as.string);
    route->path = strdup(args[1].as.string);
    route->handler = strdup(args[2].as.string);
    route->use_regex = false;
    route->param_count = 0;
    
    g_route_count++;
    pthread_mutex_unlock(&g_route_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Register HTTP route with regex and parameter names
Value kyl_ds_routeRegisterRegex(int arg_count, Value* args) {
    if (arg_count < 4 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_ARRAY || 
        args[3].type != VALUE_STRING) {
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    pthread_mutex_lock(&g_route_mutex);
    
    if (g_route_count >= MAX_ROUTES) {
        pthread_mutex_unlock(&g_route_mutex);
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    Route* route = &g_routes[g_route_count];
    route->method = strdup(args[0].as.string);
    route->path = strdup(args[1].as.string);
    route->handler = strdup(args[3].as.string);
    route->use_regex = true;
    
    // Compile regex
    int ret = regcomp(&route->regex, args[1].as.string, REG_EXTENDED);
    if (ret != 0) {
        free(route->method);
        free(route->path);
        free(route->handler);
        pthread_mutex_unlock(&g_route_mutex);
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    // Store parameter names
    ValueArray* param_array = &args[2].as.array;
    route->param_count = param_array->count < MAX_PARAMS ? param_array->count : MAX_PARAMS;
    for (int i = 0; i < route->param_count; i++) {
        if (param_array->values[i].type == VALUE_STRING) {
            route->param_names[i] = strdup(param_array->values[i].as.string);
        } else {
            route->param_names[i] = NULL;
        }
    }
    
    g_route_count++;
    pthread_mutex_unlock(&g_route_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Find matching route
Value kyl_ds_routeFind(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    const char* method = args[0].as.string;
    const char* path = args[1].as.string;
    
    pthread_mutex_lock(&g_route_mutex);
    
    for (int i = 0; i < g_route_count; i++) {
        Route* route = &g_routes[i];
        
        // Check method match
        if (strcmp(route->method, method) != 0 && strcmp(route->method, "*") != 0) {
            continue;
        }
        
        // Check path match
        if (!route->use_regex) {
            // Exact match
            if (strcmp(route->path, path) == 0) {
                pthread_mutex_unlock(&g_route_mutex);
                
                // Return {handler: "funcName", params: {}}
                Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
                result.as.object.count = 2;
                result.as.object.keys = malloc(sizeof(char*) * 2);
                result.as.object.values = malloc(sizeof(Value) * 2);
                
                result.as.object.keys[0] = strdup("handler");
                result.as.object.values[0] = make_string(route->handler);
                
                result.as.object.keys[1] = strdup("params");
                Value empty_params = {VALUE_OBJECT};
                empty_params.as.object.count = 0;
                empty_params.as.object.keys = NULL;
                empty_params.as.object.values = NULL;
                result.as.object.values[1] = empty_params;
                
                return result;
            }
        } else {
            // Regex match
            regmatch_t matches[MAX_PARAMS + 1];
            int ret = regexec(&route->regex, path, MAX_PARAMS + 1, matches, 0);
            
            if (ret == 0) {
                pthread_mutex_unlock(&g_route_mutex);
                
                // Build params object from captures
                Value params = {VALUE_OBJECT};
                params.as.object.count = route->param_count;
                params.as.object.keys = malloc(sizeof(char*) * route->param_count);
                params.as.object.values = malloc(sizeof(Value) * route->param_count);
                
                for (int j = 0; j < route->param_count; j++) {
                    params.as.object.keys[j] = strdup(route->param_names[j]);
                    
                    // Extract capture group (j+1 because 0 is full match)
                    if (matches[j + 1].rm_so != -1) {
                        int len = matches[j + 1].rm_eo - matches[j + 1].rm_so;
                        char* captured = malloc(len + 1);
                        strncpy(captured, path + matches[j + 1].rm_so, len);
                        captured[len] = '\0';
                        params.as.object.values[j] = make_string(captured);
                        free(captured);
                    } else {
                        params.as.object.values[j].type = VALUE_NIL;
                    }
                }
                
                // Return {handler: "funcName", params: {...}}
                Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
                result.as.object.count = 2;
                result.as.object.keys = malloc(sizeof(char*) * 2);
                result.as.object.values = malloc(sizeof(Value) * 2);
                
                result.as.object.keys[0] = strdup("handler");
                result.as.object.values[0] = make_string(route->handler);
                
                result.as.object.keys[1] = strdup("params");
                result.as.object.values[1] = params;
                
                return result;
            }
        }
    }
    
    pthread_mutex_unlock(&g_route_mutex);
    
    Value err = {VALUE_NIL};
    return err;
}

// List all routes
Value kyl_ds_routeList(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    pthread_mutex_lock(&g_route_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = g_route_count;
    result.as.array.values = malloc(sizeof(Value) * g_route_count);
    
    for (int i = 0; i < g_route_count; i++) {
        Route* route = &g_routes[i];
        
        Value route_obj = {VALUE_OBJECT};
        route_obj.as.object.count = 3;
        route_obj.as.object.keys = malloc(sizeof(char*) * 3);
        route_obj.as.object.values = malloc(sizeof(Value) * 3);
        
        route_obj.as.object.keys[0] = strdup("method");
        route_obj.as.object.values[0] = make_string(route->method);
        
        route_obj.as.object.keys[1] = strdup("path");
        route_obj.as.object.values[1] = make_string(route->path);
        
        route_obj.as.object.keys[2] = strdup("handler");
        route_obj.as.object.values[2] = make_string(route->handler);
        
        result.as.array.values[i] = route_obj;
    }
    
    pthread_mutex_unlock(&g_route_mutex);
    
    return result;
}

// Clear all routes
Value kyl_ds_routeClear(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    pthread_mutex_lock(&g_route_mutex);
    
    for (int i = 0; i < g_route_count; i++) {
        Route* route = &g_routes[i];
        free(route->method);
        free(route->path);
        free(route->handler);
        if (route->use_regex) {
            regfree(&route->regex);
        }
        for (int j = 0; j < route->param_count; j++) {
            free(route->param_names[j]);
        }
    }
    
    g_route_count = 0;
    pthread_mutex_unlock(&g_route_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Set metadata for function
Value kyl_ds_metaSet(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    pthread_mutex_lock(&g_metadata_mutex);
    
    if (g_metadata_count >= MAX_METADATA) {
        pthread_mutex_unlock(&g_metadata_mutex);
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    Metadata* meta = &g_metadata[g_metadata_count];
    meta->func_name = strdup(args[0].as.string);
    meta->key = strdup(args[1].as.string);
    meta->value = strdup(args[2].as.string);
    
    g_metadata_count++;
    pthread_mutex_unlock(&g_metadata_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Get metadata
Value kyl_ds_metaGet(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    const char* func_name = args[0].as.string;
    const char* key = args[1].as.string;
    
    pthread_mutex_lock(&g_metadata_mutex);
    
    for (int i = 0; i < g_metadata_count; i++) {
        if (strcmp(g_metadata[i].func_name, func_name) == 0 && 
            strcmp(g_metadata[i].key, key) == 0) {
            Value result = make_string(g_metadata[i].value);
            pthread_mutex_unlock(&g_metadata_mutex);
            return result;
        }
    }
    
    pthread_mutex_unlock(&g_metadata_mutex);
    
    Value err = {VALUE_NIL};
    return err;
}

// Get all metadata for function
Value kyl_ds_metaGetAll(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    const char* func_name = args[0].as.string;
    
    pthread_mutex_lock(&g_metadata_mutex);
    
    // Count matching entries
    int count = 0;
    for (int i = 0; i < g_metadata_count; i++) {
        if (strcmp(g_metadata[i].func_name, func_name) == 0) {
            count++;
        }
    }
    
    // Build object
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    result.as.object.count = count;
    result.as.object.keys = malloc(sizeof(char*) * count);
    result.as.object.values = malloc(sizeof(Value) * count);
    
    int idx = 0;
    for (int i = 0; i < g_metadata_count; i++) {
        if (strcmp(g_metadata[i].func_name, func_name) == 0) {
            result.as.object.keys[idx] = strdup(g_metadata[i].key);
            result.as.object.values[idx] = make_string(g_metadata[i].value);
            idx++;
        }
    }
    
    pthread_mutex_unlock(&g_metadata_mutex);
    
    return result;
}

// Check if metadata exists
Value kyl_ds_metaHas(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value err = {VALUE_BOOL};
        err.as.boolean = false;
        return err;
    }
    
    const char* func_name = args[0].as.string;
    const char* key = args[1].as.string;
    
    pthread_mutex_lock(&g_metadata_mutex);
    
    for (int i = 0; i < g_metadata_count; i++) {
        if (strcmp(g_metadata[i].func_name, func_name) == 0 && 
            strcmp(g_metadata[i].key, key) == 0) {
            pthread_mutex_unlock(&g_metadata_mutex);
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
            result.as.boolean = true;
            return result;
        }
    }
    
    pthread_mutex_unlock(&g_metadata_mutex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = false;
    return result;
}

// Stub implementations for decorator create/manage (can be extended)
Value kyl_ds_decoratorCreate(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = 1;
    return result;
}

Value kyl_ds_decoratorAddMetadata(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ds_decoratorGetMetadata(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

Value kyl_ds_decoratorList(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = 0;
    result.as.array.values = NULL;
    return result;
}
