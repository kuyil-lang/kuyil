#ifndef LIBKYLHTTP_H
#define LIBKYLHTTP_H

#include "../kuyil_types.h"

// HTTP Server functions
Value kyl_http_server(int arg_count, Value* args);
Value kyl_http_get(int arg_count, Value* args);
Value kyl_http_start_server(int arg_count, Value* args);
Value kyl_http_post(int arg_count, Value* args);
Value kyl_http_put(int arg_count, Value* args);
Value kyl_http_delete(int arg_count, Value* args);
Value kyl_http_listen(int arg_count, Value* args);

// Dynamic route registration
Value kyl_http_register_route(int arg_count, Value* args);

// HTTP Client functions
Value kyl_http_client_get(int arg_count, Value* args);
Value kyl_http_client_post(int arg_count, Value* args);

// HTTP Static file serving
Value kyl_http_static(int arg_count, Value* args);
// Additional secondary static mount registration
Value kyl_http_static_add(int arg_count, Value* args);
// Configure prefixes that bypass static serving and SPA fallback (e.g., "/api")
Value kyl_http_static_bypass(int arg_count, Value* args);
// Clear configured static bypass prefixes
Value kyl_http_static_bypass_clear(int arg_count, Value* args);

// HTTP Response builder functions
Value kyl_response_set_status(int arg_count, Value* args);
Value kyl_response_set_body(int arg_count, Value* args);
Value kyl_response_set_json(int arg_count, Value* args);
Value kyl_response_add_header(int arg_count, Value* args);

// HTTP Request accessor functions
Value kyl_request_get_method(int arg_count, Value* args);
Value kyl_request_get_path(int arg_count, Value* args);
Value kyl_request_get_body(int arg_count, Value* args);
Value kyl_request_get_param(int arg_count, Value* args);
Value kyl_request_get_header(int arg_count, Value* args);

// Lightweight JSON helpers (flat objects)
Value kyl_request_get_json_string(int arg_count, Value* args);
Value kyl_request_get_json_number(int arg_count, Value* args);
Value kyl_request_get_json_bool(int arg_count, Value* args);

// Multipart helpers
Value kyl_request_parse_multipart(int arg_count, Value* args);
Value kyl_request_get_multipart_field(int arg_count, Value* args);
// New: parse multipart and return an object of text fields keyed by name
Value kyl_request_parse_multipart_fields(int arg_count, Value* args);

// HTTP Utility functions
Value kyl_http_cleanup(int arg_count, Value* args);

#endif