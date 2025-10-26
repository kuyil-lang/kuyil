#ifndef LIBKYLHTTP_H
#define LIBKYLHTTP_H

#include "../kuyil_types.h"

// HTTP Server functions
Value kyl_http_server(int arg_count, Value* args);
Value kyl_http_get(int arg_count, Value* args);
Value kyl_http_post(int arg_count, Value* args);
Value kyl_http_put(int arg_count, Value* args);
Value kyl_http_delete(int arg_count, Value* args);
Value kyl_http_listen(int arg_count, Value* args);

// HTTP Client functions
Value kyl_http_client_get(int arg_count, Value* args);
Value kyl_http_client_post(int arg_count, Value* args);

// HTTP Static file serving
Value kyl_http_static(int arg_count, Value* args);

// HTTP Utility functions
Value kyl_http_cleanup(int arg_count, Value* args);

#endif