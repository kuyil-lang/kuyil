// libkylresponse.c - Response builder functions for route decorator
// Uses kyl_aio pointer registry

#include "../../src/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// External functions from async_http.c
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

// Set response status: response_setStatus(res_id, 200)
Value kyl_response_set_status(int arg_count, Value* args) {
    printf("[kyl_response_set_status] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        fprintf(stderr, "[kyl_response_set_status] ERROR: Invalid args\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr((int)args[0].as.number);
    if (!res) {
        fprintf(stderr, "[kyl_response_set_status] ERROR: Invalid response ID %d\n", (int)args[0].as.number);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    res->status_code = (int)args[1].as.number;
    printf("[kyl_response_set_status] Set status to %d\n", res->status_code);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Set response body: response_setBody(res_id, "Hello")
Value kyl_response_set_body(int arg_count, Value* args) {
    printf("[kyl_response_set_body] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_response_set_body] ERROR: Invalid args - arg0 type=%d, arg1 type=%d\n",
                args[0].type, args[1].type);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr((int)args[0].as.number);
    if (!res) {
        fprintf(stderr, "[kyl_response_set_body] ERROR: Invalid response ID %d\n", (int)args[0].as.number);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* body = args[1].as.string;
    size_t length = strlen(body);
    
    printf("[kyl_response_set_body] Setting body, length=%zu\n", length);
    
    // Free old body if it exists
    if (res->body) {
        free(res->body);
    }
    
    // Allocate and copy new body
    res->body = malloc(length + 1);
    memcpy(res->body, body, length);
    res->body[length] = '\0';
    res->body_length = length;
    
    printf("[kyl_response_set_body] Body set successfully\n");
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Add response header: response_addHeader(res_id, "Content-Type", "text/plain")
Value kyl_response_add_header(int arg_count, Value* args) {
    printf("[kyl_response_add_header] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_response_add_header] ERROR: Invalid args\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr((int)args[0].as.number);
    if (!res) {
        fprintf(stderr, "[kyl_response_add_header] ERROR: Invalid response ID %d\n", (int)args[0].as.number);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* name = args[1].as.string;
    const char* value = args[2].as.string;
    
    printf("[kyl_response_add_header] Adding header: %s: %s\n", name, value);
    
    // Expand arrays
    res->header_names = realloc(res->header_names, sizeof(char*) * (res->header_count + 1));
    res->header_values = realloc(res->header_values, sizeof(char*) * (res->header_count + 1));
    
    res->header_names[res->header_count] = strdup(name);
    res->header_values[res->header_count] = strdup(value);
    res->header_count++;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Set response JSON: response_setJson(res_id, "{\"key\": \"value\"}")
Value kyl_response_set_json(int arg_count, Value* args) {
    printf("[kyl_response_set_json] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_response_set_json] ERROR: Invalid args\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    // Set body first
    Value body_result = kyl_response_set_body(arg_count, args);
    
    // Then set content type
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr((int)args[0].as.number);
    if (res) {
        if (res->content_type) {
            free(res->content_type);
        }
        res->content_type = strdup("application/json");
    }
    
    return body_result;
}
