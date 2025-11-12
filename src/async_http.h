#ifndef ASYNC_HTTP_H
#define ASYNC_HTTP_H

#include <stddef.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <event2/event.h>

// Forward declarations
typedef struct AsyncHttpClient AsyncHttpClient;
typedef struct AsyncHttpRequest AsyncHttpRequest;

// Callback signature for HTTP completion
// Called when HTTP request completes (success or failure)
// user_data: User-provided pointer passed during request creation
// status_code: HTTP status code (200, 404, etc.) or 0 for connection errors
// body: Response body data (may be NULL)
// body_len: Length of response body
typedef void (*AsyncHttpCallback)(void* user_data, int status_code, const char* body, size_t body_len);

// Initialize async HTTP client with libevent base
// event_base: libevent event_base for integration
// Returns: AsyncHttpClient instance or NULL on failure
AsyncHttpClient* async_http_client_create(struct event_base* event_base);

// Cleanup and destroy async HTTP client
void async_http_client_destroy(AsyncHttpClient* client);

// Make async HTTP POST request
// client: AsyncHttpClient instance
// url: Target URL
// body: POST body data
// body_len: Length of POST body
// callback: Completion callback
// user_data: User pointer passed to callback
// Returns: Request handle or NULL on failure
AsyncHttpRequest* async_http_post(
    AsyncHttpClient* client,
    const char* url,
    const char* body,
    size_t body_len,
    AsyncHttpCallback callback,
    void* user_data
);

// Make async HTTP POST request with options
AsyncHttpRequest* async_http_post_ex(
    AsyncHttpClient* client,
    const char* url,
    const char* body,
    size_t body_len,
    const char** headers,
    int header_count,
    int dns_timeout_ms,
    int connect_timeout_ms,
    int read_timeout_ms,
    int write_timeout_ms,
    AsyncHttpCallback callback,
    void* user_data
);

// Make async HTTP GET request
AsyncHttpRequest* async_http_get(
    AsyncHttpClient* client,
    const char* url,
    AsyncHttpCallback callback,
    void* user_data
);

// Make async HTTP GET request with options
AsyncHttpRequest* async_http_get_ex(
    AsyncHttpClient* client,
    const char* url,
    const char** headers,
    int header_count,
    int dns_timeout_ms,
    int connect_timeout_ms,
    int read_timeout_ms,
    int write_timeout_ms,
    AsyncHttpCallback callback,
    void* user_data
);

// Cancel an in-flight request
void async_http_cancel(AsyncHttpRequest* request);

// Process pending HTTP events (called by libevent)
// Returns: Number of active requests
int async_http_process(AsyncHttpClient* client);

#endif // ASYNC_HTTP_H
