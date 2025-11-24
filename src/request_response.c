// Request/Response Helper Objects Implementation

#include "request_response.h"
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// Global response registry for mutable response objects
typedef struct {
    int id;
    Value* response_obj;
} ResponseEntry;

static ResponseEntry* g_response_registry = NULL;
static int g_response_count = 0;
static int g_response_capacity = 0;
static int g_next_response_id = 1;
static pthread_mutex_t g_response_lock = PTHREAD_MUTEX_INITIALIZER;

// Register a response object and return its ID
int register_response(Value* response_obj) {
    pthread_mutex_lock(&g_response_lock);
    
    if (g_response_count >= g_response_capacity) {
        int new_cap = g_response_capacity == 0 ? 16 : g_response_capacity * 2;
        g_response_registry = realloc(g_response_registry, sizeof(ResponseEntry) * new_cap);
        g_response_capacity = new_cap;
    }
    
    int id = g_next_response_id++;
    g_response_registry[g_response_count].id = id;
    g_response_registry[g_response_count].response_obj = response_obj;
    g_response_count++;
    
    pthread_mutex_unlock(&g_response_lock);
    return id;
}

// Get response object by ID
Value* get_response_by_id(int id) {
    pthread_mutex_lock(&g_response_lock);
    
    for (int i = 0; i < g_response_count; i++) {
        if (g_response_registry[i].id == id) {
            Value* obj = g_response_registry[i].response_obj;
            pthread_mutex_unlock(&g_response_lock);
            return obj;
        }
    }
    
    pthread_mutex_unlock(&g_response_lock);
    return NULL;
}

// Helper: Parse query string into object
// "?name=john&age=30" -> {name: "john", age: "30"}
static Value parse_query_string(const char* query_string) {
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    
    if (!query_string || strlen(query_string) == 0) {
        result.as.object.count = 0;
        result.as.object.keys = NULL;
        result.as.object.values = NULL;
        return result;
    }
    
    // Skip leading '?' if present
    const char* qs = query_string;
    if (qs[0] == '?') qs++;
    
    // Count parameters (number of '&' + 1)
    int param_count = 1;
    for (const char* p = qs; *p; p++) {
        if (*p == '&') param_count++;
    }
    
    result.as.object.count = param_count;
    result.as.object.keys = malloc(sizeof(char*) * param_count);
    result.as.object.values = malloc(sizeof(Value) * param_count);
    
    // Parse key=value pairs
    char* qs_copy = strdup(qs);
    char* token = strtok(qs_copy, "&");
    int idx = 0;
    
    while (token && idx < param_count) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            result.as.object.keys[idx] = strdup(token);
            result.as.object.values[idx].type = VALUE_STRING;
            result.as.object.values[idx].as.string = strdup(eq + 1);
            idx++;
        }
        token = strtok(NULL, "&");
    }
    
    result.as.object.count = idx;
    free(qs_copy);
    
    return result;
}

// Create Request object
Value create_request_object(const char* method, const char* path,
                            Value params, Value query,
                            Value headers, Value body) {
    Value req;
    memset(&req, 0, sizeof(Value));
    req.type = VALUE_OBJECT;
    req.as.object.count = 6;
    req.as.object.keys = malloc(sizeof(char*) * 6);
    req.as.object.values = malloc(sizeof(Value) * 6);
    
    // method
    req.as.object.keys[0] = strdup("method");
    req.as.object.values[0].type = VALUE_STRING;
    req.as.object.values[0].as.string = strdup(method);
    
    // path
    req.as.object.keys[1] = strdup("path");
    req.as.object.values[1].type = VALUE_STRING;
    req.as.object.values[1].as.string = strdup(path);
    
    // params (from route pattern matching)
    req.as.object.keys[2] = strdup("params");
    req.as.object.values[2] = params;
    
    // query (from query string ?key=value&...)
    req.as.object.keys[3] = strdup("query");
    req.as.object.values[3] = query;
    
    // headers
    req.as.object.keys[4] = strdup("headers");
    req.as.object.values[4] = headers;
    
    // body
    req.as.object.keys[5] = strdup("body");
    req.as.object.values[5] = body;
    
    return req;
}

// Create Response object with builder methods
Value create_response_object() {
    Value res;
    memset(&res, 0, sizeof(Value));
    res.type = VALUE_OBJECT;
    res.as.object.count = 4;
    res.as.object.keys = malloc(sizeof(char*) * 4);
    res.as.object.values = malloc(sizeof(Value) * 4);
    
    // status code (default 200)
    res.as.object.keys[0] = strdup("statusCode");
    res.as.object.values[0].type = VALUE_NUMBER;
    res.as.object.values[0].as.number = 200;
    
    // headers object
    res.as.object.keys[1] = strdup("headers");
    res.as.object.values[1].type = VALUE_OBJECT;
    res.as.object.values[1].as.object.count = 0;
    res.as.object.values[1].as.object.keys = NULL;
    res.as.object.values[1].as.object.values = NULL;
    
    // body (initially nil)
    res.as.object.keys[2] = strdup("body");
    res.as.object.values[2].type = VALUE_NIL;
    
    // sent flag
    res.as.object.keys[3] = strdup("sent");
    res.as.object.values[3].type = VALUE_BOOL;
    res.as.object.values[3].as.boolean = false;
    
    return res;
}

// Set response status code
Value response_set_status(Value* res_obj, int status_code) {
    if (!res_obj || res_obj->type != VALUE_OBJECT) {
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    // Find statusCode field
    for (int i = 0; i < res_obj->as.object.count; i++) {
        if (strcmp(res_obj->as.object.keys[i], "statusCode") == 0) {
            res_obj->as.object.values[i].type = VALUE_NUMBER;
            res_obj->as.object.values[i].as.number = status_code;
            break;
        }
    }
    
    return *res_obj;
}

// Send JSON response
Value response_send_json(Value* res_obj, Value data) {
    if (!res_obj || res_obj->type != VALUE_OBJECT) {
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    // Set body to data
    for (int i = 0; i < res_obj->as.object.count; i++) {
        if (strcmp(res_obj->as.object.keys[i], "body") == 0) {
            res_obj->as.object.values[i] = data;
        } else if (strcmp(res_obj->as.object.keys[i], "sent") == 0) {
            res_obj->as.object.values[i].type = VALUE_BOOL;
            res_obj->as.object.values[i].as.boolean = true;
        }
    }
    
    // Set Content-Type header
    response_set_header(res_obj, "Content-Type", "application/json");
    
    return *res_obj;
}

// Send text/HTML response
Value response_send_text(Value* res_obj, const char* text) {
    if (!res_obj || res_obj->type != VALUE_OBJECT) {
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    // Set body to text
    for (int i = 0; i < res_obj->as.object.count; i++) {
        if (strcmp(res_obj->as.object.keys[i], "body") == 0) {
            res_obj->as.object.values[i].type = VALUE_STRING;
            res_obj->as.object.values[i].as.string = strdup(text);
        } else if (strcmp(res_obj->as.object.keys[i], "sent") == 0) {
            res_obj->as.object.values[i].type = VALUE_BOOL;
            res_obj->as.object.values[i].as.boolean = true;
        }
    }
    
    return *res_obj;
}

// Set response header
Value response_set_header(Value* res_obj, const char* key, const char* value) {
    if (!res_obj || res_obj->type != VALUE_OBJECT) {
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    // Find headers object
    Value* headers = NULL;
    for (int i = 0; i < res_obj->as.object.count; i++) {
        if (strcmp(res_obj->as.object.keys[i], "headers") == 0) {
            headers = &res_obj->as.object.values[i];
            break;
        }
    }
    
    if (!headers || headers->type != VALUE_OBJECT) {
        return *res_obj;
    }
    
    // Check if header already exists
    for (int i = 0; i < headers->as.object.count; i++) {
        if (strcmp(headers->as.object.keys[i], key) == 0) {
            // Update existing header
            free(headers->as.object.values[i].as.string);
            headers->as.object.values[i].as.string = strdup(value);
            return *res_obj;
        }
    }
    
    // Add new header
    int new_count = headers->as.object.count + 1;
    headers->as.object.keys = realloc(headers->as.object.keys, sizeof(char*) * new_count);
    headers->as.object.values = realloc(headers->as.object.values, sizeof(Value) * new_count);
    
    headers->as.object.keys[headers->as.object.count] = strdup(key);
    headers->as.object.values[headers->as.object.count].type = VALUE_STRING;
    headers->as.object.values[headers->as.object.count].as.string = strdup(value);
    headers->as.object.count = new_count;
    
    return *res_obj;
}

// Built-in: Request(method, path, params?, query?, headers?, body?)
Value builtin_Request(int arg_count, Value* args) {
    if (arg_count < 2) {
        fprintf(stderr, "[REQUEST] Request requires at least 2 arguments: method, path\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    if (args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[REQUEST] method and path must be strings\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    const char* method = args[0].as.string;
    const char* path = args[1].as.string;
    
    // Optional params (default: empty object)
    Value params;
    if (arg_count > 2 && args[2].type == VALUE_OBJECT) {
        params = args[2];
    } else {
        memset(&params, 0, sizeof(Value));
        params.type = VALUE_OBJECT;
        params.as.object.count = 0;
        params.as.object.keys = NULL;
        params.as.object.values = NULL;
    }
    
    // Optional query (default: empty object or parse from path)
    Value query;
    if (arg_count > 3 && (args[3].type == VALUE_OBJECT || args[3].type == VALUE_STRING)) {
        if (args[3].type == VALUE_STRING) {
            query = parse_query_string(args[3].as.string);
        } else {
            query = args[3];
        }
    } else {
        // Try to parse query from path
        const char* q = strchr(path, '?');
        if (q) {
            query = parse_query_string(q);
        } else {
            memset(&query, 0, sizeof(Value));
            query.type = VALUE_OBJECT;
            query.as.object.count = 0;
            query.as.object.keys = NULL;
            query.as.object.values = NULL;
        }
    }
    
    // Optional headers
    Value headers;
    if (arg_count > 4 && args[4].type == VALUE_OBJECT) {
        headers = args[4];
    } else {
        memset(&headers, 0, sizeof(Value));
        headers.type = VALUE_OBJECT;
        headers.as.object.count = 0;
        headers.as.object.keys = NULL;
        headers.as.object.values = NULL;
    }
    
    // Optional body
    Value body;
    if (arg_count > 5) {
        body = args[5];
    } else {
        memset(&body, 0, sizeof(Value));
        body.type = VALUE_NIL;
    }
    
    return create_request_object(method, path, params, query, headers, body);
}

// Built-in: Response()
Value builtin_Response(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    return create_response_object();
}

// Built-in: response_setStatus(response_object, status)  
// Looks up real response object via __response_id and modifies it
Value builtin_response_setStatus(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_OBJECT || args[1].type != VALUE_NUMBER) {
        fprintf(stderr, "[response_setStatus] ERROR: Invalid args: count=%d, arg0_type=%d, arg1_type=%d\n",
               arg_count,
               arg_count > 0 ? args[0].type : -1,
               arg_count > 1 ? args[1].type : -1);
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    // Find __response_id field to locate the real object
    int response_id = -1;
    for (int i = 0; i < args[0].as.object.count; i++) {
        if (strcmp(args[0].as.object.keys[i], "__response_id") == 0 &&
            args[0].as.object.values[i].type == VALUE_NUMBER) {
            response_id = (int)args[0].as.object.values[i].as.number;
            break;
        }
    }
    
    if (response_id < 0) {
        fprintf(stderr, "[response_setStatus] ERROR: No __response_id found in object\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    Value* response = get_response_by_id(response_id);
    if (!response) {
        fprintf(stderr, "[response_setStatus] ERROR: Response ID %d not found in registry\n", response_id);
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    printf("[response_setStatus] Found response ID %d at %p\n", response_id, (void*)response);
    int status = (int)args[1].as.number;
    
    // Find or add "status" field
    int status_idx = -1;
    for (int i = 0; i < response->as.object.count; i++) {
        if (strcmp(response->as.object.keys[i], "status") == 0) {
            status_idx = i;
            break;
        }
    }
    
    if (status_idx >= 0) {
        // Update existing
        response->as.object.values[status_idx].type = VALUE_NUMBER;
        response->as.object.values[status_idx].as.number = status;
    } else {
        // Add new field
        int new_count = response->as.object.count + 1;
        response->as.object.keys = realloc(response->as.object.keys, sizeof(char*) * new_count);
        response->as.object.values = realloc(response->as.object.values, sizeof(Value) * new_count);
        
        response->as.object.keys[response->as.object.count] = strdup("status");
        response->as.object.values[response->as.object.count].type = VALUE_NUMBER;
        response->as.object.values[response->as.object.count].as.number = status;
        response->as.object.count = new_count;
    }
    
    Value ok;
    ok.type = VALUE_NIL;
    return ok;
}

// Built-in: response_addHeader(response_object, key, value)
// Looks up real response object via __response_id and modifies it
Value builtin_response_addHeader(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_OBJECT || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    // Find __response_id field to locate the real object
    int response_id = -1;
    for (int i = 0; i < args[0].as.object.count; i++) {
        if (strcmp(args[0].as.object.keys[i], "__response_id") == 0 &&
            args[0].as.object.values[i].type == VALUE_NUMBER) {
            response_id = (int)args[0].as.object.values[i].as.number;
            break;
        }
    }
    
    if (response_id < 0) {
        fprintf(stderr, "[response_addHeader] ERROR: No __response_id found in object\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    Value* response = get_response_by_id(response_id);
    if (!response) {
        fprintf(stderr, "[response_addHeader] ERROR: Response ID %d not found in registry\n", response_id);
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    printf("[response_addHeader] Found response ID %d at %p\n", response_id, (void*)response);
    const char* key = args[1].as.string;
    const char* value = args[2].as.string;
    
    // Find or create "headers" field
    int headers_idx = -1;
    for (int i = 0; i < response->as.object.count; i++) {
        if (strcmp(response->as.object.keys[i], "headers") == 0) {
            headers_idx = i;
            break;
        }
    }
    
    Value* headers;
    if (headers_idx >= 0) {
        headers = &response->as.object.values[headers_idx];
    } else {
        // Add headers field
        int new_count = response->as.object.count + 1;
        response->as.object.keys = realloc(response->as.object.keys, sizeof(char*) * new_count);
        response->as.object.values = realloc(response->as.object.values, sizeof(Value) * new_count);
        
        response->as.object.keys[response->as.object.count] = strdup("headers");
        response->as.object.values[response->as.object.count].type = VALUE_OBJECT;
        response->as.object.values[response->as.object.count].as.object.count = 0;
        response->as.object.values[response->as.object.count].as.object.keys = NULL;
        response->as.object.values[response->as.object.count].as.object.values = NULL;
        
        headers = &response->as.object.values[response->as.object.count];
        response->as.object.count = new_count;
        headers_idx = response->as.object.count - 1;
    }
    
    // Add header to headers object
    int new_header_count = headers->as.object.count + 1;
    headers->as.object.keys = realloc(headers->as.object.keys, sizeof(char*) * new_header_count);
    headers->as.object.values = realloc(headers->as.object.values, sizeof(Value) * new_header_count);
    
    headers->as.object.keys[headers->as.object.count] = strdup(key);
    headers->as.object.values[headers->as.object.count].type = VALUE_STRING;
    headers->as.object.values[headers->as.object.count].as.string = strdup(value);
    headers->as.object.count = new_header_count;
    
    Value ok;
    ok.type = VALUE_NIL;
    return ok;
}

// Built-in: response_setBody(response_object, body)
// Looks up real response object via __response_id and modifies it
Value builtin_response_setBody(int arg_count, Value* args) {
    printf("[BUILTIN response_setBody] *** CALLED *** arg_count=%d\n", arg_count);
    if (arg_count > 0) {
        printf("[BUILTIN response_setBody] arg[0] type=%d (2=NUMBER, 5=OBJECT)\n", args[0].type);
    }
    
    if (arg_count < 2 || args[0].type != VALUE_OBJECT || args[1].type != VALUE_STRING) {
        printf("[response_setBody] ERROR: Invalid arguments - arg0 type=%d, arg1 type=%d\n", 
               arg_count > 0 ? args[0].type : -1,
               arg_count > 1 ? args[1].type : -1);
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    // Find __response_id field to locate the real object
    int response_id = -1;
    for (int i = 0; i < args[0].as.object.count; i++) {
        if (strcmp(args[0].as.object.keys[i], "__response_id") == 0 &&
            args[0].as.object.values[i].type == VALUE_NUMBER) {
            response_id = (int)args[0].as.object.values[i].as.number;
            break;
        }
    }
    
    if (response_id < 0) {
        fprintf(stderr, "[response_setBody] ERROR: No __response_id found in object\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    Value* response = get_response_by_id(response_id);
    if (!response) {
        fprintf(stderr, "[response_setBody] ERROR: Response ID %d not found in registry\n", response_id);
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    const char* body = args[1].as.string;
    printf("[response_setBody] Found response ID %d at %p, setting body length=%zu\n", 
           response_id, (void*)response, strlen(body));
    printf("[response_setBody] Response field count BEFORE: %d\n", response->as.object.count);
    
    // Find or add "body" field
    int body_idx = -1;
    for (int i = 0; i < response->as.object.count; i++) {
        if (strcmp(response->as.object.keys[i], "body") == 0) {
            body_idx = i;
            break;
        }
    }
    
    if (body_idx >= 0) {
        // Update existing
        if (response->as.object.values[body_idx].as.string) {
            free((char*)response->as.object.values[body_idx].as.string);
        }
        response->as.object.values[body_idx].type = VALUE_STRING;
        response->as.object.values[body_idx].as.string = strdup(body);
    } else {
        // Add new field
        int new_count = response->as.object.count + 1;
        response->as.object.keys = realloc(response->as.object.keys, sizeof(char*) * new_count);
        response->as.object.values = realloc(response->as.object.values, sizeof(Value) * new_count);
        
        response->as.object.keys[response->as.object.count] = strdup("body");
        response->as.object.values[response->as.object.count].type = VALUE_STRING;
        response->as.object.values[response->as.object.count].as.string = strdup(body);
        response->as.object.count = new_count;
    }
    
    Value ok;
    ok.type = VALUE_NIL;
    return ok;
}

// ============================================================================
// AIO Request/Response Object Constructors
// Creates Kuyil objects that wrap numeric IDs for use with STRING handlers
// ============================================================================

// Create aio_request object with numeric ID
Value builtin_aio_request(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[aio_request] ERROR: Expected numeric ID\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    int request_id = (int)args[0].as.number;
    printf("[aio_request] Creating request object with ID: %d\n", request_id);
    
    // Create object with __id field using VM utility
    Value id_value;
    id_value.type = VALUE_NUMBER;
    id_value.as.number = request_id;
    
    return vm_object_create_with_field("__id", id_value);
}

// Create aio_response object with numeric ID  
Value builtin_aio_response(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[aio_response] ERROR: Expected numeric ID\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    int response_id = (int)args[0].as.number;
    printf("[aio_response] Creating response object with ID: %d\n", response_id);
    
    // Create object with __id field using VM utility
    Value id_value;
    id_value.type = VALUE_NUMBER;
    id_value.as.number = response_id;
    
    return vm_object_create_with_field("__id", id_value);
}

// Get response body from aio_response object
Value builtin_aio_response_get_body(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_OBJECT) {
        fprintf(stderr, "[aio_response_get_body] ERROR: Expected response object\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    // Extract __id using VM utility
    Value* id_field = vm_object_get_field(&args[0], "__id");
    if (!id_field || id_field->type != VALUE_NUMBER) {
        fprintf(stderr, "[aio_response_get_body] ERROR: No __id found\n");
        Value err;
        err.type = VALUE_NIL;
        return err;
    }
    
    int response_id = (int)id_field->as.number;
    
    // Get response builder from registry
    extern void* kyl_aio_get_ptr(int);
    typedef struct {
        int status_code;
        char* body;
        size_t body_length;
        char** header_names;
        char** header_values;
        int header_count;
        char* content_type;
    } KylResponseBuilder;
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
    if (!res || !res->body) {
        Value empty;
        empty.type = VALUE_STRING;
        empty.as.string = strdup("");
        return empty;
    }
    
    printf("[aio_response_get_body] Getting body, length=%zu\n", res->body_length);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(res->body);
    return result;
}
