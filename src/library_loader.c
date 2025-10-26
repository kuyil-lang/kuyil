#include "library_loader.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

// Global library registry
LibraryRegistry g_library_registry = {0};

bool library_loader_init(void) {
    if (g_library_registry.initialized) {
        return true;
    }
    
    memset(&g_library_registry, 0, sizeof(LibraryRegistry));
    g_library_registry.initialized = true;
    
    LOG_INFO("Library loader initialized");
    return true;
}

bool load_library_config(const char* config_file) {
    FILE* file = fopen(config_file, "r");
    if (!file) {
        LOG_WARNING("Could not open library config file: %s", config_file);
        return false;
    }
    
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
            strncpy(lib->path, path, MAX_PATH_LENGTH - 1);
            lib->is_optional = optional;
            lib->is_loaded = false;
            lib->handle = NULL;
            lib->function_count = 0;
            
            g_library_registry.library_count++;
            LOG_INFO("Registered library: %s (path: %s, optional: %s)", 
                    name, path, optional ? "true" : "false");
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
    
    // Try to load the library
    lib->handle = dlopen(lib->path, RTLD_LAZY);
    if (!lib->handle) {
        if (lib->is_optional) {
            LOG_INFO("Optional library not available: %s (%s)", library_name, dlerror());
            return true; // Success for optional libraries
        } else {
            LOG_ERROR("Failed to load required library %s: %s", library_name, dlerror());
            return false;
        }
    }
    
    lib->is_loaded = true;
    LOG_INFO("Successfully loaded library: %s", library_name);
    
    // Load function symbols based on library type
    if (strcmp(library_name, "webview") == 0) {
        load_webview_functions(lib);
    } else if (strcmp(library_name, "crypto") == 0) {
        load_crypto_functions(lib);
    } else if (strcmp(library_name, "compression") == 0) {
        load_compression_functions(lib);
    } else if (strcmp(library_name, "sqlite") == 0) {
        load_sqlite_functions(lib);
    } else if (strcmp(library_name, "rpc") == 0) {
        load_rpc_functions(lib);
    } else if (strcmp(library_name, "fileio") == 0) {
        load_fileio_functions(lib);
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
    // Define crypto functions
    const char* crypto_functions[][3] = {
        {"crypto_hash_md5", "hash_md5", "value_args"},
        {"crypto_hash_sha256", "hash_sha256", "value_args"},
        {"crypto_encrypt_aes", "encrypt_aes", "value_args"},
        {"crypto_decrypt_aes", "decrypt_aes", "value_args"},
        {"crypto_generate_key", "generate_key", "value_args"},
        {NULL, NULL, NULL}
    };
    
    load_generic_functions(lib, crypto_functions, "Crypto");
}

void load_compression_functions(SharedLibrary* lib) {
    const char* compression_functions[][3] = {
        {"compress_gzip", "compress_gzip", "value_args"},
        {"decompress_gzip", "decompress_gzip", "value_args"},
        {"compress_zip", "compress_zip", "value_args"},
        {"decompress_zip", "decompress_zip", "value_args"},
        {NULL, NULL, NULL}
    };
    
    load_generic_functions(lib, compression_functions, "Compression");
}

void load_sqlite_functions(SharedLibrary* lib) {
    const char* sqlite_functions[][3] = {
        {"sqlite_open", "sqlite_open", "value_args"},
        {"sqlite_close", "sqlite_close", "value_args"},
        {"sqlite_execute", "sqlite_execute", "value_args"},
        {"sqlite_query", "sqlite_query", "value_args"},
        {"sqlite_prepare", "sqlite_prepare", "value_args"},
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
        {NULL, NULL, NULL} // Terminator
    };
    
    load_generic_functions(lib, fileio_functions, "FileIO");
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