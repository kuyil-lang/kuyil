#include "library_loader.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
// dlfcn.h is already handled in library_loader.h with Windows compatibility

// Forward declarations
void load_ffmpeg_functions(SharedLibrary* lib);
void load_generic_functions(SharedLibrary* lib, const char* functions[][3], const char* category);

// Global library registry
LibraryRegistry g_library_registry = {0};
// Optional Kuyil callback bridge for libraries that accept a VM caller (e.g., HTTP)
static void* g_kuyil_caller_cb = NULL;

void library_loader_set_kuyil_caller(void* callback_ptr) {
    g_kuyil_caller_cb = callback_ptr;
}

bool library_loader_init(void) {
    if (g_library_registry.initialized) {
        return true;
    }
    
    memset(&g_library_registry, 0, sizeof(LibraryRegistry));
    g_library_registry.initialized = true;
    
    LOG_INFO("Library loader initialized");
    return true;
}

static bool is_absolute_path(const char* p) {
#ifdef _WIN32
    // Drive letter like C:\ or UNC \\
    return (strlen(p) > 2 && p[1] == ':' ) || (p[0] == '\\' && p[1] == '\\');
#else
    return p[0] == '/';
#endif
}

static void join_paths(char* out, size_t out_sz, const char* a, const char* b) {
    size_t la = strlen(a);
    bool slash = (la > 0 && a[la-1] == '/');
    snprintf(out, out_sz, slash ? "%s%s" : "%s/%s", a, b);
}

static void get_executable_dir(char* out, size_t out_sz) {
#ifdef __linux__
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (len > 0) {
        buf[len] = '\0';
        char* last = strrchr(buf, '/');
        if (last) *last = '\0';
        size_t copy_len = strlen(buf);
        if (copy_len >= out_sz) copy_len = out_sz - 1;
        memcpy(out, buf, copy_len);
        out[copy_len] = '\0';
        return;
    }
#endif
    strncpy(out, ".", out_sz-1);
    out[out_sz-1] = '\0';
}

bool load_library_config(const char* config_file) {
    FILE* file = fopen(config_file, "r");
    if (!file) {
        LOG_WARNING("Could not open library config file: %s", config_file);
        return false;
    }
    // Determine base directory of the config file; duplicate path as dirname may modify
    char cfg_path_copy[1024];
    strncpy(cfg_path_copy, config_file, sizeof(cfg_path_copy)-1);
    cfg_path_copy[sizeof(cfg_path_copy)-1] = '\0';
    char* cfg_dir = dirname(cfg_path_copy);
    
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }
        
        // Parse library definition
        // Format: library_name:path:optional[true/false]
        char* name = strtok(line, ":");
        char* path = strtok(NULL, ":");
        char* optional_str = strtok(NULL, ":\n");
        
        if (!name || !path) {
            continue;
        }
        
        bool optional = (optional_str && strcmp(optional_str, "true") == 0);
        
        // Add library to registry
        if (g_library_registry.library_count < MAX_LIBRARIES) {
            SharedLibrary* lib = &g_library_registry.libraries[g_library_registry.library_count];
            strncpy(lib->name, name, MAX_NAME_LENGTH - 1);
            // Resolve path relative to config file directory if not absolute
            if (!is_absolute_path(path)) {
                char resolved[MAX_PATH_LENGTH];
                // If path starts with ./ or libs/ etc., join with cfg_dir
                join_paths(resolved, sizeof(resolved), cfg_dir, path);
                strncpy(lib->path, resolved, MAX_PATH_LENGTH - 1);
                lib->path[MAX_PATH_LENGTH - 1] = '\0';
            } else {
                strncpy(lib->path, path, MAX_PATH_LENGTH - 1);
                lib->path[MAX_PATH_LENGTH - 1] = '\0';
            }
            lib->is_optional = optional;
            lib->is_loaded = false;
            lib->handle = NULL;
            lib->function_count = 0;
            
            g_library_registry.library_count++;
            LOG_INFO("Registered library: %s (path: %s, optional: %s)", 
                    name, lib->path, optional ? "true" : "false");
        }
    }
    
    fclose(file);
    return true;
}

bool load_library(const char* library_name) {
    SharedLibrary* lib = NULL;
    
    // Find library in registry
    for (int i = 0; i < g_library_registry.library_count; i++) {
        if (strcmp(g_library_registry.libraries[i].name, library_name) == 0) {
            lib = &g_library_registry.libraries[i];
            break;
        }
    }
    
    if (!lib) {
        LOG_ERROR("Library not found in registry: %s", library_name);
        return false;
    }
    
    if (lib->is_loaded) {
        return true; // Already loaded
    }

    // Preferred resolution order for relative paths: <exe_dir>/path first, then CWD
    if (!is_absolute_path(lib->path)) {
        char attempt[1024];
        char exe_dir[1024];
        get_executable_dir(exe_dir, sizeof(exe_dir));
        join_paths(attempt, sizeof(attempt), exe_dir, lib->path);
        dl_handle_t h1 = dlopen(attempt, RTLD_LAZY);
        if (h1) {
            lib->handle = h1;
        }
        // If not loaded from exe_dir, try CWD with original relative path
        if (!lib->handle) {
            lib->handle = dlopen(lib->path, RTLD_LAZY);
        }
    } else {
        lib->handle = dlopen(lib->path, RTLD_LAZY);
    }
    if (!lib->handle) {
        const char* err1 = dlerror();
        if (lib->is_optional) {
            LOG_INFO("Optional library not available: %s (%s)", library_name, err1 ? err1 : "unknown");
            return true; // Success for optional libraries
        } else {
            LOG_ERROR("Failed to load required library %s: %s", library_name, err1 ? err1 : "unknown");
            return false;
        }
    }
    
    lib->is_loaded = true;
    LOG_INFO("Successfully loaded library: %s", library_name);
    LOG_INFO("Enumerating functions for library switch: '%s'", library_name);
    
    // Load function symbols based on library type
    if (strcmp(library_name, "webview") == 0) {
        load_webview_functions(lib);
    } else if (strcmp(library_name, "crypto") == 0) {
        load_crypto_functions(lib);
    } else if (strcmp(library_name, "compression") == 0) {
        load_compression_functions(lib);
    } else if (strcmp(library_name, "sqlite") == 0) {
        load_sqlite_functions(lib);
    } else if (strcmp(library_name, "http") == 0) {
        // Enumerate HTTP functions from the loaded handle
        load_http_functions(lib);
    } else if (strcmp(library_name, "math") == 0) {
        load_math_functions(lib);
    } else if (strcmp(library_name, "str") == 0) {
        load_str_functions(lib);
    } else if (strcmp(library_name, "datetime") == 0) {
        load_datetime_functions(lib);
    } else if (strcmp(library_name, "rpc") == 0) {
        load_rpc_functions(lib);
    } else if (strcmp(library_name, "fileio") == 0) {
        load_fileio_functions(lib);
    } else if (strcmp(library_name, "ffmpeg") == 0) {
        load_ffmpeg_functions(lib);
    } else if (strcmp(library_name, "transcoder") == 0) {
        // Alias: the transcoder shared library exposes compression helpers
        // such as compress/decompress and unzip operations.
        // Treat it the same as the "compression" category.
        LOG_INFO("Enumerating functions for 'transcoder' via compression loader");
        load_compression_functions(lib);
    }
    
    return true;
}

void load_webview_functions(SharedLibrary* lib) {
    // Define WebView functions
    const char* webview_functions[][3] = {
        {"webview_init", "webview_init", "int_void"},
        {"webview_create", "webview_create", "ptr_string_ptr"},
        {"webview_create_default_settings", "webview_create_default_settings", "ptr_void"},
        {"webview_load_html", "webview_load_html", "int_ptr_string"},
        {"webview_load_url", "webview_load_url", "int_ptr_string"},
        {"webview_run", "webview_run", "int_ptr"},
        {"webview_show", "webview_show", "void_ptr"},
        {"webview_hide", "webview_hide", "void_ptr"},
        {"webview_destroy", "webview_destroy", "void_ptr"},
        {"webview_cleanup", "webview_cleanup", "void_void"},
        {NULL, NULL, NULL} // Terminator
    };
    
    for (int i = 0; webview_functions[i][0] != NULL; i++) {
        LibraryFunction* func = &lib->functions[lib->function_count];
        strncpy(func->name, webview_functions[i][0], MAX_NAME_LENGTH - 1);
        strncpy(func->symbol_name, webview_functions[i][1], MAX_NAME_LENGTH - 1);
        
        // Set signature based on string
        if (strcmp(webview_functions[i][2], "int_void") == 0) {
            func->signature = FUNC_SIG_INT_VOID;
        } else if (strcmp(webview_functions[i][2], "ptr_string") == 0) {
            func->signature = FUNC_SIG_PTR_STRING;
        } else if (strcmp(webview_functions[i][2], "ptr_string_ptr") == 0) {
            func->signature = FUNC_SIG_PTR_STRING_PTR;
        } else if (strcmp(webview_functions[i][2], "int_ptr_string") == 0) {
            func->signature = FUNC_SIG_INT_PTR_STRING;
        } else if (strcmp(webview_functions[i][2], "int_ptr") == 0) {
            func->signature = FUNC_SIG_INT_PTR;
        } else if (strcmp(webview_functions[i][2], "void_ptr") == 0) {
            func->signature = FUNC_SIG_VOID_PTR;
        } else if (strcmp(webview_functions[i][2], "ptr_void") == 0) {
            func->signature = FUNC_SIG_PTR_VOID;
        } else if (strcmp(webview_functions[i][2], "void_void") == 0) {
            func->signature = FUNC_SIG_VOID_VOID;
        }
        
        // Load the function symbol
        func->function_ptr = dlsym(lib->handle, func->symbol_name);
        if (func->function_ptr) {
            func->is_loaded = true;
            lib->function_count++;
            LOG_INFO("Loaded WebView function: %s", func->name);
        } else {
            LOG_WARNING("Failed to load WebView function: %s (%s)", func->name, dlerror());
        }
    }
}

void load_crypto_functions(SharedLibrary* lib) {
    // Define crypto functions - match actual exported symbols from crypto_utils.c
    const char* crypto_functions[][3] = {
        {"crypto_md5", "crypto_md5", "value_args"},
        {"crypto_sha256", "crypto_sha256", "value_args"},
        {"crypto_sha512", "crypto_sha512", "value_args"},
        {"crypto_blake2b", "crypto_blake2b", "value_args"},
        {"crypto_hmac_sha256", "crypto_hmac_sha256", "value_args"},
        {"crypto_hmac_sha512", "crypto_hmac_sha512", "value_args"},
        {"crypto_pbkdf2_sha256", "crypto_pbkdf2_sha256", "value_args"},
        {"crypto_base64_encode", "crypto_base64_encode", "value_args"},
        {"crypto_base64_decode", "crypto_base64_decode", "value_args"},
        {"crypto_aes_create_context", "crypto_aes_create_context", "value_args"},
        {"crypto_aes_encrypt", "crypto_aes_encrypt", "value_args"},
        {"crypto_aes_decrypt", "crypto_aes_decrypt", "value_args"},
        {"crypto_aes_free_context", "crypto_aes_free_context", "value_args"},
        {"crypto_random_init", "crypto_random_init", "value_args"},
        {"crypto_random_bytes", "crypto_random_bytes", "value_args"},
        {"crypto_random_hex", "crypto_random_hex", "value_args"},
        {"crypto_random_int", "crypto_random_int", "value_args"},
        {"crypto_generate_uuid", "crypto_generate_uuid", "value_args"},
        {"crypto_generate_token", "crypto_generate_token", "value_args"},
        {"crypto_constant_time_compare", "crypto_constant_time_compare", "value_args"},
        {"crypto_get_last_error", "crypto_get_last_error", "value_args"},
        {"crypto_clear_error", "crypto_clear_error", "value_args"},
        {NULL, NULL, NULL}
    };
    
    load_generic_functions(lib, crypto_functions, "Crypto");
}

void load_compression_functions(SharedLibrary* lib) {
    LOG_INFO("Loading compression/transcoder function symbols from shared library");
    const char* compression_functions[][3] = {
        {"compress_gzip", "compress_gzip", "value_args"},
        {"decompress_gzip", "decompress_gzip", "value_args"},
        {"compress_zip", "compress_zip", "value_args"},
        {"decompress_zip", "decompress_zip", "value_args"},
        {"unzip_to_directory", "transcode_unzip_to_directory", "value_args"},
        {NULL, NULL, NULL}
    };
    
    load_generic_functions(lib, compression_functions, "Compression");
}

void load_sqlite_functions(SharedLibrary* lib) {
    const char* sqlite_functions[][3] = {
        {"sqlite_open_database", "kyl_sqlite_open_database", "value_args"},
        {"sqlite_close_database", "kyl_sqlite_close_database", "value_args"},
        {"sqlite_execute_sql", "kyl_sqlite_execute_sql", "value_args"},
        {"sqlite_execute_query", "kyl_sqlite_execute_query", "value_args"},
        {"sqlite_result_first_row", "kyl_sqlite_result_first_row", "value_args"},
        {"sqlite_result_next_row", "kyl_sqlite_result_next_row", "value_args"},
        {"sqlite_row_get_int", "kyl_sqlite_row_get_int", "value_args"},
        {"sqlite_row_get_text", "kyl_sqlite_row_get_text", "value_args"},
        {"sqlite_row_get_real", "kyl_sqlite_row_get_real", "value_args"},
        {"sqlite_free_result", "kyl_sqlite_free_result", "value_args"},
        {"sqlite_get_last_error", "kyl_sqlite_get_last_error", "value_args"},
        {NULL, NULL, NULL}
    };
    
    load_generic_functions(lib, sqlite_functions, "SQLite");
}

void load_rpc_functions(SharedLibrary* lib) {
    // Define RPC functions - these correspond to the C API functions
    const char* rpc_functions[][3] = {
        // Client functions
        {"rpc_create_default_client_config", "rpc_create_default_client_config", "value_args"},
        {"rpc_client_create", "rpc_client_create", "value_args"},
        {"rpc_client_connect", "rpc_client_connect", "value_args"},
        {"rpc_client_disconnect", "rpc_client_disconnect", "value_args"},
        {"rpc_client_call_method", "rpc_client_call_method", "value_args"},
        {"rpc_client_is_connected", "rpc_client_is_connected", "value_args"},
        {"rpc_client_destroy", "rpc_client_destroy", "value_args"},
        
        // Server functions
        {"rpc_create_default_server_config", "rpc_create_default_server_config", "value_args"},
        {"rpc_server_create", "rpc_server_create", "value_args"},
        {"rpc_server_start", "rpc_server_start", "value_args"},
        {"rpc_server_stop", "rpc_server_stop", "value_args"},
        {"rpc_server_register_method", "rpc_server_register_method", "value_args"},
        {"rpc_server_register_service", "rpc_server_register_service", "value_args"},
        {"rpc_server_is_running", "rpc_server_is_running", "value_args"},
        {"rpc_server_destroy", "rpc_server_destroy", "value_args"},
        
        // Code generation functions
        {"rpc_codegen_create", "rpc_codegen_create", "value_args"},
        {"rpc_codegen_parse_thrift", "rpc_codegen_parse_thrift", "value_args"},
        {"rpc_codegen_parse_protobuf", "rpc_codegen_parse_protobuf", "value_args"},
        {"rpc_codegen_generate_code", "rpc_codegen_generate_code", "value_args"},
        {"rpc_codegen_compile_to_so", "rpc_codegen_compile_to_so", "value_args"},
        {"rpc_codegen_destroy", "rpc_codegen_destroy", "value_args"},
        
        // Configuration functions
        {"rpc_client_config_set_endpoint", "rpc_client_config_set_endpoint", "value_args"},
        {"rpc_client_config_set_protocol", "rpc_client_config_set_protocol", "value_args"},
        {"rpc_client_config_set_timeout", "rpc_client_config_set_timeout", "value_args"},
        {"rpc_server_config_set_port", "rpc_server_config_set_port", "value_args"},
        {"rpc_server_config_set_protocol", "rpc_server_config_set_protocol", "value_args"},
        {"rpc_free_config", "rpc_free_config", "value_args"},
        
        // Utility functions
        {"rpc_get_last_error", "rpc_get_last_error", "value_args"},
        {"rpc_set_log_level", "rpc_set_log_level", "value_args"},
        {"rpc_validate_endpoint", "rpc_validate_endpoint", "value_args"},
        {"rpc_get_version", "rpc_get_version", "value_args"},
        
        // Protocol-specific functions
        {"rpc_thrift_init", "rpc_thrift_init", "value_args"},
        {"rpc_thrift_cleanup", "rpc_thrift_cleanup", "value_args"},
        {"rpc_thrift_compile_idl", "rpc_thrift_compile_idl", "value_args"},
        {"rpc_protobuf_init", "rpc_protobuf_init", "value_args"},
        {"rpc_protobuf_cleanup", "rpc_protobuf_cleanup", "value_args"},
        {"rpc_protobuf_compile_proto", "rpc_protobuf_compile_proto", "value_args"},
        
        // Dynamic loading functions
        {"rpc_load_generated_library", "rpc_load_generated_library", "value_args"},
        {"rpc_invoke_method", "rpc_invoke_method", "value_args"},
        {"rpc_unload_library", "rpc_unload_library", "value_args"},
        
        {NULL, NULL, NULL} // Terminator
    };
    
    load_generic_functions(lib, rpc_functions, "RPC");
}

void load_fileio_functions(SharedLibrary* lib) {
    // Define FileIO functions - these correspond to the functions in fileio_utils.c
    const char* fileio_functions[][3] = {
        {"file_read_text", "kuyil_file_read_text", "value_args"},
        {"file_read_csv", "kuyil_file_read_csv", "value_args"},
        {"file_read_json", "kuyil_file_read_json", "value_args"},
        {"file_read_yaml", "kuyil_file_read_yaml", "value_args"},
        {"file_exists", "kuyil_file_exists", "value_args"},
        {"file_size", "kuyil_file_size", "value_args"},
        {"file_validate", "kuyil_file_validate", "value_args"},
        {"file_write_text", "kuyil_file_write_text", "value_args"},
        {NULL, NULL, NULL} // Terminator
    };
    
    load_generic_functions(lib, fileio_functions, "FileIO");
}

void load_ffmpeg_functions(SharedLibrary* lib) {
    // Define FFmpeg functions - these correspond to the functions in libkylffmpeg.c
    const char* ffmpeg_functions[][3] = {
        {"audio_editor_create_kyl", "audio_editor_create_kyl", "value_args"},
        {"audio_editor_destroy_kyl", "audio_editor_destroy_kyl", "value_args"},
        {"audio_load_kyl", "audio_load_kyl", "value_args"},
        {"audio_save_kyl", "audio_save_kyl", "value_args"},
        {"audio_segment_free_kyl", "audio_segment_free_kyl", "value_args"},
        {"audio_get_duration_kyl", "audio_get_duration_kyl", "value_args"},
        {"audio_get_sample_rate_kyl", "audio_get_sample_rate_kyl", "value_args"},
        {"audio_get_channels_kyl", "audio_get_channels_kyl", "value_args"},
        {"audio_trim_kyl", "audio_trim_kyl", "value_args"},
        {"audio_fade_in_kyl", "audio_fade_in_kyl", "value_args"},
        {"audio_fade_out_kyl", "audio_fade_out_kyl", "value_args"},
        {"audio_adjust_volume_kyl", "audio_adjust_volume_kyl", "value_args"},
        {"audio_normalize_kyl", "audio_normalize_kyl", "value_args"},
        {"audio_concat_kyl", "audio_concat_kyl", "value_args"},
        {"audio_merge_kyl", "audio_merge_kyl", "value_args"},
        {"audio_overlay_kyl", "audio_overlay_kyl", "value_args"},
        {"audio_speed_change_kyl", "audio_speed_change_kyl", "value_args"},
        {"audio_reverse_kyl", "audio_reverse_kyl", "value_args"},
        {"ffmpeg_get_last_error_kyl", "ffmpeg_get_last_error_kyl", "value_args"},
        {"ffmpeg_clear_error_kyl", "ffmpeg_clear_error_kyl", "value_args"},
        {NULL, NULL, NULL} // Terminator
    };
    
    load_generic_functions(lib, ffmpeg_functions, "FFmpeg");
}

void load_generic_functions(SharedLibrary* lib, const char* functions[][3], const char* category) {
    for (int i = 0; functions[i][0] != NULL; i++) {
        LibraryFunction* func = &lib->functions[lib->function_count];
        strncpy(func->name, functions[i][0], MAX_NAME_LENGTH - 1);
        strncpy(func->symbol_name, functions[i][1], MAX_NAME_LENGTH - 1);
        func->signature = FUNC_SIG_VALUE_ARGS; // Most library functions use this signature
        
        func->function_ptr = dlsym(lib->handle, func->symbol_name);
        if (func->function_ptr) {
            func->is_loaded = true;
            lib->function_count++;
            LOG_INFO("Loaded %s function: %s", category, func->name);
        } else {
            LOG_WARNING("Failed to load %s function: %s (%s)", category, func->name, dlerror());
        }
    }
}

// HTTP library: load server, client, request/response, and static serving functions
void load_http_functions(SharedLibrary* lib) {
    const char* http_functions[][3] = {
        // Server endpoints
        {"http_server", "kyl_http_server", "value_args"},
        {"http_get", "kyl_http_get", "value_args"},
        {"http_post", "kyl_http_post", "value_args"},
        {"http_put", "kyl_http_put", "value_args"},
        {"http_delete", "kyl_http_delete", "value_args"},
        {"http_listen", "kyl_http_listen", "value_args"},
        {"http_register_route", "kyl_http_register_route", "value_args"},

        // HTTP client
        {"http_client_get", "kyl_http_client_get", "value_args"},
        {"http_client_post", "kyl_http_client_post", "value_args"},

        // Response builders
        {"response_set_status", "kyl_response_set_status", "value_args"},
        {"response_set_body", "kyl_response_set_body", "value_args"},
        {"response_set_json", "kyl_response_set_json", "value_args"},
        {"response_add_header", "kyl_response_add_header", "value_args"},

        // Request accessors
        {"request_get_method", "kyl_request_get_method", "value_args"},
        {"request_get_path", "kyl_request_get_path", "value_args"},
        {"request_get_body", "kyl_request_get_body", "value_args"},
        {"request_get_param", "kyl_request_get_param", "value_args"},
        {"request_get_header", "kyl_request_get_header", "value_args"},
        {"request_parse_multipart", "kyl_request_parse_multipart", "value_args"},
        {"request_get_multipart_field", "kyl_request_get_multipart_field", "value_args"},
        {"request_parse_multipart_fields", "kyl_request_parse_multipart_fields", "value_args"},
        {"request_parse_multipart_file", "kyl_request_parse_multipart_file", "value_args"},
        {"request_save_multipart_file", "kyl_request_save_multipart_file", "value_args"},
        {"request_get_json_string", "kyl_request_get_json_string", "value_args"},
        {"request_get_json_number", "kyl_request_get_json_number", "value_args"},
        {"request_get_json_bool", "kyl_request_get_json_bool", "value_args"},

        // Static server helpers and cleanup
        {"http_static", "kyl_http_static", "value_args"},
        {"http_static_add", "kyl_http_static_add", "value_args"},
        {"http_static_bypass", "kyl_http_static_bypass", "value_args"},
        {"http_static_bypass_clear", "kyl_http_static_bypass_clear", "value_args"},
        {"http_cleanup", "kyl_http_cleanup", "value_args"},

        {NULL, NULL, NULL}
    };

    load_generic_functions(lib, http_functions, "HTTP");
    // Inject Kuyil caller bridge if provided by VM
    if (g_kuyil_caller_cb && lib->handle) {
        void (*http_set_kuyil_caller)(void*) = dlsym(lib->handle, "http_set_kuyil_caller");
        if (http_set_kuyil_caller) {
            LOG_INFO("Injected Kuyil caller into HTTP library (loader)");
            http_set_kuyil_caller(g_kuyil_caller_cb);
        }
    }
}

// Math library: basic arithmetic and trigonometric functions
void load_math_functions(SharedLibrary* lib) {
    const char* math_functions[][3] = {
        {"math_abs", "kyl_math_abs", "value_args"},
        {"math_floor", "kyl_math_floor", "value_args"},
        {"math_ceil", "kyl_math_ceil", "value_args"},
        {"math_round", "kyl_math_round", "value_args"},
        {"math_sqrt", "kyl_math_sqrt", "value_args"},
        {"math_pow", "kyl_math_pow", "value_args"},
        {"math_sin", "kyl_math_sin", "value_args"},
        {"math_cos", "kyl_math_cos", "value_args"},
        {"math_tan", "kyl_math_tan", "value_args"},
        {NULL, NULL, NULL}
    };

    load_generic_functions(lib, math_functions, "Math");
}

// String library: string manipulation functions
void load_str_functions(SharedLibrary* lib) {
    const char* str_functions[][3] = {
        {"str_length", "kyl_str_length", "value_args"},
        {"str_substring", "kyl_str_substring", "value_args"},
        {"str_upper", "kyl_str_upper", "value_args"},
        {"str_lower", "kyl_str_lower", "value_args"},
        {"str_trim", "kyl_str_trim", "value_args"},
        {"str_contains", "kyl_str_contains", "value_args"},
        {"str_replace", "kyl_str_replace", "value_args"},
        {"split", "kyl_str_split", "value_args"},
        {"to_number", "kyl_str_to_number", "value_args"},
        {"to_string", "kyl_str_to_string", "value_args"},
        {NULL, NULL, NULL}
    };

    load_generic_functions(lib, str_functions, "String");
}

// DateTime library: date and time manipulation
void load_datetime_functions(SharedLibrary* lib) {
    const char* datetime_functions[][3] = {
        {"date_now", "kyl_date_now", "value_args"},
        {"date_current", "kyl_date_current", "value_args"},
        {"datetime_now", "kyl_datetime_now", "value_args"},
        {"datetime_current", "kyl_datetime_current", "value_args"},
        {"date_add", "kyl_date_add", "value_args"},
        {"date_sub", "kyl_date_sub", "value_args"},
        {"date_diff", "kyl_date_diff", "value_args"},
        {"date_unix", "kyl_date_unix", "value_args"},
        {"date_from_unix", "kyl_date_from_unix", "value_args"},
        {"date_iso", "kyl_date_iso", "value_args"},
        {"date_format", "kyl_date_format", "value_args"},
        {NULL, NULL, NULL}
    };

    load_generic_functions(lib, datetime_functions, "DateTime");
}

bool load_all_libraries(void) {
    bool all_success = true;
    
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (!load_library(lib->name)) {
            if (!lib->is_optional) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

void* get_library_function(const char* library_name, const char* function_name) {
    SharedLibrary* lib = NULL;
    
    // Find library
    for (int i = 0; i < g_library_registry.library_count; i++) {
        if (strcmp(g_library_registry.libraries[i].name, library_name) == 0) {
            lib = &g_library_registry.libraries[i];
            break;
        }
    }
    
    if (!lib || !lib->is_loaded) {
        return NULL;
    }
    
    // Find function
    for (int i = 0; i < lib->function_count; i++) {
        if (strcmp(lib->functions[i].name, function_name) == 0 && lib->functions[i].is_loaded) {
            return lib->functions[i].function_ptr;
        }
    }
    
    return NULL;
}

bool register_library_functions(VM* vm) {
    int total_functions = 0;
    
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (!lib->is_loaded) continue;
        
        for (int j = 0; j < lib->function_count; j++) {
            LibraryFunction* func = &lib->functions[j];
            if (!func->is_loaded) continue;
            
            // Register function with VM based on signature
            // This would integrate with your existing VM function registration system
            total_functions++;
            LOG_INFO("Registered function: %s from library %s", func->name, lib->name);
        }
    }
    
    LOG_INFO("Registered %d functions from %d libraries", total_functions, g_library_registry.library_count);
    return true;
}

void unload_all_libraries(void) {
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (lib->is_loaded && lib->handle) {
            dlclose(lib->handle);
            lib->handle = NULL;
            lib->is_loaded = false;
            LOG_INFO("Unloaded library: %s", lib->name);
        }
    }
}

void library_loader_cleanup(void) {
    unload_all_libraries();
    memset(&g_library_registry, 0, sizeof(LibraryRegistry));
    LOG_INFO("Library loader cleanup completed");
}

// Native function wrappers for different signatures
Value native_library_call_void_void(int arg_count, Value* args, void* func_ptr) {
    void (*func)(void) = (void (*)(void))func_ptr;
    func();
    Value result = {VALUE_NIL};
    return result;
}

Value native_library_call_int_void(int arg_count, Value* args, void* func_ptr) {
    int (*func)(void) = (int (*)(void))func_ptr;
    int result = func();
    Value val;
    val.type = VALUE_NUMBER;
    val.as.number = (double)result;
    return val;
}

Value native_library_call_ptr_string(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    void* (*func)(const char*) = (void* (*)(const char*))func_ptr;
    void* ptr = func(args[0].as.string);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)(intptr_t)ptr;
    return result;
}

Value native_library_call_int_ptr_string(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int (*func)(void*, const char*) = (int (*)(void*, const char*))func_ptr;
    void* ptr = (void*)(intptr_t)args[0].as.number;
    int result = func(ptr, args[1].as.string);
    
    Value val;
    val.type = VALUE_NUMBER;
    val.as.number = (double)result;
    return val;
}

Value native_library_call_void_ptr(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    void (*func)(void*) = (void (*)(void*))func_ptr;
    void* ptr = (void*)(intptr_t)args[0].as.number;
    func(ptr);
    
    Value result = {VALUE_NIL};
    return result;
}

Value native_library_call_ptr_void(int arg_count, Value* args, void* func_ptr) {
    (void)arg_count; (void)args; // Suppress unused parameter warnings
    
    void* (*func)(void) = (void* (*)(void))func_ptr;
    void* ptr = func();
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)(intptr_t)ptr;
    return result;
}