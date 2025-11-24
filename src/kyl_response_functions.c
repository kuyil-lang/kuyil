// Response builder functions for route decorator system
// Uses kyl_aio pointer registry
#include "ast.h"
#include "async_http.h"
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Response builder structure with reference counting
typedef struct {
    int status_code;
    char* body;
    size_t body_length;
    char** header_names;
    char** header_values;
    int header_count;
    char* content_type;
    int ref_count;  // Reference counter for safe cleanup
} KylResponseBuilder;

// Reference counting functions
static void response_builder_retain(KylResponseBuilder* builder) {
    if (builder) {
        builder->ref_count++;
        printf("[ResponseBuilder] Retained, ref_count=%d\n", builder->ref_count);
    }
}

static void response_builder_release(KylResponseBuilder* builder) {
    if (!builder) return;
    
    builder->ref_count--;
    printf("[ResponseBuilder] Released, ref_count=%d\n", builder->ref_count);
    
    if (builder->ref_count <= 0) {
        printf("[ResponseBuilder] Freeing builder (ref_count=%d)\n", builder->ref_count);
        
        // Free all allocated memory
        if (builder->body) free(builder->body);
        
        if (builder->header_names) {
            for (int i = 0; i < builder->header_count; i++) {
                if (builder->header_names[i]) free(builder->header_names[i]);
                if (builder->header_values[i]) free(builder->header_values[i]);
            }
            free(builder->header_names);
            free(builder->header_values);
        }
        
        if (builder->content_type) free(builder->content_type);
        
        free(builder);
    }
}

// Set response status: response_setStatus(response_obj, 200)
Value kyl_aio_response_set_status(int arg_count, Value* args) {
    printf("[kyl_aio_response_set_status] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_OBJECT || args[1].type != VALUE_NUMBER) {
        fprintf(stderr, "[kyl_aio_response_set_status] ERROR: Invalid args - expected (object, number)\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    // Extract __id from object using VM utility
    Value* id_field = vm_object_get_field(&args[0], "__id");
    if (!id_field || id_field->type != VALUE_NUMBER) {
        fprintf(stderr, "[kyl_aio_response_set_status] ERROR: No __id found in object\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    int response_id = (int)id_field->as.number;
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
    if (!res) {
        fprintf(stderr, "[kyl_aio_response_set_status] ERROR: Invalid response ID %d\n", response_id);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    res->status_code = (int)args[1].as.number;
    printf("[kyl_aio_response_set_status] Set status to %d\n", res->status_code);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Set response body: response_setBody(response_obj, "Hello")
Value kyl_aio_response_set_body(int arg_count, Value* args) {
    printf("[kyl_aio_response_set_body] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_OBJECT || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_aio_response_set_body] ERROR: Invalid args - expected (object, string), got (%d, %d)\n",
                args[0].type, args[1].type);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    // Extract __id from object using VM utility
    Value* id_field = vm_object_get_field(&args[0], "__id");
    if (!id_field || id_field->type != VALUE_NUMBER) {
        fprintf(stderr, "[kyl_aio_response_set_body] ERROR: No __id found in object\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    int response_id = (int)id_field->as.number;
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
    if (!res) {
        fprintf(stderr, "[kyl_aio_response_set_body] ERROR: Invalid response ID %d\n", response_id);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* body = args[1].as.string;
    
    // Validate pointer before strlen
    if (!body) {
        fprintf(stderr, "[kyl_aio_response_set_body] ERROR: NULL body pointer\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    size_t length = strlen(body);
    printf("[kyl_aio_response_set_body] Body length: %zu\n", length);
    
    // Free old body if it exists (safe since builder is single-use per request)
    if (res->body) {
        free(res->body);
    }
    
    // Allocate and copy new body
    res->body = malloc(length + 1);
    if (!res->body) {
        fprintf(stderr, "[kyl_aio_response_set_body] ERROR: malloc failed for body\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    memcpy(res->body, body, length);
    res->body[length] = '\0';
    res->body_length = length;
    
    printf("[kyl_aio_response_set_body] Successfully copied %zu bytes\n", length);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Add response header: response_addHeader(response_obj, "Content-Type", "text/plain")
Value kyl_aio_response_add_header(int arg_count, Value* args) {
    printf("[kyl_aio_response_add_header] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 3 || args[0].type != VALUE_OBJECT || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_aio_response_add_header] ERROR: Invalid args - expected (object, string, string)\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    // Extract __id from object using VM utility
    Value* id_field = vm_object_get_field(&args[0], "__id");
    if (!id_field || id_field->type != VALUE_NUMBER) {
        fprintf(stderr, "[kyl_aio_response_add_header] ERROR: No __id found in object\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    int response_id = (int)id_field->as.number;
    
    KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
    if (!res) {
        fprintf(stderr, "[kyl_aio_response_add_header] ERROR: Invalid response ID %d\n", response_id);
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* name = args[1].as.string;
    const char* value = args[2].as.string;
    
    printf("[kyl_aio_response_add_header] Adding header: %s: %s\n", name, value);
    
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

// Set response JSON: response_setJson(response_obj, "{\"key\": \"value\"}")
Value kyl_aio_response_set_json(int arg_count, Value* args) {
    printf("[kyl_aio_response_set_json] Called with arg_count=%d\n", arg_count);
    
    if (arg_count != 2 || args[0].type != VALUE_OBJECT || args[1].type != VALUE_STRING) {
        fprintf(stderr, "[kyl_aio_response_set_json] ERROR: Invalid args - expected (object, string)\n");
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_NIL;
        return result;
    }
    
    // Set body first
    Value body_result = kyl_aio_response_set_body(arg_count, args);
    
    // Extract __id to set content type using VM utility
    Value* id_field = vm_object_get_field(&args[0], "__id");
    if (id_field && id_field->type == VALUE_NUMBER) {
        int response_id = (int)id_field->as.number;
        KylResponseBuilder* res = (KylResponseBuilder*)kyl_aio_get_ptr(response_id);
        if (res) {
            if (res->content_type) {
                free(res->content_type);
            }
            res->content_type = strdup("application/json");
        }
    }
    
    return body_result;
}
