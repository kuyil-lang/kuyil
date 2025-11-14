#define _GNU_SOURCE
#include "libkylhttp.h"
#include "http_client_ex.h"
#include <string.h>
#include <stdlib.h>

// For Phase 1: We'll implement simple versions without full map support
// TODO: Add proper map/object handling in Phase 2

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
