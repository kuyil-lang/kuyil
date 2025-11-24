// Request/Response Helper Objects for HTTP Routing
// Provides convenient wrapper objects for HTTP request/response handling

#ifndef REQUEST_RESPONSE_H
#define REQUEST_RESPONSE_H

#include "vm.h"
#include "bytecode.h"
#include <stdbool.h>

// Create a Request object with all HTTP request data
// Returns a VALUE_OBJECT with: method, path, params, query, headers, body
Value create_request_object(const char* method, const char* path, 
                            Value params, Value query, 
                            Value headers, Value body);

// Create a Response builder object
// Returns a VALUE_OBJECT with helper methods: status(), json(), send(), header()
Value create_response_object();

// Response helper: Set status code
// Usage: res.status(200)
Value response_set_status(Value* res_obj, int status_code);

// Response helper: Send JSON response
// Usage: res.json({key: "value"})
Value response_send_json(Value* res_obj, Value data);

// Response helper: Send plain text/HTML response
// Usage: res.send("Hello World")
Value response_send_text(Value* res_obj, const char* text);

// Response helper: Set response header
// Usage: res.header("Content-Type", "application/json")
Value response_set_header(Value* res_obj, const char* key, const char* value);

// Built-in functions for VM
Value builtin_Request(int arg_count, Value* args);
Value builtin_Response(int arg_count, Value* args);
Value builtin_response_setStatus(int arg_count, Value* args);
Value builtin_response_addHeader(int arg_count, Value* args);
Value builtin_response_setBody(int arg_count, Value* args);

#endif // REQUEST_RESPONSE_H
