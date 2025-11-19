#define _GNU_SOURCE
#include "libkylhttp.h"
#include "http_server.h"
#include "../../src/vm_task_queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

// Export interface signatures for auto-binding (lowerCamel)
__attribute__((visibility("default")))
const char* kyl_interface_signature_text =
    "http server(port: int32) -> int32\n"
    "http get(path: string, handler: string) -> bool\n"
    "http post(path: string, handler: string) -> bool\n"
    "http put(path: string, handler: string) -> bool\n"
    "http delete(path: string, handler: string) -> bool\n"
    "http listen(port: int32) -> bool\n"
    "http startServer(port: int32) -> bool\n"
    "http registerRoute(method: string, path: string, handler: string) -> bool\n"
    "http clientGet(url: string) -> string\n"
    "http clientPost(url: string, body: string) -> string\n"
    "http cleanup() -> bool\n"
    "http static(root: string) -> bool\n"
    "http staticAdd(route: string, dir: string) -> bool\n"
    "http staticBypass(path: string) -> bool\n"
    "http staticBypassClear() -> bool\n"
    "response setStatus(response: int32, status: int32) -> bool\n"
    "response setBody(response: int32, body: string) -> bool\n"
    "response setJson(response: int32, json: string) -> bool\n"
    "response addHeader(response: int32, name: string, value: string) -> bool\n"
    "request getMethod(request: int32) -> string\n"
    "request getPath(request: int32) -> string\n"
    "request getBody(request: int32) -> string\n"
    "request getParam(request: int32, name: string) -> string\n"
    "request getHeader(request: int32, name: string) -> string\n"
    "request parseMultipart(request: int32) -> bool\n"
    "request getMultipartField(request: int32, name: string) -> string\n"
    "request parseMultipartFields(request: int32) -> object\n"
    "request parseMultipartFile(request: int32, fieldname: string) -> object\n"
    "request saveMultipartFile(request: int32, fieldname: string, destpath: string) -> bool\n"
    "request getJsonString(request: int32, path: string) -> string\n"
    "request getJsonNumber(request: int32, path: string) -> float64\n"
    "request getJsonBool(request: int32, path: string) -> bool\n";

// Helper: strnstr implementation for portability (not in glibc)
static const char* strnstr(const char* haystack, const char* needle, size_t len) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;
    if (len < needle_len) return NULL;
    
    for (size_t i = 0; i <= len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

// Global HTTP server instance (for simplicity in this demo)
static HttpServer* g_http_server = NULL;
// Workspace root (default). Can be changed via /bridge/workspace endpoint.
static char g_workspace_root[PATH_MAX] = {0};
static char g_kuyil_binary[PATH_MAX] = {0};

static void ensure_default_workspace() {
    if (g_workspace_root[0] != '\0') return;
    const char* home = getenv("HOME");
    if (!home) home = ".";
    snprintf(g_workspace_root, sizeof(g_workspace_root), "%s/%s", home, "kuyil-workspace-default");
    // mkdir -p style (single level ok). For nested, user can precreate.
    mkdir(g_workspace_root, 0755);
    if (g_kuyil_binary[0] == '\0') {
        ssize_t r = readlink("/proc/self/exe", g_kuyil_binary, sizeof(g_kuyil_binary)-1);
        if (r > 0) g_kuyil_binary[r] = '\0';
    }
}

static char* build_workspace_path(const char* filename, char* out, size_t out_sz) {
    ensure_default_workspace();
    snprintf(out, out_sz, "%s/%s", g_workspace_root, filename);
    return out;
}

// Recursively list workspace entries (files and directories) and append JSON strings
static void list_append_recursive(const char* root, const char* rel, char* out, size_t out_cap, size_t* pos, int* first) {
    char dirpath[PATH_MAX];
    if (rel && rel[0]) snprintf(dirpath, sizeof(dirpath), "%s/%s", root, rel);
    else snprintf(dirpath, sizeof(dirpath), "%s", root);
    DIR* d = opendir(dirpath);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (name[0] == '.') continue; // skip dotfiles/dirs
        char relchild[PATH_MAX];
        if (rel && rel[0]) snprintf(relchild, sizeof(relchild), "%s/%s", rel, name);
        else snprintf(relchild, sizeof(relchild), "%s", name);
        char full[PATH_MAX]; snprintf(full, sizeof(full), "%s/%s", root, relchild);
        struct stat st; if (stat(full, &st) != 0) continue;
        // append entry
        if (!*first) {
            if (*pos >= out_cap - 1) { closedir(d); return; }
            out[(*pos)++] = ',';
        }
        if (S_ISDIR(st.st_mode)) {
            size_t remaining = (out_cap > *pos) ? (out_cap - *pos) : 0;
            if (remaining == 0) { closedir(d); return; }
            int n = snprintf(out + *pos, remaining, "\"%s/\"", relchild);
            if (n > 0) {
                if ((size_t)n >= remaining) { *pos = out_cap - 1; }
                else { *pos += (size_t)n; }
            }
            *first = 0;
            // recurse
            list_append_recursive(root, relchild, out, out_cap, pos, first);
        } else if (S_ISREG(st.st_mode)) {
            size_t remaining = (out_cap > *pos) ? (out_cap - *pos) : 0;
            if (remaining == 0) { closedir(d); return; }
            int n = snprintf(out + *pos, remaining, "\"%s\"", relchild);
            if (n > 0) {
                if ((size_t)n >= remaining) { *pos = out_cap - 1; }
                else { *pos += (size_t)n; }
            }
            *first = 0;
        }
    }
    closedir(d);
}

// Simple pointer registry to safely pass C pointers to Kuyil (avoid double precision loss)
#define HTTP_MAX_POINTERS 2048
static void* g_http_ptrs[HTTP_MAX_POINTERS];
static int g_http_next = 1;

static int http_register_ptr(void* p) {
    if (!p) return 0;
    if (g_http_next >= HTTP_MAX_POINTERS) return 0;
    int h = g_http_next++;
    g_http_ptrs[h] = p;
    return h;
}

static void* http_get_ptr(int h) {
    if (h <= 0 || h >= HTTP_MAX_POINTERS) return NULL;
    return g_http_ptrs[h];
}

static void http_unregister_ptr(int h) {
    if (h > 0 && h < HTTP_MAX_POINTERS) g_http_ptrs[h] = NULL;
}

// ========== Minimal JSON parsing helpers (naive, flat object only) ==========
// These helpers are intentionally simple: they scan the request body for a
// key and extract a JSON string/number/bool value without a full parser.
// They handle cases like: {"key":"value"}, {"key": 123.45}, {"key": true}

static const char* json_skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

static char* json_extract_string_value(const char* body, const char* key) {
    if (!body || !key) return NULL;
    // Build marker "key"
    size_t keylen = strlen(key);
    size_t marker_len = keylen + 3; // quotes around key plus one quote
    char* marker = malloc(marker_len + 1);
    if (!marker) return NULL;
    marker[0] = '"';
    memcpy(marker + 1, key, keylen);
    marker[1 + keylen] = '"';
    marker[2 + keylen] = '\0';

    const char* p = strstr(body, marker);
    free(marker);
    if (!p) return NULL;
    p += (2 + keylen); // past "key"
    p = json_skip_ws(p);
    if (*p != ':') return NULL;
    p++;
    p = json_skip_ws(p);
    if (*p != '"') return NULL;
    p++;
    const char* start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) { p += 2; continue; }
        p++;
    }
    if (*p != '"') return NULL;
    size_t len = (size_t)(p - start);
    char* out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

// Parse multipart/form-data and extract a specific file part by field name
// Returns "filename|content" string for the specified file field
// If field not found or not a file, returns "|"
Value kyl_request_parse_multipart_file(int arg_count, Value* args) {
    Value out; out.type = VALUE_STRING; out.as.string = strdup("|");
    
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return out;
    }

    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* target_field = args[1].as.string;
    
    if (!req || !req->body || req->body_length == 0 || !target_field) {
        return out;
    }

    const char* ct = http_request_get_header(req, "Content-Type");
    if (!ct || strstr(ct, "multipart/form-data") == NULL) {
        return out;
    }

    const char* bstart = strstr(ct, "boundary=");
    if (!bstart) {
        return out;
    }
    bstart += 9; // skip 'boundary='

    char boundary[256];
    int bi = 0;
    while (*bstart && *bstart != '"' && *bstart != ';' && *bstart != ' ' && bi < 255) {
        boundary[bi++] = *bstart++;
    }
    boundary[bi] = '\0';

    char marker[260];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t marker_len = strlen(marker);

    const char* body = req->body;
    size_t body_len = req->body_length;

    const char* scan = body;
    const char* body_end = body + body_len;

    // Find the first boundary
    const char* part = NULL;
    for (const char* p = scan; p <= body_end - (ptrdiff_t)marker_len; p++) {
        if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
    }
    if (!part) { return out; }

    // Iterate over parts to find the target file field
    while (part && part < body_end) {
        // Check for end marker
        if ((part + 2) <= body_end && part[0] == '-' && part[1] == '-') break;

        // Skip leading CRLF or LF
        if (part[0] == '\r' && (part + 1) < body_end && part[1] == '\n') part += 2;
        else if (part[0] == '\n') part += 1;

        // Locate headers end
        const char* headers_end = NULL;
        const char* content_start = NULL;
        for (const char* p = part; p < body_end - 3; p++) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                headers_end = p; content_start = p + 4; break;
            } else if (p[0] == '\n' && p[1] == '\n') {
                headers_end = p; content_start = p + 2; break;
            }
        }
        if (!headers_end || !content_start) break;

        // Parse Content-Disposition for name and filename
        char field_name[256] = {0};
        char filename[256] = {0};
        int is_file = 0;
        const char* disp = strnstr(part, "Content-Disposition:", headers_end - part);
        if (disp) {
            const char* n = strstr(disp, "name=\"");
            if (n && n < headers_end) {
                n += 6; int k = 0; while (n < headers_end && *n != '"' && k < 255) { field_name[k++] = *n++; } field_name[k] = '\0';
            }
            const char* fn = strstr(disp, "filename=\"");
            if (fn && fn < headers_end) {
                is_file = 1;
                fn += 10; int k = 0; while (fn < headers_end && *fn != '"' && k < 255) { filename[k++] = *fn++; } filename[k] = '\0';
            }
        }

        // Find end of this part's content
        const char* content_end = NULL;
        char end_marker[264]; size_t end_marker_len = 0; int used_crlf = 0;
        snprintf(end_marker, sizeof(end_marker), "\r\n--%s", boundary);
        end_marker_len = strlen(end_marker); used_crlf = 1;
        for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
            if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
        }
        if (!content_end) {
            snprintf(end_marker, sizeof(end_marker), "\n--%s", boundary);
            end_marker_len = strlen(end_marker); used_crlf = 0;
            for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
                if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
            }
        }
        if (!content_end) { content_end = body_end; }

        // Check if this is the target file field
        if (is_file && field_name[0] != '\0' && strcmp(field_name, target_field) == 0) {
            // Found it! Build "filename|content" result
            size_t fname_len = strlen(filename);
            size_t content_len = (size_t)(content_end - content_start);
            size_t result_len = fname_len + 1 + content_len + 1;
            
            char* result_str = (char*)malloc(result_len);
            if (!result_str) { return out; }
            
            memcpy(result_str, filename, fname_len);
            result_str[fname_len] = '|';
            memcpy(result_str + fname_len + 1, content_start, content_len);
            result_str[fname_len + 1 + content_len] = '\0';
            
            free(out.as.string); // Free the default "|"
            out.as.string = result_str;
            return out;
        }

        // Advance to next part
        const char* after_boundary = content_end + end_marker_len;
        if ((after_boundary + 2) <= body_end && after_boundary[0] == '-' && after_boundary[1] == '-') {
            break;
        }
        if (used_crlf) {
            if ((after_boundary + 2) <= body_end && after_boundary[0] == '\r' && after_boundary[1] == '\n') {
                after_boundary += 2;
            }
        } else {
            if ((after_boundary + 1) <= body_end && after_boundary[0] == '\n') {
                after_boundary += 1;
            }
        }

        part = NULL;
        for (const char* p = after_boundary; p <= body_end - (ptrdiff_t)marker_len; p++) {
            if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
        }
        if (!part) break;
    }

    return out; // Not found or not a file
}

static int json_extract_bool_value(const char* body, const char* key, int* out_bool) {
    if (!body || !key || !out_bool) return 0;
    size_t keylen = strlen(key);
    char* marker = malloc(keylen + 3);
    if (!marker) return 0;
    marker[0] = '"'; memcpy(marker + 1, key, keylen); marker[1 + keylen] = '"'; marker[2 + keylen] = '\0';
    const char* p = strstr(body, marker);
    free(marker);
    if (!p) return 0;
    p += (2 + keylen);
    p = json_skip_ws(p);
    if (*p != ':') return 0;
    p++; p = json_skip_ws(p);
    if (strncmp(p, "true", 4) == 0) { *out_bool = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out_bool = 0; return 1; }
    return 0;
}

static int json_extract_number_value(const char* body, const char* key, double* out_num) {
    if (!body || !key || !out_num) return 0;
    size_t keylen = strlen(key);
    char* marker = malloc(keylen + 3);
    if (!marker) return 0;
    marker[0] = '"'; memcpy(marker + 1, key, keylen); marker[1 + keylen] = '"'; marker[2 + keylen] = '\0';
    const char* p = strstr(body, marker);
    free(marker);
    if (!p) return 0;
    p += (2 + keylen);
    p = json_skip_ws(p);
    if (*p != ':') return 0;
    p++; p = json_skip_ws(p);
    // Parse a simple JSON number: -?\d+(\.\d+)?([eE][+-]?\d+)?
    char* endptr = NULL;
    double val = strtod(p, &endptr);
    if (endptr == p) return 0; // no conversion
    *out_num = val;
    return 1;
}

// HTTP server creation function - creates or returns existing server
Value kyl_http_server(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    int port = (int)args[0].as.number;
    
    // Initialize HTTP subsystem if not already done
    if (!g_http_server) {
        http_init();
        g_http_server = http_server_create(port);
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = port;
    return result;
}

// HTTP GET route registration
Value kyl_http_get(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* path = args[0].as.string;
    // args[1] should be a callback function - for now we'll store the path
    
    if (g_http_server) {
        // Register the route with the HTTP server
        http_server_get(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP GET route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP POST route registration
Value kyl_http_post(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_post(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP POST route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP PUT route registration
Value kyl_http_put(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_put(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP PUT route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP DELETE route registration
Value kyl_http_delete(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* path = args[0].as.string;
    
    if (g_http_server) {
        http_server_delete(g_http_server, path, NULL); // TODO: Handle callback properly
        printf("HTTP DELETE route registered: %s\n", path);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(path);
    return result;
}

// HTTP server listen function
Value kyl_http_listen(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    int port = (int)args[0].as.number;
    
    if (g_http_server) {
        printf("HTTP server listening on port %d\n", port);
        // Start the server (blocking call)
        http_server_listen(g_http_server);
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// HTTP client GET function
Value kyl_http_client_get(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* url = args[0].as.string;
    HttpResponse* response = http_get(url);
    
    Value result;
    if (response && response->data) {
        result.type = VALUE_STRING;
        result.as.string = strdup(response->data);
        http_client_response_free(response);
    } else {
        result.type = VALUE_NIL;
    }
    
    return result;
}

// Alias for interface binding (camelCase)
Value kyl_http_clientGet(int arg_count, Value* args) {
    return kyl_http_client_get(arg_count, args);
}

// HTTP client POST function
Value kyl_http_client_post(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* url = args[0].as.string;
    const char* data = args[1].as.string;
    HttpResponse* response = http_post(url, data);
    
    Value result;
    if (response && response->data) {
        result.type = VALUE_STRING;
        result.as.string = strdup(response->data);
        http_client_response_free(response);
    } else {
        result.type = VALUE_NIL;
    }
    
    return result;
}

// Alias for interface binding (camelCase)
Value kyl_http_clientPost(int arg_count, Value* args) {
    return kyl_http_client_post(arg_count, args);
}

// HTTP static file serving: supports either (root) or (url_prefix, root)
Value kyl_http_static(int arg_count, Value* args) {
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    if (!g_http_server) return result;

    if (arg_count == 1 && args[0].type == VALUE_STRING) {
        const char* root_path = args[0].as.string;
        // When only root path is provided, mount at "/" (root)
        http_server_set_static_mount(g_http_server, "/", root_path);
        printf("HTTP static root set to: %s\n", root_path);
        result.type = VALUE_BOOL; result.as.boolean = true; return result;
    }
    if (arg_count == 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
        const char* url_prefix = args[0].as.string;
        const char* root_path = args[1].as.string;
        http_server_set_static_mount(g_http_server, url_prefix, root_path);
        printf("HTTP static mount %s -> %s configured\n", url_prefix, root_path);
        result.type = VALUE_BOOL; result.as.boolean = true; return result;
    }
    return result;
}

// Register an additional secondary static mount: args = (url_prefix, root_path)
Value kyl_http_static_add(int arg_count, Value* args) {
    Value result = (Value){VALUE_NIL};
    if (!g_http_server) return result;
    if (arg_count == 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
        const char* url_prefix = args[0].as.string;
        const char* root_path = args[1].as.string;
        http_server_add_static_mount(g_http_server, url_prefix, root_path);
        printf("HTTP secondary static mount %s -> %s configured\n", url_prefix, root_path);
        result.type = VALUE_BOOL; result.as.boolean = true; return result;
    }
    return result;
}

// Configure prefixes that bypass static serving and SPA fallback
// Usage from Kuyil: http_static_bypass("/api") or multiple calls for more prefixes
Value kyl_http_static_bypass(int arg_count, Value* args) {
    Value result = (Value){VALUE_NIL};
    if (!g_http_server) return result;
    if (arg_count == 1 && args[0].type == VALUE_STRING) {
        const char* url_prefix = args[0].as.string;
        http_server_add_static_bypass_prefix(g_http_server, url_prefix);
        result.type = VALUE_BOOL; result.as.boolean = true; return result;
    }
    return result;
}

// Clear all configured static bypass prefixes
Value kyl_http_static_bypass_clear(int arg_count, Value* args) {
    (void)args; (void)arg_count;
    Value result = (Value){VALUE_BOOL};
    result.as.boolean = false;
    if (!g_http_server) return result;
    http_server_clear_static_bypass_prefixes(g_http_server);
    result.as.boolean = true;
    return result;
}

// HTTP server cleanup
Value kyl_http_cleanup(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    if (g_http_server) {
        http_server_stop(g_http_server);
        http_server_free(g_http_server);
        g_http_server = NULL;
        http_cleanup();
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// ========== New Dynamic Request/Response Functions ==========

// Structure to hold Kuyil callback information
typedef struct {
    char* handler_name;   // Name of Kuyil handler function
    void* vm;            // VM instance for callback execution
} KuyilRouteCallback;

// Callback into Kuyil VM setter (provided by main VM via dlsym)
typedef bool (*kuyil_call_fn)(Value function_value, int arg_count, Value* args, Value* result_out);
static kuyil_call_fn g_call_kuyil = NULL;

// Exported setter for VM to provide the callback function
__attribute__((visibility("default")))
void http_set_kuyil_caller(void* fn_ptr) {
    g_call_kuyil = (kuyil_call_fn)fn_ptr;
    fprintf(stderr, "[HTTP] http_set_kuyil_caller installed: %p\n", fn_ptr);
}

// Background listener thread for non-blocking server start
typedef struct {
    HttpServer* server;
} ListenThreadArgs;

static void* http_listen_thread_fn(void* arg) {
    ListenThreadArgs* a = (ListenThreadArgs*)arg;
    if (a && a->server) {
        http_server_listen(a->server);
    }
    free(a);
    return NULL;
}

// Start HTTP server in background thread: http_start_server(port)
Value kyl_http_start_server(int arg_count, Value* args) {
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = false;
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        return result;
    }
    int port = (int)args[0].as.number;
    if (!g_http_server) {
        http_init();
        g_http_server = http_server_create(port);
    }
    // Spawn listener thread if not already running
    if (g_http_server && !http_server_is_running(g_http_server)) {
        ListenThreadArgs* a = (ListenThreadArgs*)malloc(sizeof(ListenThreadArgs));
        if (!a) return result;
        a->server = g_http_server;
        pthread_t t;
        int rc = pthread_create(&t, NULL, http_listen_thread_fn, a);
        if (rc == 0) {
            pthread_detach(t);
            result.as.boolean = true;
        } else {
            free(a);
        }
    }
    return result;
}

// Callback executed on main thread via task queue
static void execute_kuyil_handler_main_thread(void* data) {
    HttpTaskContext* ctx = (HttpTaskContext*)data;
    if (!ctx) return;
    
    // Get VM task queue access
    extern TaskQueue* vm_get_task_queue(void);
    extern Value vm_lookup_global(const char* name);
    
    // Get request/response objects from handles
    HttpRequest* req = (HttpRequest*)http_get_ptr(ctx->req_handle);
    HttpResponseBuilder* res = (HttpResponseBuilder*)http_get_ptr(ctx->res_handle);
    
    if (!req || !res || !ctx->handler_name) {
        http_task_context_signal(ctx);
        return;
    }
    
    // Look up handler function
    Value handler_func = vm_lookup_global(ctx->handler_name);
    
    if (handler_func.type == VALUE_FUNCTION && g_call_kuyil) {
        // Prepare arguments
        Value req_obj; req_obj.type = VALUE_NUMBER; req_obj.as.number = (double)ctx->req_handle;
        Value res_obj; res_obj.type = VALUE_NUMBER; res_obj.as.number = (double)ctx->res_handle;
        Value args[2] = {req_obj, res_obj};
        
        // Call Kuyil handler on main thread (thread-safe!)
        Value result;
        g_call_kuyil(handler_func, 2, args, &result);
    }
    
    // Signal completion to waiting worker thread
    http_task_context_signal(ctx);
}

// C callback that bridges to Kuyil function
static void route_handler_bridge(HttpRequest* req, HttpResponseBuilder* res, void* user_data) {
    if (!req || !res) {
        return;
    }
    // Continue even if user_data is NULL (fallback will handle known endpoints)
    KuyilRouteCallback* callback_info = (KuyilRouteCallback*)user_data;
    
    // Register pointers and pass handles as numbers
    int req_h = http_register_ptr((void*)req);
    int res_h = http_register_ptr((void*)res);
    
    // THREADING FIX: Skip VM lookup for dummy handlers or when handler lookup would be unsafe
    bool use_fallback = (!callback_info || !callback_info->handler_name || 
                          strcmp(callback_info->handler_name, "___C_FALLBACK___") == 0);

    if (!use_fallback && g_call_kuyil && callback_info && callback_info->handler_name) {
        // THREAD-SAFE APPROACH: Enqueue task to main thread
        extern TaskQueue* vm_get_task_queue(void);
        TaskQueue* queue = vm_get_task_queue();
        
        if (queue) {
            // Create task context
            HttpTaskContext* ctx = http_task_context_create(req_h, res_h, callback_info->handler_name);
            if (ctx) {
                // Enqueue task to main thread
                Task task = {
                    .callback = execute_kuyil_handler_main_thread,
                    .data = ctx
                };
                
                if (task_queue_enqueue_timeout(queue, task, 5000)) {  // 5 second enqueue timeout
                    // Wait for handler to complete (30 second timeout)
                    http_task_context_wait(ctx, 30000);
                    
                    // Handler executed successfully on main thread
                    http_task_context_destroy(ctx);
                    http_unregister_ptr(req_h);
                    http_unregister_ptr(res_h);
                    return;
                } else {
                    // Enqueue failed - fall through to C fallback
                    http_task_context_destroy(ctx);
                    use_fallback = true;
                }
            } else {
                use_fallback = true;
            }
        } else {
            // No task queue available - fall through to C fallback
            use_fallback = true;
        }
    }
    
    if (use_fallback) {
        // Use built-in C fallback for known endpoints (thread-safe, no VM calls)
        const char* method = (req && req->method) ? req->method : NULL;
        const char* path = (req && req->path) ? req->path : NULL;
        const char* body = (req && req->body) ? req->body : NULL;
        bool ok = false;  // Track if fallback handled the request

        // Always add CORS headers for browser-based callers
        http_response_add_header(res, "Access-Control-Allow-Origin", "*");
        http_response_add_header(res, "Access-Control-Allow-Methods", "POST, OPTIONS");
        http_response_add_header(res, "Access-Control-Allow-Headers", "Content-Type");

        if (method && strcmp(method, "OPTIONS") == 0) {
            // Preflight response
            http_response_set_status(res, 204);
            http_response_set_body(res, "", 0);
            // Consider fallback handled successfully
            ok = true;
        } else if (path && strcmp(path, "/bridge/generate") == 0) {
            // Known IDE proxy endpoint: forward to Ollama
            const char* upstream = "http://127.0.0.1:11434/api/generate";
            HttpResponse* fwd = http_post(upstream, body ? body : "");
            if (fwd && fwd->data) {
                http_response_set_status(res, 200);
                http_response_set_json(res, fwd->data);
                http_client_response_free(fwd);
                ok = true;
            } else {
                http_response_set_status(res, 502);
                http_response_set_json(res, "{\"error\":\"ollama unavailable\"}");
                ok = false;
            }
        } else if (path && strcmp(path, "/bridge/list") == 0) {
            // Recursive list of files and directories under workspace root
            ensure_default_workspace();
            // Validate workspace exists before attempting traversal
            struct stat ws_st;
            if (stat(g_workspace_root, &ws_st) != 0 || !S_ISDIR(ws_st.st_mode)) {
                http_response_set_status(res, 500);
                http_response_set_json(res, "{\"success\":false,\"error\":\"workspace not accessible\"}");
                ok = true;
            } else {
                char* buffer = malloc(131072);
                if (!buffer) {
                    http_response_set_status(res, 500);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"oom\"}");
                    ok = true;
                } else {
                    // TEMP: Return empty list to confirm IDE stability
                    snprintf(buffer, 131072, "{\"success\":true,\"workspace\":\"%s\",\"files\":[]}", g_workspace_root);
                    http_response_set_status(res, 200);
                    http_response_set_json(res, buffer);
                    free(buffer);
                    ok = true;
                }
            }
        } else if (path && strcmp(path, "/bridge/read") == 0 && method && strcmp(method, "POST") == 0) {
            // Read file content from workspace: {"filename":"..."}
            ensure_default_workspace();
            char* fname = json_extract_string_value(body, "filename");
            if (!fname || fname[0] == '\0') {
                http_response_set_status(res, 400);
                http_response_set_json(res, "{\"success\":false,\"error\":\"missing filename\"}");
                if (fname) free(fname);
                ok = true;
            } else {
                // Basic path sanitization
                if (strstr(fname, "..") || strchr(fname, '\n') || strchr(fname, '\r')) {
                    http_response_set_status(res, 400);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"invalid path\"}");
                    free(fname); ok = true;
                } else {
                    char full[PATH_MAX];
                    build_workspace_path(fname, full, sizeof(full));
                    FILE* rf = fopen(full, "rb");
                    if (!rf) {
                        http_response_set_status(res, 404);
                        http_response_set_json(res, "{\"success\":false,\"error\":\"not found\"}");
                        free(fname); ok = true;
                    } else {
                        fseek(rf, 0, SEEK_END); long sz = ftell(rf); fseek(rf, 0, SEEK_SET);
                        if (sz < 0) sz = 0; if (sz > 200000) sz = 200000; // cap 200KB
                        char* data = malloc((size_t)sz + 1);
                        if (!data) { fclose(rf); free(fname); http_response_set_status(res, 500); http_response_set_json(res, "{\"success\":false,\"error\":\"oom\"}"); ok = true; }
                        else {
                            size_t rd = fread(data, 1, (size_t)sz, rf);
                            data[rd] = '\0'; fclose(rf);
                            // Escape for JSON
                            size_t esc_cap = rd * 2 + 128; char* esc = malloc(esc_cap);
                            if (!esc) { free(data); free(fname); http_response_set_status(res, 500); http_response_set_json(res, "{\"success\":false,\"error\":\"oom2\"}"); ok = true; }
                            else {
                                size_t epos = 0; esc[epos++] = '{';
                                epos += snprintf(esc+epos, esc_cap-epos, "\"success\":true,\"filename\":\"%s\",\"content\":\"", fname);
                                for (size_t i=0;i<rd && epos+4<esc_cap;i++) {
                                    unsigned char c = (unsigned char)data[i];
                                    switch(c) {
                                        case '\\': esc[epos++]='\\'; esc[epos++]='\\'; break;
                                        case '"': esc[epos++]='\\'; esc[epos++]='"'; break;
                                        case '\n': esc[epos++]='\\'; esc[epos++]='n'; break;
                                        case '\r': esc[epos++]='\\'; esc[epos++]='r'; break;
                                        case '\t': esc[epos++]='\\'; esc[epos++]='t'; break;
                                        default: esc[epos++]=c; break;
                                    }
                                }
                                if (epos+3 < esc_cap) {
                                    esc[epos++]='"'; esc[epos++]='}'; esc[epos]='\0';
                                    http_response_set_status(res, 200);
                                    http_response_set_json(res, esc);
                                    ok = true;
                                } else {
                                    http_response_set_status(res, 500);
                                    http_response_set_json(res, "{\"success\":false,\"error\":\"escape overflow\"}");
                                    ok = true;
                                }
                                free(esc); free(data); free(fname);
                            }
                        }
                    }
                }
            }
        } else if (path && strcmp(path, "/bridge/workspace") == 0 && method && strcmp(method, "POST") == 0) {
            // Workspace management: {"action":"create|switch","name":"optional"}
            ensure_default_workspace();
            char action[128] = ""; char name[PATH_MAX] = "";
            if (body && strlen(body) > 0) {
                const char* act_start = strstr(body, "\"action\":\"");
                if (act_start) {
                    act_start += 10; const char* act_end = strchr(act_start, '"');
                    if (act_end && (act_end - act_start) < (int)sizeof(action)-1) {
                        strncpy(action, act_start, act_end-act_start); action[act_end-act_start]='\0';
                    }
                }
                const char* name_start = strstr(body, "\"name\":\"");
                if (name_start) {
                    name_start += 8; const char* name_end = strchr(name_start, '"');
                    if (name_end && (name_end - name_start) < (int)sizeof(name)-1) {
                        strncpy(name, name_start, name_end-name_start); name[name_end-name_start]='\0';
                    }
                }
            }
            if (name[0] == '\0') {
                // Use default if not provided
                const char* home = getenv("HOME"); if (!home) home = ".";
                snprintf(name, sizeof(name), "%s", "kuyil-workspace-default");
            }
            // Build full path relative to HOME if name isn't absolute
            char target[PATH_MAX];
            if (name[0] == '/') {
                snprintf(target, sizeof(target), "%s", name);
            } else {
                const char* home = getenv("HOME"); if (!home) home = ".";
                snprintf(target, sizeof(target), "%s/%s", home, name);
            }
            // Create directory if not exists
            mkdir(target, 0755);
            // Switch workspace
            snprintf(g_workspace_root, sizeof(g_workspace_root), "%s", target);
            char resp[PATH_MAX + 128];
            snprintf(resp, sizeof(resp), "{\"success\":true,\"workspace\":\"%s\"}", g_workspace_root);
            http_response_set_status(res, 200);
            http_response_set_json(res, resp);
            ok = true;
        } else if (path && strcmp(path, "/bridge/mkdir") == 0 && method && strcmp(method, "POST") == 0) {
            // Create directory (mkdir -p): {"path":"rel/new/folder"}
            ensure_default_workspace();
            char* rel = json_extract_string_value(body, "path");
            if (!rel || rel[0] == '\0' || strstr(rel, "..") || strchr(rel, '\n') || strchr(rel, '\r')) {
                http_response_set_status(res, 400);
                http_response_set_json(res, "{\"success\":false,\"error\":\"invalid path\"}");
                if (rel) free(rel); ok = true;
            } else {
                char pathbuf[PATH_MAX]; build_workspace_path(rel, pathbuf, sizeof(pathbuf));
                // mkdir -p behavior
                int status = 0;
                char buf[PATH_MAX]; snprintf(buf, sizeof(buf), "%s", pathbuf);
                size_t len = strlen(buf);
                if (len > 0 && buf[len-1] == '/') buf[len-1] = '\0';
                for (char* p = buf + strlen(g_workspace_root) + 1; *p; p++) {
                    if (*p == '/') { *p = '\0'; if (mkdir(buf, 0755) != 0 && errno != EEXIST) { status = -1; *p = '/'; break; } *p = '/'; }
                }
                if (status == 0) { if (mkdir(buf, 0755) != 0 && errno != EEXIST) status = -1; }
                if (status == 0) { http_response_set_status(res, 200); http_response_set_json(res, "{\"success\":true}"); }
                else { http_response_set_status(res, 500); http_response_set_json(res, "{\"success\":false,\"error\":\"mkdir failed\"}"); }
                free(rel); ok = true;
            }
        } else if (path && strcmp(path, "/bridge/rename") == 0 && method && strcmp(method, "POST") == 0) {
            // Rename/move: {"old":"a/b","new":"a/c"}
            ensure_default_workspace();
            char* oldp = json_extract_string_value(body, "old");
            char* newp = json_extract_string_value(body, "new");
            if (!oldp || !newp || oldp[0]=='\0' || newp[0]=='\0' || strstr(oldp, "..") || strstr(newp, "..") || strchr(oldp,'\n') || strchr(newp,'\n') || strchr(oldp,'\r') || strchr(newp,'\r')) {
                if (oldp) free(oldp); if (newp) free(newp);
                http_response_set_status(res, 400);
                http_response_set_json(res, "{\"success\":false,\"error\":\"invalid path\"}");
                ok = true;
            } else {
                char f_old[PATH_MAX]; char f_new[PATH_MAX];
                build_workspace_path(oldp, f_old, sizeof(f_old));
                build_workspace_path(newp, f_new, sizeof(f_new));
                // Ensure parent of new exists
                char parent[PATH_MAX]; snprintf(parent, sizeof(parent), "%s", f_new);
                char* s = strrchr(parent, '/'); if (s) { *s='\0'; if (mkdir(parent, 0755) != 0 && errno != EEXIST) { http_response_set_status(res, 500); http_response_set_json(res, "{\"success\":false,\"error\":\"mkdir parent failed\"}"); free(oldp); free(newp); ok = true; goto after_ops; } }
                if (rename(f_old, f_new) == 0) {
                    http_response_set_status(res, 200);
                    http_response_set_json(res, "{\"success\":true}");
                } else {
                    http_response_set_status(res, 500);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"rename failed\"}");
                }
                free(oldp); free(newp);
            }
after_ops:
            ;
        } else if (path && strcmp(path, "/bridge/delete") == 0 && method && strcmp(method, "POST") == 0) {
            // Delete a file or directory recursively: {"path":"rel/path"}
            // Delete a file or directory recursively: {"path":"rel/path"}
            ensure_default_workspace();
            char* rel = json_extract_string_value(body, "path");
            if (!rel || rel[0] == '\0' || strstr(rel, "..") || strchr(rel, '\n') || strchr(rel, '\r')) {
                http_response_set_status(res, 400);
                http_response_set_json(res, "{\"success\":false,\"error\":\"invalid path\"}");
                if (rel) free(rel); ok = true;
            } else {
                char full[PATH_MAX]; build_workspace_path(rel, full, sizeof(full));
                struct stat st;
                if (lstat(full, &st) != 0) {
                    http_response_set_status(res, 404);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"not found\"}");
                    free(rel); ok = true;
                } else {
                    int status = 0;
                    // Non-recursive stack-based rm -rf
                    struct Node { char path[PATH_MAX]; int processed; };
                    struct Node stack[2048]; int sp = 0;
                    snprintf(stack[sp].path, sizeof(stack[sp].path), "%s", full); stack[sp].processed = 0; sp++;
                    while (sp > 0 && status == 0) {
                        struct Node* it = &stack[sp-1];
                        struct stat st2; if (lstat(it->path, &st2) != 0) { sp--; continue; }
                        if (!S_ISDIR(st2.st_mode)) {
                            if (unlink(it->path) != 0) status = -1; sp--; continue;
                        }
                        if (it->processed == 0) {
                            DIR* d = opendir(it->path);
                            if (!d) { status = -1; sp--; continue; }
                            struct dirent* e; int hasChild = 0;
                            while ((e = readdir(d)) != NULL) {
                                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                                char child[PATH_MAX]; snprintf(child, sizeof(child), "%s/%s", it->path, e->d_name);
                                if (sp >= 2047) { status = -1; break; }
                                snprintf(stack[sp].path, sizeof(stack[sp].path), "%s", child);
                                stack[sp].processed = 0; sp++; hasChild = 1;
                            }
                            closedir(d);
                            it->processed = 1;
                            if (!hasChild) { if (rmdir(it->path) != 0) status = -1; sp--; }
                        } else {
                            if (rmdir(it->path) != 0) status = -1; sp--;
                        }
                    }
                    if (status == 0) {
                        http_response_set_status(res, 200);
                        http_response_set_json(res, "{\"success\":true}");
                    } else {
                        http_response_set_status(res, 500);
                        http_response_set_json(res, "{\"success\":false,\"error\":\"delete failed\"}");
                    }
                    free(rel); ok = true;
                }
            }
        } else if (path && strcmp(path, "/bridge/command") == 0 && method && strcmp(method, "POST") == 0) {
            // C fallback for command endpoint - parse JSON and write file
            fprintf(stderr, "[HTTP] C fallback handling /bridge/command\n");

            // Simple JSON parsing - extract action, filename and content
            char action_raw[256] = "";
            char action_norm[256] = "";
            char filename[256] = "ai_generated.kyl";
            char content[8192] = "// AI generated code\nprint(\"Hello from AI\")\n";

            if (body && strlen(body) > 0) {
                // Extract action: "action":"..."
                const char* act_start = strstr(body, "\"action\":\"");
                if (act_start) {
                    act_start += 10; // Skip past "action":" (10 chars)
                    const char* act_end = strchr(act_start, '"');
                    if (act_end && (act_end - act_start) < (int)sizeof(action_raw) - 1) {
                        strncpy(action_raw, act_start, act_end - act_start);
                        action_raw[act_end - act_start] = '\0';
                        // normalize: lowercase and remove non-alnum
                        size_t j = 0;
                        for (size_t i = 0; action_raw[i] && j < sizeof(action_norm) - 1; i++) {
                            unsigned char c = (unsigned char)action_raw[i];
                            if ((c >= 'A' && c <= 'Z')) c = (unsigned char)(c - 'A' + 'a');
                            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                                action_norm[j++] = (char)c;
                            }
                        }
                        action_norm[j] = '\0';
                    }
                }

                // Look for "filename":"..." pattern
                const char* fn_start = strstr(body, "\"filename\":\"");
                if (fn_start) {
                    fn_start += 12; // Skip past "filename":"
                    const char* fn_end = strchr(fn_start, '"');
                    if (fn_end && (fn_end - fn_start) < 255) {
                        strncpy(filename, fn_start, fn_end - fn_start);
                        filename[fn_end - fn_start] = '\0';
                    }
                }

                // Look for "content":"..." pattern (handles escaped quotes and newlines)
                const char* content_start = strstr(body, "\"content\":\"");
                if (content_start) {
                    content_start += 11; // Skip past "content":"
                    const char* content_end = content_start;
                    int escape = 0;
                    // Find the closing quote, handling escapes
                    while (*content_end && (escape || *content_end != '"')) {
                        if (*content_end == '\\' && !escape) {
                            escape = 1;
                        } else {
                            escape = 0;
                        }
                        content_end++;
                    }

                    if (content_end > content_start && (content_end - content_start) < 8191) {
                        // Copy and unescape
                        int idx = 0;
                        const char* p = content_start;
                        while (p < content_end && idx < 8190) {
                            if (*p == '\\' && p + 1 < content_end) {
                                p++;
                                if (*p == 'n') content[idx++] = '\n';
                                else if (*p == 't') content[idx++] = '\t';
                                else if (*p == 'r') content[idx++] = '\r';
                                else if (*p == '"') content[idx++] = '"';
                                else if (*p == '\\') content[idx++] = '\\';
                                else content[idx++] = *p;
                                p++;
                            } else {
                                content[idx++] = *p++;
                            }
                        }
                        content[idx] = '\0';
                    }
                }
            }

            // Decide whether to proceed based on action or presence of filename/content
            int allow = 0;
            if (action_norm[0] == '\0') {
                // No action provided; allow if filename/content are present
                allow = 1;
            } else if (
                strcmp(action_norm, "generatecode") == 0 ||
                strcmp(action_norm, "createfile") == 0 ||
                strcmp(action_norm, "writefile") == 0 ||
                strcmp(action_norm, "savecode") == 0 ||
                strcmp(action_norm, "newfile") == 0
            ) {
                allow = 1;
            }

            if (!allow) {
                fprintf(stderr, "[HTTP] Unknown action: raw='%s' norm='%s' (proceeding only if data present)\n", action_raw, action_norm);
                // Still proceed if we have a filename and content
                if (filename[0] != '\0' && content[0] != '\0') allow = 1;
            }

            if (!allow) {
                http_response_set_status(res, 400);
                http_response_set_json(res, "{\"success\":false,\"error\":\"Unknown action\"}");
                ok = false;
            } else {
                // Write file to kuyil-ai-ide/ directory
                char filepath[PATH_MAX];
                build_workspace_path(filename, filepath, sizeof(filepath));
                FILE* f = fopen(filepath, "w");
                if (f) {
                    fprintf(f, "%s", content);
                    fclose(f);
                    fprintf(stderr, "[HTTP] Created file: %s\n", filepath);

                    // Build response JSON
                    char response[512];
                    snprintf(response, sizeof(response),
                        "{\"success\":true,\"filename\":\"%s\",\"message\":\"File created via C fallback\"}",
                        filename);
                    http_response_set_status(res, 200);
                    http_response_set_json(res, response);
                    ok = true;
                } else {
                    fprintf(stderr, "[HTTP] Failed to create file: %s\n", filepath);
                    http_response_set_status(res, 500);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"Failed to write file\"}");
                    ok = false;
                }
            }
        } else if (path && strcmp(path, "/bridge/run") == 0) {
            // Execute a Kuyil script file and return its output
            if (method && strcmp(method, "OPTIONS") == 0) {
                http_response_set_status(res, 204);
                http_response_set_body(res, "", 0);
                ok = true;
            } else if (method && strcmp(method, "POST") == 0) {
                // Parse JSON: {"filename":"..."}
                char filename[256] = "";
                if (body && strlen(body) > 0) {
                    const char* fn_start = strstr(body, "\"filename\":\"");
                    if (fn_start) {
                        fn_start += 12;
                        const char* fn_end = strchr(fn_start, '"');
                        if (fn_end && (fn_end - fn_start) < 255) {
                            strncpy(filename, fn_start, fn_end - fn_start);
                            filename[fn_end - fn_start] = '\0';
                        }
                    }
                }
                if (filename[0] == '\0') {
                    http_response_set_status(res, 400);
                    http_response_set_json(res, "{\"success\":false,\"error\":\"filename required\"}");
                    ok = false;
                } else {
                    char filepath[PATH_MAX];
                    build_workspace_path(filename, filepath, sizeof(filepath));
                    char cmd[PATH_MAX * 2];
                    // Use resolved binary if available, else ./kuyil
                    ensure_default_workspace();
                    const char* bin = (g_kuyil_binary[0] ? g_kuyil_binary : "./kuyil");
                    // Run with cwd = workspace by prepending shell cd
                    snprintf(cmd, sizeof(cmd), "cd '%s' && '%s' '%s' 2>&1", g_workspace_root, bin, filename);
                    FILE* pipe = popen(cmd, "r");
                    if (!pipe) {
                        http_response_set_status(res, 500);
                        http_response_set_json(res, "{\"success\":false,\"error\":\"failed to execute\"}");
                        ok = false;
                    } else {
                        char outbuf[8192];
                        size_t total = 0;
                        int ch;
                        while ((ch = fgetc(pipe)) != EOF) {
                            if (total + 1 < sizeof(outbuf)) outbuf[total++] = (char)ch;
                        }
                        outbuf[total] = '\0';
                        (void)pclose(pipe);
                        // JSON-escape minimal
                        char jsonbuf[16384];
                        size_t j = 0;
                        const char* prefix = "{\"success\":true,\"output\":\"";
                        memcpy(&jsonbuf[j], prefix, strlen(prefix));
                        j += strlen(prefix);
                        for (size_t i = 0; i < total && j + 4 < sizeof(jsonbuf) - 2; i++) {
                            unsigned char c = (unsigned char)outbuf[i];
                            if (c == '"' || c == '\\') { jsonbuf[j++] = '\\'; jsonbuf[j++] = (char)c; }
                            else if (c == '\n') { jsonbuf[j++] = '\\'; jsonbuf[j++] = 'n'; }
                            else if (c == '\r') { jsonbuf[j++] = '\\'; jsonbuf[j++] = 'r'; }
                            else if (c == '\t') { jsonbuf[j++] = '\\'; jsonbuf[j++] = 't'; }
                            else { jsonbuf[j++] = (char)c; }
                        }
                        jsonbuf[j++] = '"';
                        jsonbuf[j++] = '}';
                        jsonbuf[j] = '\0';
                        http_response_set_status(res, 200);
                        http_response_set_json(res, jsonbuf);
                        ok = true;
                    }
                }
            } else {
                http_response_set_status(res, 405);
                http_response_set_json(res, "{\"success\":false,\"error\":\"method not allowed\"}");
                ok = false;
            }
        } else {
            // No known fallback
            ok = false;
        }
    }
    
    // Cleanup local handles (underlying pointers are owned by HTTP server)
    http_unregister_ptr(req_h);
    http_unregister_ptr(res_h);
}

// Register a route with a Kuyil callback function
// Usage: registerRoute("GET", "/api/users/:id", "handler_function")
Value kyl_http_register_route(int arg_count, Value* args) {
    if (arg_count != 3) {
        printf("registerRoute requires 3 arguments: method, path, callback\n");
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    if (!g_http_server) {
        printf("HTTP server not initialized\n");
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    // args[0] = method (string: "GET", "POST", etc.)
    // args[1] = path (string: "/api/users/:id")
    // args[2] = callback function name
    
    if (args[0].type != VALUE_STRING || args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        printf("Method, path, and handler must be strings\n");
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* method = args[0].as.string;
    const char* path = args[1].as.string;
    
    // Create callback info structure
    KuyilRouteCallback* callback_info = malloc(sizeof(KuyilRouteCallback));
    callback_info->handler_name = strdup(args[2].as.string);
    callback_info->vm = NULL;
    
    // Register the route
    http_server_register_route(g_http_server, method, path, route_handler_bridge, callback_info);
    
    printf("HTTP route registered: %s %s\n", method, path);
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Response helper functions

// Set response status: response_set_status(res, 200)
Value kyl_response_set_status(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpResponseBuilder* res = (HttpResponseBuilder*)http_get_ptr((int)args[0].as.number);
    int status_code = (int)args[1].as.number;
    
    http_response_set_status(res, status_code);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Set response body: response_set_body(res, "Hello")
Value kyl_response_set_body(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpResponseBuilder* res = (HttpResponseBuilder*)http_get_ptr((int)args[0].as.number);
    const char* body = args[1].as.string;
    
    http_response_set_body(res, body, strlen(body));
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Set response JSON: response_set_json(res, "{\"key\": \"value\"}")
Value kyl_response_set_json(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpResponseBuilder* res = (HttpResponseBuilder*)http_get_ptr((int)args[0].as.number);
    const char* json = args[1].as.string;
    
    http_response_set_json(res, json);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Add response header: response_add_header(res, "X-Custom", "value")
Value kyl_response_add_header(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpResponseBuilder* res = (HttpResponseBuilder*)http_get_ptr((int)args[0].as.number);
    const char* name = args[1].as.string;
    const char* value = args[2].as.string;
    
    http_response_add_header(res, name, value);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

// Request accessor functions

// Get request method: request_get_method(req)
Value kyl_request_get_method(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = req->method ? strdup(req->method) : strdup("");
    return result;
}

// Get request path: request_get_path(req)
Value kyl_request_get_path(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = req->path ? strdup(req->path) : strdup("");
    return result;
}

// Get request body: request_get_body(req)
Value kyl_request_get_body(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = req->body ? strdup(req->body) : strdup("");
    return result;
}

// Extract JSON string field with default: request_get_json_string(req, key, default)
Value kyl_request_get_json_string(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* key = args[1].as.string;
    const char* def = (args[2].type == VALUE_STRING) ? args[2].as.string : "";

    char* found = json_extract_string_value(req && req->body ? req->body : NULL, key);
    Value result;
    if (found) {
        result.type = VALUE_STRING;
        result.as.string = found; // ownership transferred
    } else {
        result.type = VALUE_STRING;
        result.as.string = strdup(def ? def : "");
    }
    return result;
}

// Extract JSON number field with default: request_get_json_number(req, key, default)
Value kyl_request_get_json_number(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* key = args[1].as.string;
    double def = (args[2].type == VALUE_NUMBER) ? args[2].as.number : 0.0;

    double val = 0.0;
    Value result;
    if (json_extract_number_value(req && req->body ? req->body : NULL, key, &val)) {
        result.type = VALUE_NUMBER;
        result.as.number = val;
    } else {
        result.type = VALUE_NUMBER;
        result.as.number = def;
    }
    return result;
}

// Extract JSON bool field with default: request_get_json_bool(req, key, default)
Value kyl_request_get_json_bool(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* key = args[1].as.string;
    int def = (args[2].type == VALUE_BOOL) ? (args[2].as.boolean ? 1 : 0) : 0;

    int b = 0;
    Value result;
    if (json_extract_bool_value(req && req->body ? req->body : NULL, key, &b)) {
        result.type = VALUE_BOOL;
        result.as.boolean = (b != 0);
    } else {
        result.type = VALUE_BOOL;
        result.as.boolean = (def != 0);
    }
    return result;
}

// Get request parameter: request_get_param(req, "id")
Value kyl_request_get_param(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* name = args[1].as.string;
    
    const char* value = http_request_get_param(req, name);
    
    Value result;
    if (value) {
        result.type = VALUE_STRING;
        result.as.string = strdup(value);
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

// Get request header: request_get_header(req, "Content-Type")
Value kyl_request_get_header(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* name = args[1].as.string;
    
    const char* value = http_request_get_header(req, name);
    
    Value result;
    if (value) {
        result.type = VALUE_STRING;
        result.as.string = strdup(value);
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

// Parse multipart/form-data and extract first file: returns "filename|content"
// This native C implementation has direct byte access and handles CRLF properly
Value kyl_request_parse_multipart(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    if (!req || !req->body || req->body_length == 0) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    // Extract boundary from Content-Type header
    const char* ct = http_request_get_header(req, "Content-Type");
    if (!ct || strstr(ct, "multipart/form-data") == NULL) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    const char* boundary_start = strstr(ct, "boundary=");
    if (!boundary_start) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    boundary_start += 9; // skip "boundary="
    
    // Extract boundary (trim quotes if present)
    char boundary[256];
    int i = 0;
    while (*boundary_start && *boundary_start != '"' && *boundary_start != ';' && 
           *boundary_start != ' ' && i < 255) {
        boundary[i++] = *boundary_start++;
    }
    boundary[i] = '\0';
    
    // Build full boundary marker with "--" prefix
    char marker[260];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t marker_len = strlen(marker);
    
    // Find first boundary in body
    const char* body = req->body;
    size_t body_len = req->body_length;
    const char* part_start = NULL;
    
    for (size_t pos = 0; pos <= body_len - marker_len; pos++) {
        if (memcmp(body + pos, marker, marker_len) == 0) {
            part_start = body + pos + marker_len;
            break;
        }
    }
    
    if (!part_start || part_start >= body + body_len) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    // Skip CRLF or LF after boundary
    if (part_start[0] == '\r' && part_start[1] == '\n') part_start += 2;
    else if (part_start[0] == '\n') part_start += 1;
    
    // Find header/body separator (CRLFCRLF or LFLF)
    const char* content_start = NULL;
    const char* headers_end = NULL;
    for (const char* p = part_start; p < body + body_len - 3; p++) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
            headers_end = p;
            content_start = p + 4;
            break;
        } else if (p[0] == '\n' && p[1] == '\n') {
            headers_end = p;
            content_start = p + 2;
            break;
        }
    }
    
    if (!content_start || !headers_end) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    // Parse filename from headers
    char filename[256] = {0};
    const char* disp = strnstr(part_start, "Content-Disposition:", headers_end - part_start);
    if (disp) {
        const char* fn = strstr(disp, "filename=\"");
        if (fn && fn < headers_end) {
            fn += 10; // skip 'filename="'
            int j = 0;
            while (fn < headers_end && *fn != '"' && j < 255) {
                filename[j++] = *fn++;
            }
            filename[j] = '\0';
        }
    }
    
    // Find end of content (next boundary)
    const char* content_end = content_start;
    char end_marker[264];
    snprintf(end_marker, sizeof(end_marker), "\r\n--%s", boundary);
    size_t end_marker_len = strlen(end_marker);
    
    for (const char* p = content_start; p <= body + body_len - end_marker_len; p++) {
        if (memcmp(p, end_marker, end_marker_len) == 0) {
            content_end = p;
            break;
        }
    }
    
    // Try LF-only end marker if CRLF didn't work
    if (content_end == content_start) {
        snprintf(end_marker, sizeof(end_marker), "\n--%s", boundary);
        end_marker_len = strlen(end_marker);
        for (const char* p = content_start; p <= body + body_len - end_marker_len; p++) {
            if (memcmp(p, end_marker, end_marker_len) == 0) {
                content_end = p;
                break;
            }
        }
    }
    
    // If still not found, use end of body
    if (content_end == content_start) {
        content_end = body + body_len;
    }
    
    size_t content_len = content_end - content_start;
    
    // Build result: "filename|content"
    size_t filename_len = strlen(filename);
    size_t result_len = filename_len + 1 + content_len + 1;
    char* result_str = malloc(result_len);
    if (!result_str) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("|");
        return result;
    }
    
    memcpy(result_str, filename, filename_len);
    result_str[filename_len] = '|';
    memcpy(result_str + filename_len + 1, content_start, content_len);
    result_str[filename_len + 1 + content_len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

// Extract a specific form field value from multipart/form-data
Value kyl_request_get_multipart_field(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* field_name = args[1].as.string;
    
    if (!req || !req->body || req->body_length == 0 || !field_name) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    // Extract boundary from Content-Type header
    const char* ct = http_request_get_header(req, "Content-Type");
    if (!ct || strstr(ct, "multipart/form-data") == NULL) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    const char* boundary_start = strstr(ct, "boundary=");
    if (!boundary_start) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    boundary_start += 9; // skip "boundary="
    
    // Extract boundary
    char boundary[256];
    int i = 0;
    while (*boundary_start && *boundary_start != '"' && *boundary_start != ';' && 
           *boundary_start != ' ' && i < 255) {
        boundary[i++] = *boundary_start++;
    }
    boundary[i] = '\0';
    
    // Build search pattern: name="field_name"
    char search_pattern[512];
    snprintf(search_pattern, sizeof(search_pattern), "name=\"%s\"", field_name);
    
    // Find the field in the body
    const char* field_pos = strnstr(req->body, search_pattern, req->body_length);
    if (!field_pos) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    // Find the end of the Content-Disposition line
    const char* line_end = field_pos;
    while (line_end < req->body + req->body_length && *line_end != '\n') {
        line_end++;
    }
    if (line_end >= req->body + req->body_length) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    line_end++; // Skip the \n
    
    // Skip the blank line (could be \r\n or just \n)
    if (line_end < req->body + req->body_length && *line_end == '\r') line_end++;
    if (line_end < req->body + req->body_length && *line_end == '\n') line_end++;
    
    // Now we're at the value start
    const char* value_start = line_end;
    
    // Find the next boundary
    char boundary_marker[260];
    snprintf(boundary_marker, sizeof(boundary_marker), "\r\n--%s", boundary);
    const char* value_end = strnstr(value_start, boundary_marker, 
                                     req->body + req->body_length - value_start);
    
    // Try LF-only boundary if CRLF didn't work
    if (!value_end) {
        snprintf(boundary_marker, sizeof(boundary_marker), "\n--%s", boundary);
        value_end = strnstr(value_start, boundary_marker, 
                             req->body + req->body_length - value_start);
    }
    
    if (!value_end) {
        value_end = req->body + req->body_length;
    }
    
    // Extract and trim the value
    size_t value_len = value_end - value_start;
    char* value = malloc(value_len + 1);
    if (!value) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    memcpy(value, value_start, value_len);
    value[value_len] = '\0';
    
    // Trim trailing whitespace
    while (value_len > 0 && (value[value_len-1] == ' ' || value[value_len-1] == '\t' || 
                              value[value_len-1] == '\r' || value[value_len-1] == '\n')) {
        value[--value_len] = '\0';
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = value;
    return result;
}

// Parse multipart/form-data and return an object of text fields keyed by name
// Only non-file parts (those without filename="...") are included.
// Returned object: { name: "...", description: "...", ... }
Value kyl_request_parse_multipart_fields(int arg_count, Value* args) {
    Value out; out.type = VALUE_OBJECT; out.as.object.count = 0; out.as.object.keys = NULL; out.as.object.values = NULL;
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        return out;
    }

    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    if (!req || !req->body || req->body_length == 0) {
        return out;
    }

    const char* ct = http_request_get_header(req, "Content-Type");
    if (!ct || strstr(ct, "multipart/form-data") == NULL) {
        return out;
    }

    const char* bstart = strstr(ct, "boundary=");
    if (!bstart) {
        return out;
    }
    bstart += 9; // skip 'boundary='

    char boundary[256];
    int bi = 0;
    while (*bstart && *bstart != '"' && *bstart != ';' && *bstart != ' ' && bi < 255) {
        boundary[bi++] = *bstart++;
    }
    boundary[bi] = '\0';

    char marker[260];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t marker_len = strlen(marker);

    const char* body = req->body;
    size_t body_len = req->body_length;

    // Dynamic arrays for keys/values
    int cap = 8;
    out.as.object.keys = (char**)malloc(sizeof(char*) * cap);
    out.as.object.values = (Value*)malloc(sizeof(Value) * cap);
    out.as.object.count = 0;

    const char* scan = body;
    const char* body_end = body + body_len;

    // Find the first boundary
    const char* part = NULL;
    for (const char* p = scan; p <= body_end - (ptrdiff_t)marker_len; p++) {
        if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
    }
    if (!part) { return out; }

    // Iterate over parts
    while (part && part < body_end) {
        // Check for end marker: "--" immediately after boundary means done
        if ((part + 2) <= body_end && part[0] == '-' && part[1] == '-') break;

        // Skip leading CRLF or LF
        if (part[0] == '\r' && (part + 1) < body_end && part[1] == '\n') part += 2;
        else if (part[0] == '\n') part += 1;

        // Locate headers end (CRLFCRLF or LFLF)
        const char* headers_end = NULL;
        const char* content_start = NULL;
        for (const char* p = part; p < body_end - 3; p++) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                headers_end = p; content_start = p + 4; break;
            } else if (p[0] == '\n' && p[1] == '\n') {
                headers_end = p; content_start = p + 2; break;
            }
        }
        if (!headers_end || !content_start) break; // malformed

        // Parse Content-Disposition for name and filename
        char field_name[256] = {0};
        int is_file = 0;
        const char* disp = strnstr(part, "Content-Disposition:", headers_end - part);
        if (disp) {
            const char* n = strstr(disp, "name=\"");
            if (n && n < headers_end) {
                n += 6; int k = 0; while (n < headers_end && *n != '"' && k < 255) { field_name[k++] = *n++; } field_name[k] = '\0';
            }
            const char* fn = strstr(disp, "filename=\"");
            if (fn && fn < headers_end) { is_file = 1; }
        }

        // Find end of this part's content (next boundary)
        const char* content_end = NULL;
        char end_marker[264]; size_t end_marker_len = 0; int used_crlf = 0;
        snprintf(end_marker, sizeof(end_marker), "\r\n--%s", boundary);
        end_marker_len = strlen(end_marker); used_crlf = 1;
        for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
            if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
        }
        if (!content_end) {
            snprintf(end_marker, sizeof(end_marker), "\n--%s", boundary);
            end_marker_len = strlen(end_marker); used_crlf = 0;
            for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
                if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
            }
        }
        if (!content_end) { content_end = body_end; }

        // Include only text fields (no filename)
        if (!is_file && field_name[0] != '\0') {
            // Trim trailing whitespace from value
            const char* vs = content_start; const char* ve = content_end;
            while (ve > vs && (ve[-1] == '\r' || ve[-1] == '\n' || ve[-1] == ' ' || ve[-1] == '\t')) ve--;
            size_t vlen = (size_t)(ve - vs);
            char* vcopy = (char*)malloc(vlen + 1);
            if (!vcopy) { /* OOM: stop early */ break; }
            memcpy(vcopy, vs, vlen); vcopy[vlen] = '\0';

            if (out.as.object.count >= cap) {
                cap *= 2;
                out.as.object.keys = (char**)realloc(out.as.object.keys, sizeof(char*) * cap);
                out.as.object.values = (Value*)realloc(out.as.object.values, sizeof(Value) * cap);
            }
            out.as.object.keys[out.as.object.count] = strdup(field_name);
            out.as.object.values[out.as.object.count].type = VALUE_STRING;
            out.as.object.values[out.as.object.count].as.string = vcopy;
            out.as.object.count++;
        }

        // Advance to after boundary line that ended this part
        const char* after_boundary = content_end + end_marker_len;
        // Check for final boundary "--"
        if ((after_boundary + 2) <= body_end && after_boundary[0] == '-' && after_boundary[1] == '-') {
            break; // end
        }
        // Skip trailing CRLF or LF after boundary line
        if (used_crlf) {
            if ((after_boundary + 2) <= body_end && after_boundary[0] == '\r' && after_boundary[1] == '\n') {
                after_boundary += 2;
            }
        } else {
            if ((after_boundary + 1) <= body_end && after_boundary[0] == '\n') {
                after_boundary += 1;
            }
        }

        // The next part should start with "--boundary"
        part = NULL;
        for (const char* p = after_boundary; p <= body_end - (ptrdiff_t)marker_len; p++) {
            if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
        }
        if (!part) break; // no more parts
    }

    return out;
}

// Save multipart file directly to disk (binary-safe)
// Args: (req_handle, field_name, output_path)
// Returns: 1 on success, 0 on failure
Value kyl_request_save_multipart_file(int arg_count, Value* args) {
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = 0;
    
    FILE* debug = fopen("/tmp/save_multipart_debug.txt", "w");
    if (debug) { fprintf(debug, "Called with arg_count=%d\n", arg_count); fflush(debug); }
    
    if (arg_count != 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        if (debug) { fprintf(debug, "Argument validation failed\n"); fclose(debug); }
        return result;
    }

    HttpRequest* req = (HttpRequest*)http_get_ptr((int)args[0].as.number);
    const char* target_field = args[1].as.string;
    const char* output_path = args[2].as.string;
    
    if (debug) { fprintf(debug, "req=%p, target_field=%s, output_path=%s\n", req, target_field, output_path); fflush(debug); }
    
    if (!req || !req->body || req->body_length == 0 || !target_field || !output_path) {
        if (debug) { fprintf(debug, "NULL check failed: req=%p, body=%p, body_length=%zu\n", req, req ? req->body : NULL, req ? req->body_length : 0); fclose(debug); }

        return result;
    }

    const char* ct = http_request_get_header(req, "Content-Type");
    if (!ct || strstr(ct, "multipart/form-data") == NULL) {
        if (debug) { fprintf(debug, "Not multipart: %s\n", ct ? ct : "NULL"); fclose(debug); }
        return result;
    }

    const char* bstart = strstr(ct, "boundary=");
    if (!bstart) {
        if (debug) { fprintf(debug, "No boundary in Content-Type\n"); fclose(debug); }
        return result;
    }
    bstart += 9;

    char boundary[256];
    int bi = 0;
    while (*bstart && *bstart != '"' && *bstart != ';' && *bstart != ' ' && bi < 255) {
        boundary[bi++] = *bstart++;
    }
    boundary[bi] = '\0';
    
    if (debug) { fprintf(debug, "Boundary: [%s], body_length=%zu\n", boundary, req->body_length); fflush(debug); }

    char marker[260];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t marker_len = strlen(marker);

    const char* body = req->body;
    size_t body_len = req->body_length;
    const char* scan = body;
    const char* body_end = body + body_len;

    const char* part = NULL;
    for (const char* p = scan; p <= body_end - (ptrdiff_t)marker_len; p++) {
        if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
    }
    if (!part) { return result; }

    while (part && part < body_end) {
        if ((part + 2) <= body_end && part[0] == '-' && part[1] == '-') break;

        if (part[0] == '\r' && (part + 1) < body_end && part[1] == '\n') part += 2;
        else if (part[0] == '\n') part += 1;

        const char* headers_end = NULL;
        const char* content_start = NULL;
        for (const char* p = part; p < body_end - 3; p++) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                headers_end = p; content_start = p + 4; break;
            } else if (p[0] == '\n' && p[1] == '\n') {
                headers_end = p; content_start = p + 2; break;
            }
        }
        if (!headers_end || !content_start) break;

        char field_name[256] = {0};
        int is_file = 0;
        const char* disp = strnstr(part, "Content-Disposition:", headers_end - part);
        if (disp) {
            const char* n = strstr(disp, "name=\"");
            if (n && n < headers_end) {
                n += 6; int k = 0; while (n < headers_end && *n != '"' && k < 255) { field_name[k++] = *n++; } field_name[k] = '\0';
            }
            const char* fn = strstr(disp, "filename=\"");
            if (fn && fn < headers_end) {
                is_file = 1;
            }
        }

        const char* content_end = NULL;
        char end_marker[264]; size_t end_marker_len = 0; int used_crlf = 0;
        snprintf(end_marker, sizeof(end_marker), "\r\n--%s", boundary);
        end_marker_len = strlen(end_marker); used_crlf = 1;
        for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
            if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
        }
        if (!content_end) {
            snprintf(end_marker, sizeof(end_marker), "\n--%s", boundary);
            end_marker_len = strlen(end_marker); used_crlf = 0;
            for (const char* p = content_start; !content_end && p <= body_end - (ptrdiff_t)end_marker_len; p++) {
                if (memcmp(p, end_marker, end_marker_len) == 0) { content_end = p; break; }
            }
        }
        if (!content_end) { content_end = body_end; }

        if (is_file && field_name[0] != '\0' && strcmp(field_name, target_field) == 0) {
            if (debug) { fprintf(debug, "Found matching file field: %s\n", field_name); fflush(debug); }
            if (debug) { fprintf(debug, "content_start=%p, content_end=%p, body=%p, body_end=%p\n", content_start, content_end, body, body_end); fflush(debug); }
            FILE* f = fopen(output_path, "wb");
            if (!f) {
                if (debug) { fprintf(debug, "Failed to open output file: %s\n", output_path); fclose(debug); }
                return result;
            }
            size_t content_len = (size_t)(content_end - content_start);
            if (debug) { fprintf(debug, "Writing %zu bytes to %s\n", content_len, output_path); fflush(debug); }
            size_t written = fwrite(content_start, 1, content_len, f);
            if (debug) { fprintf(debug, "Written %zu bytes\n", written); fflush(debug); }
            fclose(f);
            if (debug) { fclose(debug); }
            result.as.number = 1;
            return result;
        }

        const char* after_boundary = content_end + end_marker_len;
        if ((after_boundary + 2) <= body_end && after_boundary[0] == '-' && after_boundary[1] == '-') {
            break;
        }
        if (used_crlf) {
            if ((after_boundary + 2) <= body_end && after_boundary[0] == '\r' && after_boundary[1] == '\n') {
                after_boundary += 2;
            }
        } else {
            if ((after_boundary + 1) <= body_end && after_boundary[0] == '\n') {
                after_boundary += 1;
            }
        }

        part = NULL;
        for (const char* p = after_boundary; p <= body_end - (ptrdiff_t)marker_len; p++) {
            if (memcmp(p, marker, marker_len) == 0) { part = p + marker_len; break; }
        }
        if (!part) break;
    }

    return result;
}
