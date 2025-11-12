// async_http.c - Non-blocking HTTP client using libcurl multi interface
#include "async_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <event2/event.h>

// Per-request state
struct AsyncHttpRequest {
    CURL* easy_handle;
    struct curl_slist* headers;
    char* response_body;
    size_t response_len;
    size_t response_capacity;
    AsyncHttpCallback callback;
    void* user_data;
    long status_code;
    AsyncHttpClient* client;
    struct AsyncHttpRequest* next;
};

// HTTP client with libevent integration
struct AsyncHttpClient {
    CURLM* multi_handle;
    struct event_base* event_base;
    struct event* timer_event;
    AsyncHttpRequest* requests;  // Linked list of active requests
    int still_running;
};

// Curl write callback - accumulates response data
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    AsyncHttpRequest* req = (AsyncHttpRequest*)userp;
    
    // Expand buffer if needed
    if (req->response_len + realsize >= req->response_capacity) {
        size_t new_capacity = (req->response_capacity == 0) ? 4096 : req->response_capacity * 2;
        while (new_capacity < req->response_len + realsize + 1) {
            new_capacity *= 2;
        }
        char* new_buf = realloc(req->response_body, new_capacity);
        if (!new_buf) return 0;  // Out of memory
        req->response_body = new_buf;
        req->response_capacity = new_capacity;
    }
    
    memcpy(req->response_body + req->response_len, contents, realsize);
    req->response_len += realsize;
    req->response_body[req->response_len] = '\0';
    
    return realsize;
}

// Timer callback for curl_multi_socket_action
static void timer_callback(evutil_socket_t fd, short kind, void* userp) {
    AsyncHttpClient* client = (AsyncHttpClient*)userp;
    int running_handles;
    
    curl_multi_socket_action(client->multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    async_http_process(client);
}

// Socket callback for libevent integration
static void event_callback(evutil_socket_t fd, short kind, void* userp) {
    AsyncHttpClient* client = (AsyncHttpClient*)userp;
    int action = ((kind & EV_READ) ? CURL_CSELECT_IN : 0) |
                 ((kind & EV_WRITE) ? CURL_CSELECT_OUT : 0);
    
    int running_handles;
    curl_multi_socket_action(client->multi_handle, fd, action, &running_handles);
    async_http_process(client);
}

// Curl timer function - schedules libevent timer
static int multi_timer_cb(CURLM* multi, long timeout_ms, void* userp) {
    AsyncHttpClient* client = (AsyncHttpClient*)userp;
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    evtimer_add(client->timer_event, &timeout);
    return 0;
}

AsyncHttpClient* async_http_client_create(struct event_base* event_base) {
    AsyncHttpClient* client = calloc(1, sizeof(AsyncHttpClient));
    if (!client) return NULL;
    
    client->multi_handle = curl_multi_init();
    if (!client->multi_handle) {
        free(client);
        return NULL;
    }
    
    client->event_base = event_base;
    client->requests = NULL;
    client->still_running = 0;
    
    // Create timer event for curl
    client->timer_event = evtimer_new(event_base, timer_callback, client);
    
    // Set curl multi options
    curl_multi_setopt(client->multi_handle, CURLMOPT_SOCKETFUNCTION, NULL);
    curl_multi_setopt(client->multi_handle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    curl_multi_setopt(client->multi_handle, CURLMOPT_TIMERDATA, client);
    
    return client;
}

void async_http_client_destroy(AsyncHttpClient* client) {
    if (!client) return;
    
    // Cancel all active requests
    while (client->requests) {
        async_http_cancel(client->requests);
    }
    
    if (client->timer_event) {
        event_free(client->timer_event);
    }
    
    if (client->multi_handle) {
        curl_multi_cleanup(client->multi_handle);
    }
    
    free(client);
}

AsyncHttpRequest* async_http_post(
    AsyncHttpClient* client,
    const char* url,
    const char* body,
    size_t body_len,
    AsyncHttpCallback callback,
    void* user_data
) {
    return async_http_post_ex(client, url, body, body_len, NULL, 0, 0, 0, 0, 0, callback, user_data);
}

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
) {
    if (!client || !url || !callback) return NULL;
    
    AsyncHttpRequest* req = calloc(1, sizeof(AsyncHttpRequest));
    if (!req) return NULL;
    
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        free(req);
        return NULL;
    }
    
    req->callback = callback;
    req->user_data = user_data;
    req->client = client;
    req->response_body = NULL;
    req->response_len = 0;
    req->response_capacity = 0;
    
    // Setup POST request
    curl_easy_setopt(req->easy_handle, CURLOPT_URL, url);
    curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDSIZE, body_len);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, req);
    curl_easy_setopt(req->easy_handle, CURLOPT_PRIVATE, req);
    curl_easy_setopt(req->easy_handle, CURLOPT_USERAGENT, "Kuyil-Async/1.0");
    
    // Set timeouts
    if (dns_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_DNS_CACHE_TIMEOUT, dns_timeout_ms / 1000);
    }
    if (connect_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    }
    if (read_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT_MS, read_timeout_ms);
    }
    
    // Add headers
    req->headers = NULL;
    if (headers && header_count > 0) {
        for (int i = 0; i < header_count; i++) {
            req->headers = curl_slist_append(req->headers, headers[i]);
        }
    }
    
    // Add default JSON content-type if no Content-Type header provided
    bool has_content_type = false;
    if (headers) {
        for (int i = 0; i < header_count; i++) {
            if (strncasecmp(headers[i], "Content-Type:", 13) == 0) {
                has_content_type = true;
                break;
            }
        }
    }
    if (!has_content_type) {
        req->headers = curl_slist_append(req->headers, "Content-Type: application/json");
    }
    
    if (req->headers) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->headers);
    }
    
    // Add to multi handle
    curl_multi_add_handle(client->multi_handle, req->easy_handle);
    
    // Add to request list
    req->next = client->requests;
    client->requests = req;
    
    // Kick off the request
    int running;
    curl_multi_socket_action(client->multi_handle, CURL_SOCKET_TIMEOUT, 0, &running);
    
    return req;
}

AsyncHttpRequest* async_http_get(
    AsyncHttpClient* client,
    const char* url,
    AsyncHttpCallback callback,
    void* user_data
) {
    return async_http_get_ex(client, url, NULL, 0, 0, 0, 0, 0, callback, user_data);
}

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
) {
    if (!client || !url || !callback) return NULL;
    
    AsyncHttpRequest* req = calloc(1, sizeof(AsyncHttpRequest));
    if (!req) return NULL;
    
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        free(req);
        return NULL;
    }
    
    req->callback = callback;
    req->user_data = user_data;
    req->client = client;
    req->response_body = NULL;
    req->response_len = 0;
    req->response_capacity = 0;
    
    // Setup GET request
    curl_easy_setopt(req->easy_handle, CURLOPT_URL, url);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, req);
    curl_easy_setopt(req->easy_handle, CURLOPT_PRIVATE, req);
    curl_easy_setopt(req->easy_handle, CURLOPT_USERAGENT, "Kuyil-Async/1.0");
    
    // Set timeouts
    if (dns_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_DNS_CACHE_TIMEOUT, dns_timeout_ms / 1000);
    }
    if (connect_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    }
    if (read_timeout_ms > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT_MS, read_timeout_ms);
    }
    
    // Add headers
    req->headers = NULL;
    if (headers && header_count > 0) {
        for (int i = 0; i < header_count; i++) {
            req->headers = curl_slist_append(req->headers, headers[i]);
        }
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->headers);
    }
    
    // Add to multi handle
    curl_multi_add_handle(client->multi_handle, req->easy_handle);
    
    // Add to request list
    req->next = client->requests;
    client->requests = req;
    
    // Kick off the request
    int running;
    curl_multi_socket_action(client->multi_handle, CURL_SOCKET_TIMEOUT, 0, &running);
    
    return req;
}

void async_http_cancel(AsyncHttpRequest* request) {
    if (!request) return;
    
    AsyncHttpClient* client = request->client;
    
    // Remove from client's request list
    if (client->requests == request) {
        client->requests = request->next;
    } else {
        AsyncHttpRequest* prev = client->requests;
        while (prev && prev->next != request) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = request->next;
        }
    }
    
    // Remove from multi handle
    if (request->easy_handle) {
        curl_multi_remove_handle(client->multi_handle, request->easy_handle);
        curl_easy_cleanup(request->easy_handle);
    }
    
    if (request->headers) {
        curl_slist_free_all(request->headers);
    }
    
    free(request->response_body);
    free(request);
}

int async_http_process(AsyncHttpClient* client) {
    if (!client) return 0;
    
    // Perform network I/O
    int running_handles;
    curl_multi_perform(client->multi_handle, &running_handles);
    
    CURLMsg* msg;
    int msgs_left;
    
    while ((msg = curl_multi_info_read(client->multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL* easy = msg->easy_handle;
            AsyncHttpRequest* req;
            
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
            if (!req) continue;
            
            // Get HTTP status code
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &req->status_code);
            
            // Invoke callback
            if (req->callback) {
                req->callback(req->user_data, (int)req->status_code, 
                            req->response_body, req->response_len);
            }
            
            // Clean up this request
            async_http_cancel(req);
        }
    }
    
    // Count remaining requests
    int count = 0;
    AsyncHttpRequest* req = client->requests;
    while (req) {
        count++;
        req = req->next;
    }
    
    return count;
}
