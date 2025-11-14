#ifndef HTTP_CLIENT_EX_H
#define HTTP_CLIENT_EX_H

#include <stddef.h>
#include <stdbool.h>

// Enhanced HTTP Response with full details
typedef struct {
    int status_code;        // HTTP status code (200, 404, 500, etc.)
    char* body;             // Response body
    size_t body_length;     // Length of body
    char** header_names;    // Response header names
    char** header_values;   // Response header values
    int header_count;       // Number of headers
    char* error_message;    // Error message if request failed
    long response_time_ms;  // Response time in milliseconds
} HttpClientResponse;

// HTTP Client request configuration
typedef struct {
    const char* url;
    const char* method;         // GET, POST, PUT, DELETE, PATCH
    const char* body;           // Request body (for POST/PUT)
    size_t body_length;         // Body length
    char** header_names;        // Custom headers
    char** header_values;
    int header_count;
    int timeout_ms;             // Request timeout
    bool follow_redirects;      // Follow HTTP redirects
    bool verify_ssl;            // Verify SSL certificates
} HttpClientRequest;

// Create enhanced HTTP client response
HttpClientResponse* http_client_response_create(void);

// Free enhanced response
void http_client_response_ex_free(HttpClientResponse* response);

// Enhanced HTTP client functions
HttpClientResponse* http_client_request_ex(const HttpClientRequest* request);
HttpClientResponse* http_client_get_ex(const char* url, char** header_names, char** header_values, int header_count);
HttpClientResponse* http_client_post_ex(const char* url, const char* body, char** header_names, char** header_values, int header_count);
HttpClientResponse* http_client_put_ex(const char* url, const char* body, char** header_names, char** header_values, int header_count);
HttpClientResponse* http_client_delete_ex(const char* url, char** header_names, char** header_values, int header_count);

// Helper functions for response headers
const char* http_client_response_get_header(const HttpClientResponse* response, const char* header_name);
bool http_client_response_has_header(const HttpClientResponse* response, const char* header_name);

#endif // HTTP_CLIENT_EX_H
