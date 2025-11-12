#ifndef ASYNC_REQUEST_QUEUE_H
#define ASYNC_REQUEST_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

// Forward declarations
typedef struct AsyncRequestQueue AsyncRequestQueue;
typedef struct AsyncRequest AsyncRequest;

// Request types
typedef enum {
    ASYNC_REQUEST_HTTP_POST,
    ASYNC_REQUEST_HTTP_GET,
    ASYNC_REQUEST_FILE_READ,
    ASYNC_REQUEST_FILE_WRITE
} AsyncRequestType;

// Request status
typedef enum {
    ASYNC_STATUS_PENDING,
    ASYNC_STATUS_PROCESSING,
    ASYNC_STATUS_COMPLETED,
    ASYNC_STATUS_ERROR
} AsyncRequestStatus;

// HTTP request data
typedef struct {
    char* url;
    char* body;
    size_t body_len;
    char** headers;         // Array of header strings
    int header_count;
    int dns_timeout_ms;     // DNS resolution timeout
    int connect_timeout_ms; // Connection timeout
    int read_timeout_ms;    // Read timeout
    int write_timeout_ms;   // Write timeout
} AsyncHttpRequestData;

// File I/O request data
typedef struct {
    char* path;
    char* data;
    size_t data_len;
} AsyncFileRequestData;

// Request structure
struct AsyncRequest {
    int request_id;
    AsyncRequestType type;
    AsyncRequestStatus status;
    
    union {
        AsyncHttpRequestData http;
        AsyncFileRequestData file;
    } data;
    
    // Result
    char* result_data;
    size_t result_len;
    int status_code;  // For HTTP requests
    
    // Error message
    char error_message[256];
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
    struct AsyncRequest* next;
};

// Queue structure
struct AsyncRequestQueue {
    AsyncRequest* head;
    AsyncRequest* tail;
    int next_id;
    pthread_mutex_t mutex;
};

// Create request queue
AsyncRequestQueue* async_request_queue_create(void);

// Destroy request queue
void async_request_queue_destroy(AsyncRequestQueue* queue);

// Submit HTTP POST request
// Returns request ID
int async_request_http_post(AsyncRequestQueue* queue, const char* url, 
                            const char* body, size_t body_len);

// Submit HTTP POST request with options
int async_request_http_post_ex(AsyncRequestQueue* queue, const char* url, 
                                const char* body, size_t body_len,
                                const char** headers, int header_count,
                                int dns_timeout_ms, int connect_timeout_ms,
                                int read_timeout_ms, int write_timeout_ms);

// Submit HTTP GET request
int async_request_http_get(AsyncRequestQueue* queue, const char* url);

// Submit HTTP GET request with options
int async_request_http_get_ex(AsyncRequestQueue* queue, const char* url,
                               const char** headers, int header_count,
                               int dns_timeout_ms, int connect_timeout_ms,
                               int read_timeout_ms, int write_timeout_ms);

// Submit file read request
int async_request_file_read(AsyncRequestQueue* queue, const char* path);

// Submit file write request
int async_request_file_write(AsyncRequestQueue* queue, const char* path, 
                             const char* data, size_t data_len);

// Process pending requests (called from main thread)
// Returns number of requests processed
int async_request_queue_process(AsyncRequestQueue* queue, int max_requests);

// Wait for request to complete (blocking)
// Returns true if completed successfully, false on timeout/error
bool async_request_wait(AsyncRequestQueue* queue, int request_id, 
                       int timeout_ms, char** result_data, size_t* result_len);

// Check if request is complete (non-blocking)
bool async_request_is_complete(AsyncRequestQueue* queue, int request_id);

// Get request status
AsyncRequestStatus async_request_get_status(AsyncRequestQueue* queue, int request_id);

// Get result without blocking (returns NULL if not ready)
char* async_request_get_result(AsyncRequestQueue* queue, int request_id, size_t* result_len);

#endif // ASYNC_REQUEST_QUEUE_H
