#define _GNU_SOURCE
#include "libkylhttp.h"
#include "http_client_ex.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// For Phase 1: We'll implement simple versions without full map support
// TODO: Add proper map handling in Phase 2

// http_clientGetEx(url: string, headers: map) -> HttpResponse
// Simplified: ignores headers for now
Value kyl_http_clientGetEx(int arg_count, Value* args) {
    Value result = {.type = VALUE_NIL};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    const char* url = args[0].as.string;
    
    // Simple GET without custom headers for now
    HttpClientResponse* response = http_client_get_ex(url, NULL, NULL, 0);
    
    if (!response) {
        return result;
    }
    
    // Create object result
    result.type = VALUE_OBJECT;
    result.as.object.count = 4;
    result.as.object.keys = malloc(4 * sizeof(char*));
    result.as.object.values = malloc(4 * sizeof(Value));
    
    // status field
    result.as.object.keys[0] = strdup("status");
    result.as.object.values[0].type = VALUE_NUMBER;
    result.as.object.values[0].as.number = (double)response->status_code;
    
    // body field
    result.as.object.keys[1] = strdup("body");
    result.as.object.values[1].type = VALUE_STRING;
    result.as.object.values[1].as.string = response->body ? strdup(response->body) : strdup("");
    
    // error field
    result.as.object.keys[2] = strdup("error");
    result.as.object.values[2].type = VALUE_STRING;
    result.as.object.values[2].as.string = response->error_message ? strdup(response->error_message) : strdup("");
    
    // responseTime field
    result.as.object.keys[3] = strdup("responseTime");
    result.as.object.values[3].type = VALUE_NUMBER;
    result.as.object.values[3].as.number = (double)response->response_time_ms;
    
    http_client_response_ex_free(response);
    return result;
}

// http_clientPostEx(url: string, body: string, headers: map) -> HttpResponse
// Simplified: ignores headers for now
Value kyl_http_clientPostEx(int arg_count, Value* args) {
    Value result = {.type = VALUE_NIL};
    
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        return result;
    }
    
    const char* url = args[0].as.string;
    const char* body = args[1].as.string;
    
    HttpClientResponse* response = http_client_post_ex(url, body, NULL, NULL, 0);
    
    if (!response) {
        fprintf(stderr, "[C DEBUG] http_client_post_ex returned NULL\n");
        return result;
    }
    
    fprintf(stderr, "[C DEBUG] Response received - status: %d, body_length: %zu\n", 
            response->status_code, response->body_length);
    fprintf(stderr, "[C DEBUG] Body pointer: %p\n", (void*)response->body);
    if (response->body) {
        fprintf(stderr, "[C DEBUG] Body content (first 100 chars): %.100s\n", response->body);
    }
    if (response->error_message) {
        fprintf(stderr, "[C DEBUG] Error message: %s\n", response->error_message);
    }
    
    // Return the response body as a string
    if (response->body && strlen(response->body) > 0) {
        result.type = VALUE_STRING;
        result.as.string = strdup(response->body);
        fprintf(stderr, "[HTTP C] SUCCESS: Returning response body, type=%d, length=%zu, ptr=%p\n", 
                result.type, strlen(response->body), (void*)result.as.string);
        fprintf(stderr, "[HTTP C] First 50 chars: %.50s\n", result.as.string);
    } else {
        // No body or error - return nil
        result.type = VALUE_NIL;
        result.as.string = NULL;
        fprintf(stderr, "[HTTP C] FAIL: No response body, returning nil\n");
    }
    
    http_client_response_ex_free(response);
    fprintf(stderr, "[HTTP C] About to return, result.type=%d\n", result.type);
    return result;
}

// Helper functions to extract fields from Kuyil objects
static const char* get_string_field(Value* obj, const char* field_name) {
    for (int i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.keys[i], field_name) == 0) {
            if (obj->as.object.values[i].type == VALUE_STRING) {
                return obj->as.object.values[i].as.string;
            }
            break;
        }
    }
    return NULL;
}

static double get_number_field(Value* obj, const char* field_name, double default_val) {
    for (int i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.keys[i], field_name) == 0) {
            if (obj->as.object.values[i].type == VALUE_NUMBER) {
                return obj->as.object.values[i].as.number;
            }
            break;
        }
    }
    return default_val;
}

static bool get_bool_field(Value* obj, const char* field_name, bool default_val) {
    for (int i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.keys[i], field_name) == 0) {
            if (obj->as.object.values[i].type == VALUE_BOOL) {
                return obj->as.object.values[i].as.boolean;
            }
            break;
        }
    }
    return default_val;
}

// http_clientRequestEx(request: HttpRequest) -> HttpResponse
// Full request configuration using struct parameter
Value kyl_http_clientRequestEx(int arg_count, Value* args) {
    Value result = {.type = VALUE_NIL};
    
    if (arg_count < 1 || args[0].type != VALUE_OBJECT) {
        fprintf(stderr, "[C DEBUG] kyl_http_clientRequestEx: Invalid args (count=%d, type=%d)\n", 
                arg_count, arg_count > 0 ? args[0].type : -1);
        return result;
    }
    
    Value* request_obj = &args[0];
    
    // Extract fields from Kuyil HttpRequest object
    const char* url = get_string_field(request_obj, "url");
    const char* method = get_string_field(request_obj, "method");
    const char* body = get_string_field(request_obj, "body");
    int timeout = (int)get_number_field(request_obj, "timeout", 0);
    bool follow_redirects = get_bool_field(request_obj, "followRedirects", true);
    bool verify_ssl = get_bool_field(request_obj, "verifySSL", true);
    
    if (!url) {
        fprintf(stderr, "[C DEBUG] kyl_http_clientRequestEx: Missing url field\n");
        return result;
    }
    
    // Build C HttpClientRequest struct
    HttpClientRequest c_request = {0};
    c_request.url = url;
    c_request.method = method ? method : "GET";
    c_request.body = body;
    c_request.body_length = body ? strlen(body) : 0;
    c_request.header_names = NULL;
    c_request.header_values = NULL;
    c_request.header_count = 0;
    c_request.timeout_ms = timeout;
    c_request.follow_redirects = follow_redirects;
    c_request.verify_ssl = verify_ssl;
    
    // TODO: Extract headers from the map field when map support is added
    
    // Call the actual C function
    HttpClientResponse* response = http_client_request_ex(&c_request);
    
    if (!response) {
        fprintf(stderr, "[C DEBUG] http_client_request_ex returned NULL\n");
        return result;
    }
    
    // Create object result
    result.type = VALUE_OBJECT;
    result.as.object.count = 4;
    result.as.object.keys = malloc(4 * sizeof(char*));
    result.as.object.values = malloc(4 * sizeof(Value));
    
    // status field
    result.as.object.keys[0] = strdup("status");
    result.as.object.values[0].type = VALUE_NUMBER;
    result.as.object.values[0].as.number = (double)response->status_code;
    
    // body field
    result.as.object.keys[1] = strdup("body");
    result.as.object.values[1].type = VALUE_STRING;
    result.as.object.values[1].as.string = response->body ? strdup(response->body) : strdup("");
    
    // error field
    result.as.object.keys[2] = strdup("error");
    result.as.object.values[2].type = VALUE_STRING;
    result.as.object.values[2].as.string = response->error_message ? strdup(response->error_message) : strdup("");
    
    // responseTime field
    result.as.object.keys[3] = strdup("responseTime");
    result.as.object.values[3].type = VALUE_NUMBER;
    result.as.object.values[3].as.number = (double)response->response_time_ms;
    
    http_client_response_ex_free(response);
    return result;
}

// Stub implementations for other functions
Value kyl_http_clientPutEx(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result = {.type = VALUE_NIL};
    return result;
}

Value kyl_http_clientDeleteEx(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result = {.type = VALUE_NIL};
    return result;
}

Value kyl_http_responseGetHeader(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result = {.type = VALUE_STRING};
    result.as.string = strdup("");
    return result;
}

Value kyl_http_responseHasHeader(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    Value result = {.type = VALUE_BOOL};
    result.as.boolean = false;
    return result;
}
