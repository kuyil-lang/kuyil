#define _POSIX_C_SOURCE 200809L
#include "async_request_queue.h"
#include "async_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// Global async HTTP client (set by VM)
static AsyncHttpClient* g_async_http_client = NULL;

void async_request_queue_set_http_client(AsyncHttpClient* client) {
    g_async_http_client = client;
}

AsyncRequestQueue* async_request_queue_create(void) {
    AsyncRequestQueue* queue = malloc(sizeof(AsyncRequestQueue));
    if (!queue) return NULL;
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->next_id = 1;
    pthread_mutex_init(&queue->mutex, NULL);
    
    return queue;
}

void async_request_queue_destroy(AsyncRequestQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    AsyncRequest* req = queue->head;
    while (req) {
        AsyncRequest* next = req->next;
        
        // Free request data
        if (req->type == ASYNC_REQUEST_HTTP_POST || req->type == ASYNC_REQUEST_HTTP_GET) {
            free(req->data.http.url);
            free(req->data.http.body);
        } else {
            free(req->data.file.path);
            free(req->data.file.data);
        }
        
        free(req->result_data);
        pthread_mutex_destroy(&req->mutex);
        pthread_cond_destroy(&req->cond);
        free(req);
        
        req = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

static AsyncRequest* create_request(AsyncRequestQueue* queue, AsyncRequestType type) {
    AsyncRequest* req = calloc(1, sizeof(AsyncRequest));
    if (!req) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    req->request_id = queue->next_id++;
    pthread_mutex_unlock(&queue->mutex);
    
    req->type = type;
    req->status = ASYNC_STATUS_PENDING;
    req->result_data = NULL;
    req->result_len = 0;
    req->status_code = 0;
    req->next = NULL;
    
    pthread_mutex_init(&req->mutex, NULL);
    pthread_cond_init(&req->cond, NULL);
    
    return req;
}

static void enqueue_request(AsyncRequestQueue* queue, AsyncRequest* req) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->tail) {
        queue->tail->next = req;
        queue->tail = req;
    } else {
        queue->head = queue->tail = req;
    }
    
    pthread_mutex_unlock(&queue->mutex);
}

int async_request_http_post(AsyncRequestQueue* queue, const char* url,
                            const char* body, size_t body_len) {
    return async_request_http_post_ex(queue, url, body, body_len, NULL, 0, 0, 0, 0, 0);
}

int async_request_http_post_ex(AsyncRequestQueue* queue, const char* url,
                                const char* body, size_t body_len,
                                const char** headers, int header_count,
                                int dns_timeout_ms, int connect_timeout_ms,
                                int read_timeout_ms, int write_timeout_ms) {
    AsyncRequest* req = create_request(queue, ASYNC_REQUEST_HTTP_POST);
    if (!req) return -1;
    
    req->data.http.url = strdup(url);
    req->data.http.body = malloc(body_len);
    if (req->data.http.body) {
        memcpy(req->data.http.body, body, body_len);
    }
    req->data.http.body_len = body_len;
    
    // Copy headers
    req->data.http.headers = NULL;
    req->data.http.header_count = 0;
    if (headers && header_count > 0) {
        req->data.http.headers = malloc(sizeof(char*) * header_count);
        if (req->data.http.headers) {
            for (int i = 0; i < header_count; i++) {
                req->data.http.headers[i] = strdup(headers[i]);
            }
            req->data.http.header_count = header_count;
        }
    }
    
    // Set timeouts (use defaults if 0)
    req->data.http.dns_timeout_ms = dns_timeout_ms > 0 ? dns_timeout_ms : 5000;
    req->data.http.connect_timeout_ms = connect_timeout_ms > 0 ? connect_timeout_ms : 10000;
    req->data.http.read_timeout_ms = read_timeout_ms > 0 ? read_timeout_ms : 30000;
    req->data.http.write_timeout_ms = write_timeout_ms > 0 ? write_timeout_ms : 30000;
    
    enqueue_request(queue, req);
    return req->request_id;
}

int async_request_http_get(AsyncRequestQueue* queue, const char* url) {
    return async_request_http_get_ex(queue, url, NULL, 0, 0, 0, 0, 0);
}

int async_request_http_get_ex(AsyncRequestQueue* queue, const char* url,
                               const char** headers, int header_count,
                               int dns_timeout_ms, int connect_timeout_ms,
                               int read_timeout_ms, int write_timeout_ms) {
    AsyncRequest* req = create_request(queue, ASYNC_REQUEST_HTTP_GET);
    if (!req) return -1;
    
    req->data.http.url = strdup(url);
    req->data.http.body = NULL;
    req->data.http.body_len = 0;
    
    // Copy headers
    req->data.http.headers = NULL;
    req->data.http.header_count = 0;
    if (headers && header_count > 0) {
        req->data.http.headers = malloc(sizeof(char*) * header_count);
        if (req->data.http.headers) {
            for (int i = 0; i < header_count; i++) {
                req->data.http.headers[i] = strdup(headers[i]);
            }
            req->data.http.header_count = header_count;
        }
    }
    
    // Set timeouts (use defaults if 0)
    req->data.http.dns_timeout_ms = dns_timeout_ms > 0 ? dns_timeout_ms : 5000;
    req->data.http.connect_timeout_ms = connect_timeout_ms > 0 ? connect_timeout_ms : 10000;
    req->data.http.read_timeout_ms = read_timeout_ms > 0 ? read_timeout_ms : 30000;
    req->data.http.write_timeout_ms = write_timeout_ms > 0 ? write_timeout_ms : 30000;
    
    enqueue_request(queue, req);
    return req->request_id;
}

int async_request_file_read(AsyncRequestQueue* queue, const char* path) {
    AsyncRequest* req = create_request(queue, ASYNC_REQUEST_FILE_READ);
    if (!req) return -1;
    
    req->data.file.path = strdup(path);
    req->data.file.data = NULL;
    req->data.file.data_len = 0;
    
    enqueue_request(queue, req);
    return req->request_id;
}

int async_request_file_write(AsyncRequestQueue* queue, const char* path,
                             const char* data, size_t data_len) {
    AsyncRequest* req = create_request(queue, ASYNC_REQUEST_FILE_WRITE);
    if (!req) return -1;
    
    req->data.file.path = strdup(path);
    req->data.file.data = malloc(data_len);
    if (req->data.file.data) {
        memcpy(req->data.file.data, data, data_len);
    }
    req->data.file.data_len = data_len;
    
    enqueue_request(queue, req);
    return req->request_id;
}

// HTTP completion callback
static void http_completion_callback(void* user_data, int status_code,
                                    const char* body, size_t body_len) {
    AsyncRequest* req = (AsyncRequest*)user_data;
    
    pthread_mutex_lock(&req->mutex);
    
    req->status_code = status_code;
    if (body && body_len > 0) {
        req->result_data = malloc(body_len + 1);
        if (req->result_data) {
            memcpy(req->result_data, body, body_len);
            req->result_data[body_len] = '\0';
            req->result_len = body_len;
        }
    }
    
    if (status_code >= 200 && status_code < 300) {
        req->status = ASYNC_STATUS_COMPLETED;
    } else {
        req->status = ASYNC_STATUS_ERROR;
        snprintf(req->error_message, sizeof(req->error_message),
                "HTTP error: %d", status_code);
    }
    
    pthread_cond_broadcast(&req->cond);
    pthread_mutex_unlock(&req->mutex);
}

// Process file read request
static void process_file_read(AsyncRequest* req) {
    FILE* f = fopen(req->data.file.path, "rb");
    if (!f) {
        req->status = ASYNC_STATUS_ERROR;
        snprintf(req->error_message, sizeof(req->error_message),
                "Failed to open file: %s", req->data.file.path);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    req->result_data = malloc(size + 1);
    if (req->result_data) {
        size_t read = fread(req->result_data, 1, size, f);
        req->result_data[read] = '\0';
        req->result_len = read;
        req->status = ASYNC_STATUS_COMPLETED;
    } else {
        req->status = ASYNC_STATUS_ERROR;
        snprintf(req->error_message, sizeof(req->error_message),
                "Memory allocation failed");
    }
    
    fclose(f);
}

// Process file write request
static void process_file_write(AsyncRequest* req) {
    FILE* f = fopen(req->data.file.path, "wb");
    if (!f) {
        req->status = ASYNC_STATUS_ERROR;
        snprintf(req->error_message, sizeof(req->error_message),
                "Failed to open file for writing: %s", req->data.file.path);
        return;
    }
    
    size_t written = fwrite(req->data.file.data, 1, req->data.file.data_len, f);
    fclose(f);
    
    if (written == req->data.file.data_len) {
        req->status = ASYNC_STATUS_COMPLETED;
        req->result_data = strdup("OK");
        req->result_len = 2;
    } else {
        req->status = ASYNC_STATUS_ERROR;
        snprintf(req->error_message, sizeof(req->error_message),
                "Failed to write complete file");
    }
}

int async_request_queue_process(AsyncRequestQueue* queue, int max_requests) {
    if (!queue) return 0;
    
    int processed = 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    AsyncRequest* req = queue->head;
    AsyncRequest* prev = NULL;
    
    while (req && processed < max_requests) {
        AsyncRequest* next = req->next;
        
        pthread_mutex_lock(&req->mutex);
        
        if (req->status == ASYNC_STATUS_PENDING) {
            req->status = ASYNC_STATUS_PROCESSING;
            pthread_mutex_unlock(&req->mutex);
            pthread_mutex_unlock(&queue->mutex);
            
            // Process request based on type
            if (req->type == ASYNC_REQUEST_HTTP_POST && g_async_http_client) {
                async_http_post_ex(g_async_http_client, req->data.http.url,
                              req->data.http.body, req->data.http.body_len,
                              (const char**)req->data.http.headers, req->data.http.header_count,
                              req->data.http.dns_timeout_ms, req->data.http.connect_timeout_ms,
                              req->data.http.read_timeout_ms, req->data.http.write_timeout_ms,
                              http_completion_callback, req);
                processed++;
            } else if (req->type == ASYNC_REQUEST_HTTP_GET && g_async_http_client) {
                async_http_get_ex(g_async_http_client, req->data.http.url,
                             (const char**)req->data.http.headers, req->data.http.header_count,
                             req->data.http.dns_timeout_ms, req->data.http.connect_timeout_ms,
                             req->data.http.read_timeout_ms, req->data.http.write_timeout_ms,
                             http_completion_callback, req);
                processed++;
            } else if (req->type == ASYNC_REQUEST_FILE_READ) {
                pthread_mutex_lock(&req->mutex);
                process_file_read(req);
                pthread_cond_broadcast(&req->cond);
                pthread_mutex_unlock(&req->mutex);
                processed++;
            } else if (req->type == ASYNC_REQUEST_FILE_WRITE) {
                pthread_mutex_lock(&req->mutex);
                process_file_write(req);
                pthread_cond_broadcast(&req->cond);
                pthread_mutex_unlock(&req->mutex);
                processed++;
            }
            
            pthread_mutex_lock(&queue->mutex);
        } else {
            pthread_mutex_unlock(&req->mutex);
        }
        
        prev = req;
        req = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    return processed;
}

bool async_request_wait(AsyncRequestQueue* queue, int request_id,
                       int timeout_ms, char** result_data, size_t* result_len) {
    if (!queue) return false;
    
    // Find request
    pthread_mutex_lock(&queue->mutex);
    AsyncRequest* req = queue->head;
    while (req && req->request_id != request_id) {
        req = req->next;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    if (!req) return false;
    
    // Wait for completion
    pthread_mutex_lock(&req->mutex);
    
    if (timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        while (req->status != ASYNC_STATUS_COMPLETED && 
               req->status != ASYNC_STATUS_ERROR) {
            if (pthread_cond_timedwait(&req->cond, &req->mutex, &ts) != 0) {
                pthread_mutex_unlock(&req->mutex);
                return false;  // Timeout
            }
        }
    } else {
        while (req->status != ASYNC_STATUS_COMPLETED && 
               req->status != ASYNC_STATUS_ERROR) {
            pthread_cond_wait(&req->cond, &req->mutex);
        }
    }
    
    bool success = (req->status == ASYNC_STATUS_COMPLETED);
    if (success && result_data && result_len) {
        *result_data = req->result_data ? strdup(req->result_data) : NULL;
        *result_len = req->result_len;
    }
    
    pthread_mutex_unlock(&req->mutex);
    return success;
}

bool async_request_is_complete(AsyncRequestQueue* queue, int request_id) {
    if (!queue) return false;
    
    pthread_mutex_lock(&queue->mutex);
    AsyncRequest* req = queue->head;
    while (req && req->request_id != request_id) {
        req = req->next;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    if (!req) return false;
    
    pthread_mutex_lock(&req->mutex);
    bool complete = (req->status == ASYNC_STATUS_COMPLETED || 
                     req->status == ASYNC_STATUS_ERROR);
    pthread_mutex_unlock(&req->mutex);
    
    return complete;
}

AsyncRequestStatus async_request_get_status(AsyncRequestQueue* queue, int request_id) {
    if (!queue) return ASYNC_STATUS_ERROR;
    
    pthread_mutex_lock(&queue->mutex);
    AsyncRequest* req = queue->head;
    while (req && req->request_id != request_id) {
        req = req->next;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    if (!req) return ASYNC_STATUS_ERROR;
    
    pthread_mutex_lock(&req->mutex);
    AsyncRequestStatus status = req->status;
    pthread_mutex_unlock(&req->mutex);
    
    return status;
}

char* async_request_get_result(AsyncRequestQueue* queue, int request_id, size_t* result_len) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    AsyncRequest* req = queue->head;
    while (req && req->request_id != request_id) {
        req = req->next;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    if (!req) return NULL;
    
    pthread_mutex_lock(&req->mutex);
    char* result = NULL;
    if (req->status == ASYNC_STATUS_COMPLETED && req->result_data) {
        result = strdup(req->result_data);
        if (result_len) *result_len = req->result_len;
    }
    pthread_mutex_unlock(&req->mutex);
    
    return result;
}
