#define _POSIX_C_SOURCE 200809L
#include "http_client_ex.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <strings.h>

// Response body write callback
static size_t write_body_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    HttpClientResponse* response = (HttpClientResponse*)userp;
    
    char* ptr = realloc(response->body, response->body_length + realsize + 1);
    if (!ptr) {
        return 0; // Out of memory
    }
    
    response->body = ptr;
    memcpy(&(response->body[response->body_length]), contents, realsize);
    response->body_length += realsize;
    response->body[response->body_length] = '\0';
    
    return realsize;
}

// Response header write callback
static size_t write_header_callback(char* buffer, size_t size, size_t nitems, void* userp) {
    size_t realsize = size * nitems;
    HttpClientResponse* response = (HttpClientResponse*)userp;
    
    // Parse header line
    char* colon = strchr(buffer, ':');
    if (!colon) {
        return realsize; // Not a header line (could be status line or empty)
    }
    
    // Extract header name and value
    size_t name_len = colon - buffer;
    char* name = malloc(name_len + 1);
    if (!name) return realsize;
    
    memcpy(name, buffer, name_len);
    name[name_len] = '\0';
    
    // Trim whitespace from name
    while (name_len > 0 && (name[name_len - 1] == ' ' || name[name_len - 1] == '\t')) {
        name[--name_len] = '\0';
    }
    
    // Extract value (skip colon and whitespace)
    char* value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    
    size_t value_len = realsize - (value_start - buffer);
    // Trim trailing \r\n
    while (value_len > 0 && (value_start[value_len - 1] == '\r' || 
                              value_start[value_len - 1] == '\n' || 
                              value_start[value_len - 1] == ' ' || 
                              value_start[value_len - 1] == '\t')) {
        value_len--;
    }
    
    char* value = malloc(value_len + 1);
    if (!value) {
        free(name);
        return realsize;
    }
    
    memcpy(value, value_start, value_len);
    value[value_len] = '\0';
    
    // Add to response headers
    int new_count = response->header_count + 1;
    char** new_names = realloc(response->header_names, new_count * sizeof(char*));
    char** new_values = realloc(response->header_values, new_count * sizeof(char*));
    
    if (!new_names || !new_values) {
        free(name);
        free(value);
        return realsize;
    }
    
    response->header_names = new_names;
    response->header_values = new_values;
    response->header_names[response->header_count] = name;
    response->header_values[response->header_count] = value;
    response->header_count = new_count;
    
    return realsize;
}

// Create enhanced HTTP client response
HttpClientResponse* http_client_response_create(void) {
    HttpClientResponse* response = calloc(1, sizeof(HttpClientResponse));
    if (!response) return NULL;
    
    response->body = malloc(1);
    if (!response->body) {
        free(response);
        return NULL;
    }
    response->body[0] = '\0';
    response->body_length = 0;
    response->status_code = 0;
    response->header_count = 0;
    response->header_names = NULL;
    response->header_values = NULL;
    response->error_message = NULL;
    response->response_time_ms = 0;
    
    return response;
}

// Free enhanced response
void http_client_response_ex_free(HttpClientResponse* response) {
    if (!response) return;
    
    free(response->body);
    free(response->error_message);
    
    for (int i = 0; i < response->header_count; i++) {
        free(response->header_names[i]);
        free(response->header_values[i]);
    }
    free(response->header_names);
    free(response->header_values);
    
    free(response);
}

// Initialize curl globally (thread-safe, called once)
static void ensure_curl_initialized(void) {
    static int initialized = 0;
    if (!initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        initialized = 1;
    }
}

// Enhanced HTTP client request
HttpClientResponse* http_client_request_ex(const HttpClientRequest* request) {
    if (!request || !request->url) {
        return NULL;
    }
    
    // Ensure curl is initialized before use (required for thread safety)
    ensure_curl_initialized();
    
    HttpClientResponse* response = http_client_response_create();
    if (!response) return NULL;
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        response->error_message = strdup("Failed to initialize CURL");
        return response;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    
    // Set method
    if (request->method) {
        if (strcmp(request->method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (strcmp(request->method, "PUT") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (strcmp(request->method, "DELETE") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (strcmp(request->method, "PATCH") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        }
        // GET is default
    }
    
    // Set body if provided
    if (request->body && request->body_length > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request->body_length);
    }
    
    // Set custom headers
    struct curl_slist* headers = NULL;
    if (request->header_count > 0) {
        for (int i = 0; i < request->header_count; i++) {
            char header[1024];
            snprintf(header, sizeof(header), "%s: %s", 
                     request->header_names[i], request->header_values[i]);
            headers = curl_slist_append(headers, header);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Set callbacks
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);
    
    // Set timeout
    if (request->timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)request->timeout_ms);
    }
    
    // Set redirect following
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request->follow_redirects ? 1L : 0L);
    
    // Set SSL verification
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request->verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request->verify_ssl ? 2L : 0L);
    
    // Set user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kuyil/2.0");
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Get response time
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    response->response_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + 
                                  (end_time.tv_nsec - start_time.tv_nsec) / 1000000;
    
    if (res != CURLE_OK) {
        response->error_message = strdup(curl_easy_strerror(res));
    } else {
        // Get HTTP status code
        long status_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        response->status_code = (int)status_code;
    }
    
    // Cleanup
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    
    return response;
}

// Convenience function: GET
HttpClientResponse* http_client_get_ex(const char* url, char** header_names, char** header_values, int header_count) {
    HttpClientRequest request = {
        .url = url,
        .method = "GET",
        .body = NULL,
        .body_length = 0,
        .header_names = header_names,
        .header_values = header_values,
        .header_count = header_count,
        .timeout_ms = 30000,
        .follow_redirects = true,
        .verify_ssl = true
    };
    return http_client_request_ex(&request);
}

// Convenience function: POST
HttpClientResponse* http_client_post_ex(const char* url, const char* body, char** header_names, char** header_values, int header_count) {
    HttpClientRequest request = {
        .url = url,
        .method = "POST",
        .body = body,
        .body_length = body ? strlen(body) : 0,
        .header_names = header_names,
        .header_values = header_values,
        .header_count = header_count,
        .timeout_ms = 300000,  // 5 minutes for LLM requests
        .follow_redirects = true,
        .verify_ssl = true
    };
    return http_client_request_ex(&request);
}

// Convenience function: PUT
HttpClientResponse* http_client_put_ex(const char* url, const char* body, char** header_names, char** header_values, int header_count) {
    HttpClientRequest request = {
        .url = url,
        .method = "PUT",
        .body = body,
        .body_length = body ? strlen(body) : 0,
        .header_names = header_names,
        .header_values = header_values,
        .header_count = header_count,
        .timeout_ms = 30000,
        .follow_redirects = true,
        .verify_ssl = true
    };
    return http_client_request_ex(&request);
}

// Convenience function: DELETE
HttpClientResponse* http_client_delete_ex(const char* url, char** header_names, char** header_values, int header_count) {
    HttpClientRequest request = {
        .url = url,
        .method = "DELETE",
        .body = NULL,
        .body_length = 0,
        .header_names = header_names,
        .header_values = header_values,
        .header_count = header_count,
        .timeout_ms = 30000,
        .follow_redirects = true,
        .verify_ssl = true
    };
    return http_client_request_ex(&request);
}

// Get header value by name (case-insensitive)
const char* http_client_response_get_header(const HttpClientResponse* response, const char* header_name) {
    if (!response || !header_name) return NULL;
    
    for (int i = 0; i < response->header_count; i++) {
        if (strcasecmp(response->header_names[i], header_name) == 0) {
            return response->header_values[i];
        }
    }
    
    return NULL;
}

// Check if header exists
bool http_client_response_has_header(const HttpClientResponse* response, const char* header_name) {
    return http_client_response_get_header(response, header_name) != NULL;
}
