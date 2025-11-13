#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "../kuyil_types.h"

// External async request queue API
extern void* async_request_queue_get_global(void);
extern int async_request_http_post(void* queue, const char* url, const char* body, size_t body_len);
extern int async_request_http_post_ex(void* queue, const char* url, const char* body, size_t body_len,
                                       const char** headers, int header_count,
                                       int dns_timeout_ms, int connect_timeout_ms,
                                       int read_timeout_ms, int write_timeout_ms);
extern int async_request_http_get(void* queue, const char* url);
extern int async_request_http_get_ex(void* queue, const char* url,
                                      const char** headers, int header_count,
                                      int dns_timeout_ms, int connect_timeout_ms,
                                      int read_timeout_ms, int write_timeout_ms);
extern int async_request_file_read(void* queue, const char* path);
extern int async_request_file_write(void* queue, const char* path, const char* data, size_t data_len);
extern bool async_request_wait(void* queue, int request_id, int timeout_ms, char** result_data, size_t* result_len);
extern bool async_request_is_complete(void* queue, int request_id);
extern int async_request_get_status(void* queue, int request_id);
extern char* async_request_get_result(void* queue, int request_id, size_t* result_len);
extern int vm_process_events(int timeout_ms);

// Get global request queue from VM
static void* get_request_queue(void) {
    return async_request_queue_get_global();
}

// asyncio.httpPost(url, body) -> requestId
// body can be string or byte array
Value kyl_asyncio_httpPost(int arg_count, Value* args) {
    fprintf(stderr, "[asyncio_httpPost] Called with arg_count=%d\n", arg_count);
    if (arg_count != 2) {
        fprintf(stderr, "asyncio.httpPost requires 2 arguments (url, body)\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    fprintf(stderr, "[asyncio_httpPost] args[0].type=%d, args[1].type=%d\n", args[0].type, args[1].type);
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.httpPost requires string URL\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* url = args[0].as.string;
    const char* body = NULL;
    size_t body_len = 0;
    
    // Handle both string and byte array for body
    if (args[1].type == VALUE_STRING) {
        body = args[1].as.string;
        body_len = strlen(body);
    } else if (args[1].type == VALUE_ARRAY) {
        // Byte array - extract raw bytes
        ValueArray* arr = &args[1].as.array;
        body_len = arr->count;
        char* body_buf = malloc(body_len);
        if (body_buf) {
            for (int i = 0; i < arr->count; i++) {
                if (arr->values[i].type == VALUE_NUMBER) {
                    body_buf[i] = (char)(int)arr->values[i].as.number;
                } else {
                    body_buf[i] = 0;
                }
            }
            body = body_buf;
        }
    } else {
        fprintf(stderr, "asyncio.httpPost body must be string or byte array\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    int request_id = async_request_http_post(queue, url, body, body_len);
    
    // Free allocated buffer if we created one
    if (args[1].type == VALUE_ARRAY && body) {
        free((void*)body);
    }
    
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.httpGet(url) -> requestId
Value kyl_asyncio_httpGet(int arg_count, Value* args) {
    if (arg_count != 1) {
        fprintf(stderr, "asyncio.httpGet requires 1 argument (url)\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.httpGet requires string argument\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* url = args[0].as.string;
    int request_id = async_request_http_get(queue, url);
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.httpGetEx(url, headers, dns_timeout, connect_timeout, read_timeout) -> requestId
// headers: array of strings like ["Authorization: Bearer token", "Accept: application/json"]
// timeouts in milliseconds (0 = use default)
Value kyl_asyncio_httpGetEx(int arg_count, Value* args) {
    if (arg_count < 1) {
        fprintf(stderr, "asyncio.httpGetEx requires at least URL argument\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.httpGetEx requires string URL\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* url = args[0].as.string;
    const char** headers = NULL;
    int header_count = 0;
    int dns_timeout = 0, connect_timeout = 0, read_timeout = 0, write_timeout = 0;
    
    // Parse headers (arg 1 - array of strings)
    if (arg_count > 1 && args[1].type == VALUE_ARRAY) {
        ValueArray* arr = &args[1].as.array;
        header_count = arr->count;
        if (header_count > 0) {
            headers = malloc(sizeof(char*) * header_count);
            for (int i = 0; i < header_count; i++) {
                if (arr->values[i].type == VALUE_STRING) {
                    headers[i] = arr->values[i].as.string;
                } else {
                    headers[i] = "";
                }
            }
        }
    }
    
    // Parse timeouts (args 2-5)
    if (arg_count > 2 && args[2].type == VALUE_NUMBER) dns_timeout = (int)args[2].as.number;
    if (arg_count > 3 && args[3].type == VALUE_NUMBER) connect_timeout = (int)args[3].as.number;
    if (arg_count > 4 && args[4].type == VALUE_NUMBER) read_timeout = (int)args[4].as.number;
    if (arg_count > 5 && args[5].type == VALUE_NUMBER) write_timeout = (int)args[5].as.number;
    
    int request_id = async_request_http_get_ex(queue, url, headers, header_count,
                                                dns_timeout, connect_timeout, read_timeout, write_timeout);
    
    if (headers) free(headers);
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.httpPostEx(url, body, headers, dns_timeout, connect_timeout, read_timeout) -> requestId
Value kyl_asyncio_httpPostEx(int arg_count, Value* args) {
    if (arg_count < 2) {
        fprintf(stderr, "asyncio.httpPostEx requires at least URL and body arguments\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.httpPostEx requires string URL\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* url = args[0].as.string;
    const char* body = NULL;
    size_t body_len = 0;
    
    // Handle body (string or byte array)
    if (args[1].type == VALUE_STRING) {
        body = args[1].as.string;
        body_len = strlen(body);
    } else if (args[1].type == VALUE_ARRAY) {
        ValueArray* arr = &args[1].as.array;
        body_len = arr->count;
        char* body_buf = malloc(body_len);
        if (body_buf) {
            for (size_t i = 0; i < body_len; i++) {
                body_buf[i] = (char)(int)arr->values[i].as.number;
            }
            body = body_buf;
        }
    }
    
    const char** headers = NULL;
    int header_count = 0;
    int dns_timeout = 0, connect_timeout = 0, read_timeout = 0, write_timeout = 0;
    
    // Parse headers (arg 2 - array of strings)
    if (arg_count > 2 && args[2].type == VALUE_ARRAY) {
        ValueArray* arr = &args[2].as.array;
        header_count = arr->count;
        if (header_count > 0) {
            headers = malloc(sizeof(char*) * header_count);
            for (int i = 0; i < header_count; i++) {
                if (arr->values[i].type == VALUE_STRING) {
                    headers[i] = arr->values[i].as.string;
                } else {
                    headers[i] = "";
                }
            }
        }
    }
    
    // Parse timeouts (args 3-6)
    if (arg_count > 3 && args[3].type == VALUE_NUMBER) dns_timeout = (int)args[3].as.number;
    if (arg_count > 4 && args[4].type == VALUE_NUMBER) connect_timeout = (int)args[4].as.number;
    if (arg_count > 5 && args[5].type == VALUE_NUMBER) read_timeout = (int)args[5].as.number;
    if (arg_count > 6 && args[6].type == VALUE_NUMBER) write_timeout = (int)args[6].as.number;
    
    int request_id = async_request_http_post_ex(queue, url, body, body_len, headers, header_count,
                                                 dns_timeout, connect_timeout, read_timeout, write_timeout);
    
    if (headers) free(headers);
    if (args[1].type == VALUE_ARRAY && body) free((void*)body);
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.waitResult(requestId, timeout_ms) -> byte array or nil
Value kyl_asyncio_waitResult(int arg_count, Value* args) {
    if (arg_count < 1 || arg_count > 2) {
        fprintf(stderr, "asyncio.waitResult requires 1-2 arguments (requestId, [timeout_ms])\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    if (args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "asyncio.waitResult requires numeric requestId\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    int timeout_ms = 0;  // 0 = wait forever
    if (arg_count == 2) {
        if (args[1].type != VALUE_NUMBER) {
            fprintf(stderr, "asyncio.waitResult timeout must be numeric\n");
            return (Value){VALUE_NIL, {.number = 0}};
        }
        timeout_ms = (int)args[1].as.number;
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    int request_id = (int)args[0].as.number;
    char* result_data = NULL;
    size_t result_len = 0;
    
    bool success = async_request_wait(queue, request_id, timeout_ms, &result_data, &result_len);
    
    if (success && result_data) {
        // Create byte array from result
        Value* values = malloc(sizeof(Value) * result_len);
        if (values) {
            for (size_t i = 0; i < result_len; i++) {
                values[i] = (Value){VALUE_NUMBER, {.number = (unsigned char)result_data[i]}};
            }
            
            ValueArray arr = {.count = result_len, .values = values};
            free(result_data);
            return (Value){VALUE_ARRAY, {.array = arr}};
        }
        free(result_data);
    }
    
    return (Value){VALUE_NIL, {.number = 0}};
}

// asyncio.isComplete(requestId) -> bool
Value kyl_asyncio_isComplete(int arg_count, Value* args) {
    if (arg_count != 1) {
        fprintf(stderr, "asyncio.isComplete requires 1 argument (requestId)\n");
        return (Value){VALUE_BOOL, {.boolean = false}};
    }
    
    if (args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "asyncio.isComplete requires numeric requestId\n");
        return (Value){VALUE_BOOL, {.boolean = false}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_BOOL, {.boolean = false}};
    }
    
    int request_id = (int)args[0].as.number;
    bool complete = async_request_is_complete(queue, request_id);
    return (Value){VALUE_BOOL, {.boolean = complete}};
}

// asyncio.getResult(requestId) -> byte array or nil
Value kyl_asyncio_getResult(int arg_count, Value* args) {
    if (arg_count != 1) {
        fprintf(stderr, "asyncio.getResult requires 1 argument (requestId)\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    if (args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "asyncio.getResult requires numeric requestId\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    int request_id = (int)args[0].as.number;
    size_t result_len = 0;
    char* result_data = async_request_get_result(queue, request_id, &result_len);
    
    if (result_data) {
        // Create byte array from result
        Value* values = malloc(sizeof(Value) * result_len);
        if (values) {
            for (size_t i = 0; i < result_len; i++) {
                values[i] = (Value){VALUE_NUMBER, {.number = (unsigned char)result_data[i]}};
            }
            
            ValueArray arr = {.count = result_len, .values = values};
            free(result_data);
            return (Value){VALUE_ARRAY, {.array = arr}};
        }
        free(result_data);
    }
    
    return (Value){VALUE_NIL, {.number = 0}};
}

// asyncio.fileRead(path) -> requestId
Value kyl_asyncio_fileRead(int arg_count, Value* args) {
    if (arg_count != 1) {
        fprintf(stderr, "asyncio.fileRead requires 1 argument (path)\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    if (args[0].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.fileRead requires string argument\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* path = args[0].as.string;
    int request_id = async_request_file_read(queue, path);
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.fileWrite(path, data) -> requestId
Value kyl_asyncio_fileWrite(int arg_count, Value* args) {
    if (arg_count != 2) {
        fprintf(stderr, "asyncio.fileWrite requires 2 arguments (path, data)\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    if (args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        fprintf(stderr, "asyncio.fileWrite requires string arguments\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NUMBER, {.number = -1}};
    }
    
    const char* path = args[0].as.string;
    const char* data = args[1].as.string;
    size_t data_len = strlen(data);
    
    int request_id = async_request_file_write(queue, path, data, data_len);
    return (Value){VALUE_NUMBER, {.number = request_id}};
}

// asyncio.awaitResult(requestId, timeout_ms) -> byte_array
// Process event loop while waiting for async request
// This enables async operations to complete in CLI mode
Value kyl_asyncio_awaitResult(int arg_count, Value* args) {
    if (arg_count < 1 || arg_count > 2) {
        fprintf(stderr, "asyncio.awaitResult requires 1 or 2 arguments (requestId, [timeout_ms])\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    if (args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "asyncio.awaitResult requires numeric request ID\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    void* queue = get_request_queue();
    if (!queue) {
        fprintf(stderr, "Async request queue not initialized\n");
        return (Value){VALUE_NIL, {.number = 0}};
    }
    
    int request_id = (int)args[0].as.number;
    int timeout_ms = (arg_count == 2 && args[1].type == VALUE_NUMBER) ? 
                     (int)args[1].as.number : 30000;  // Default 30s timeout
    
    fprintf(stderr, "[awaitResult] Starting wait for request %d, timeout=%d ms\n", request_id, timeout_ms);
    
    // Process events in a loop until request completes or timeout
    int elapsed = 0;
    int sleep_interval = 10;  // Check every 10ms
    
    while (elapsed < timeout_ms) {
        // Check if request is complete
        if (async_request_is_complete(queue, request_id)) {
            fprintf(stderr, "[awaitResult] Request complete after %d ms\n", elapsed);
            // Get the result as byte array
            size_t result_len = 0;
            char* result_data = async_request_get_result(queue, request_id, &result_len);
            
            if (!result_data) {
                return (Value){VALUE_NIL, {.number = 0}};
            }
            
            // Convert to byte array (ValueArray of numbers)
            Value* values = malloc(sizeof(Value) * result_len);
            if (!values) {
                free(result_data);
                return (Value){VALUE_NIL, {.number = 0}};
            }
            
            for (size_t i = 0; i < result_len; i++) {
                values[i] = (Value){VALUE_NUMBER, {.number = (unsigned char)result_data[i]}};
            }
            
            ValueArray arr = {.count = result_len, .values = values};
            return (Value){VALUE_ARRAY, {.array = arr}};
        }
        
        // Process event loop to allow async operations to complete
        int events_processed = vm_process_events(sleep_interval);
        if (elapsed % 1000 == 0) {  // Log every second
            fprintf(stderr, "[awaitResult] %d ms elapsed, events_processed=%d\n", elapsed, events_processed);
        }
        
        // Sleep to allow network I/O
        usleep(sleep_interval * 1000);  // Convert ms to microseconds
        
        elapsed += sleep_interval;
    }
    
    // Timeout
    fprintf(stderr, "asyncio.awaitResult: timeout after %d ms\n", timeout_ms);
    return (Value){VALUE_NIL, {.number = 0}};
}

// Interface signature for automatic function discovery
// Format: "interface_name method_name(params) -> return_type"
const char* kyl_interface_signature_text = 
    "asyncio httpPost(url: string, body: any) -> int32\n"
    "asyncio httpGet(url: string) -> int32\n"
    "asyncio httpPostEx(url: string, body: any, headers: any, dns_timeout: int32, connect_timeout: int32, read_timeout: int32, write_timeout: int32) -> int32\n"
    "asyncio httpGetEx(url: string, headers: any, dns_timeout: int32, connect_timeout: int32, read_timeout: int32, write_timeout: int32) -> int32\n"
    "asyncio waitResult(requestId: int32, timeout_ms: int32) -> any\n"
    "asyncio isComplete(requestId: int32) -> bool\n"
    "asyncio getResult(requestId: int32) -> any\n"
    "asyncio fileRead(path: string) -> int32\n"
    "asyncio fileWrite(path: string, data: string) -> int32\n"
    "asyncio awaitResult(requestId: int32, timeout_ms: int32) -> any\n";

