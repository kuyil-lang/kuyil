#define _POSIX_C_SOURCE 200809L
#include "library_loader.h"
#include "vm_library_integration.h"
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
Value vm_bind_interface_method(int arg_count, Value* args);  // From vm_library_integration.c

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
    
    // New design: discover signatures exported by the library
    // Look for kyl_interface_signature_text symbol
    const char** sig_ptr = (const char**)dlsym(lib->handle, "kyl_interface_signature_text");
    if (sig_ptr && *sig_ptr) {
        const char* sig_text = *sig_ptr;
        LOG_INFO("Found interface signatures in library %s, parsing...", library_name);
        
        // Parse signature text format: "interface_name method_name(param:type,...) -> return_type\n..."
        // Manual parsing to avoid strtok issues
        const char* p = sig_text;
        char line_buf[1024];
        
        while (*p) {
            // Extract one line
            int line_len = 0;
            while (*p && *p != '\n' && line_len < sizeof(line_buf) - 1) {
                line_buf[line_len++] = *p++;
            }
            line_buf[line_len] = '\0';
            if (*p == '\n') p++; // skip newline
            
            // Skip empty lines and comments
            const char* line = line_buf;
            while (*line == ' ' || *line == '\t') line++;
            if (*line == '\0' || *line == '#') continue;
            
            // Parse: interface_name method_name(params) -> return_type
            char iface_name[128] = {0};
            char method_name[128] = {0};
            
            // Extract interface name
            int i = 0;
            while (*line && *line != ' ' && *line != '\t' && i < 127) {
                iface_name[i++] = *line++;
            }
            iface_name[i] = '\0';
            
            // Skip whitespace
            while (*line == ' ' || *line == '\t') line++;
            
            // Extract method name (stop at '(')
            i = 0;
            while (*line && *line != '(' && *line != ' ' && *line != '\t' && i < 127) {
                method_name[i++] = *line++;
            }
            method_name[i] = '\0';
            
            // Parse parameters if present: openDatabase(path: string, flags: int32)
            // Skip to '(' to find params
            while (*line && *line != '(') line++;
            
            // Initialize param array - collect params in a temporary buffer
            char* param_list[32] = {0};  // Max 32 params
            int param_count = 0;
            
            char return_type_str[128] = {0};
            
            if (*line == '(') {
                line++; // skip '('
                // Parse params until ')'
                while (*line && *line != ')' && param_count < 32) {
                    while (*line == ' ' || *line == '\t' || *line == ',') line++;
                    if (*line == ')') break;
                    
                    // Extract one param: "name: type"
                    char param_spec[256] = {0};
                    int pi = 0;
                    int paren_depth = 0;
                    while (*line && pi < 255) {
                        if (*line == '(') paren_depth++;
                        if (*line == ')') {
                            if (paren_depth == 0) break;
                            paren_depth--;
                        }
                        if (*line == ',' && paren_depth == 0) break;
                        param_spec[pi++] = *line++;
                    }
                    param_spec[pi] = '\0';
                    
                    // Trim trailing spaces
                    while (pi > 0 && (param_spec[pi-1] == ' ' || param_spec[pi-1] == '\t')) {
                        param_spec[--pi] = '\0';
                    }
                    
                    if (pi > 0) {
                        param_list[param_count++] = strdup(param_spec);
                    }
                }
                
                // Skip ')'
                if (*line == ')') line++;
                
                // Look for return type: " -> type"
                while (*line == ' ' || *line == '\t') line++;
                if (*line == '-' && *(line+1) == '>') {
                    line += 2;
                    while (*line == ' ' || *line == '\t') line++;
                    // Extract return type
                    int ri = 0;
                    while (*line && *line != '\n' && ri < 127) {
                        return_type_str[ri++] = *line++;
                    }
                    return_type_str[ri] = '\0';
                    // Trim trailing spaces
                    while (ri > 0 && (return_type_str[ri-1] == ' ' || return_type_str[ri-1] == '\t')) {
                        return_type_str[--ri] = '\0';
                    }
                }
            }
            
            if (iface_name[0] && method_name[0]) {
                LOG_INFO("Auto-binding %s.%s from signature (params=%d)", 
                         iface_name, method_name, param_count);
                
                // Build Value array for params
                Value param_array;
                memset(&param_array, 0, sizeof(Value));
                param_array.type = VALUE_ARRAY;
                param_array.as.array.count = param_count;
                if (param_count > 0) {
                    param_array.as.array.values = malloc(sizeof(Value) * param_count);
                    for (int pi = 0; pi < param_count; pi++) {
                        param_array.as.array.values[pi].type = VALUE_STRING;
                        param_array.as.array.values[pi].as.string = param_list[pi];
                    }
                } else {
                    param_array.as.array.values = NULL;
                }
                
                // Prepare args: [interface, method, params_array, return_type, aliases(null), is_exported(true)]
                Value args[6];
                args[0].type = VALUE_STRING;
                args[0].as.string = strdup(iface_name);
                args[1].type = VALUE_STRING;
                args[1].as.string = strdup(method_name);
                args[2] = param_array;
                args[3].type = VALUE_STRING;
                args[3].as.string = return_type_str[0] ? strdup(return_type_str) : strdup("");
                args[4].type = VALUE_NIL;  // No extra aliases
                args[5].type = VALUE_BOOL;
                args[5].as.boolean = true;  // Treat signature-based bindings as exported (namespace required)
                
                fprintf(stderr, "[LOADER] Auto-binding %s.%s with is_exported=TRUE\n", iface_name, method_name);
                fflush(stderr);
                
                // Call the binding function with is_exported=true
                Value result = vm_bind_interface_method(6, args);
                
                // Free the allocated strings
                free(args[0].as.string);
                free(args[1].as.string);
                free(args[3].as.string);
                
                // Free param array
                if (param_array.as.array.values) {
                    for (int pi = 0; pi < param_count; pi++) {
                        free(param_array.as.array.values[pi].as.string);
                    }
                    free(param_array.as.array.values);
                }
                
                if (result.type == VALUE_BOOL && result.as.boolean) {
                    LOG_DEBUG("Successfully bound %s.%s", iface_name, method_name);
                } else {
                    LOG_ERROR("Failed to bind %s.%s - C function not found in library", iface_name, method_name);
                    fprintf(stderr, "\nERROR: Interface binding failed for %s.%s\n", iface_name, method_name);
                    fprintf(stderr, "  Library: %s\n", library_name);
                    fprintf(stderr, "  The C function for this method was not found.\n");
                    fprintf(stderr, "  Check that the library exports the correct function name.\n\n");
                    return false;  // Fail the library load
                }
            }
        }
    } else {
        LOG_DEBUG("No kyl_interface_signature_text found in %s (functions discovered at call time)", library_name);
    }
    
    return true;
}

void load_webview_functions(SharedLibrary* lib) {
    // Define WebView functions - using value_args for new functions that take Value* args
    const char* webview_functions[][3] = {
        {"webview_init", "kyl_webview_init", "value_args"},
        {"webview_create", "kyl_webview_create", "value_args"},
        {"webview_create_default_settings", "kyl_webview_create_default_settings", "value_args"},
        {"webview_load_html", "kyl_webview_load_html", "value_args"},
        {"webview_load_url", "kyl_webview_load_url", "value_args"},
        {"webview_run", "kyl_webview_run", "value_args"},
        {"webview_show", "kyl_webview_show", "value_args"},
        {"webview_hide", "kyl_webview_hide", "value_args"},
        {"webview_destroy", "kyl_webview_destroy", "value_args"},
        {"webview_cleanup", "kyl_webview_cleanup", "value_args"},
        {"webview_eval", "kyl_webview_eval", "value_args"},
        {"webview_set_title", "kyl_webview_set_title", "value_args"},
        {"webview_set_size", "kyl_webview_set_size", "value_args"},
        {"webview_bind", "kyl_webview_bind", "value_args"},
        {"webview_step", "kyl_webview_step", "value_args"},
        {NULL, NULL, NULL} // Terminator
    };
    
    for (int i = 0; webview_functions[i][0] != NULL; i++) {
        if (lib->function_count >= MAX_FUNCTIONS_PER_LIBRARY) {
            LOG_WARNING("Max functions reached for '%s' while loading WebView; stopping", lib->name);
            break;
        }
        LibraryFunction* func = &lib->functions[lib->function_count];
        memset(func, 0, sizeof(*func));
        strncpy(func->name, webview_functions[i][0], MAX_NAME_LENGTH - 1);
        func->name[MAX_NAME_LENGTH - 1] = '\0';
        strncpy(func->symbol_name, webview_functions[i][1], MAX_NAME_LENGTH - 1);
        func->symbol_name[MAX_NAME_LENGTH - 1] = '\0';
        
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
        } else if (strcmp(webview_functions[i][2], "value_args") == 0) {
            func->signature = FUNC_SIG_VALUE_ARGS;
        }
        
        // Load the function symbol
        func->function_ptr = dlsym(lib->handle, func->symbol_name);
        if (func->function_ptr) {
            func->is_loaded = true;
            lib->function_count++;
            LOG_INFO("Loaded WebView function: %s", func->name);
        } else {
            const char* err = dlerror();
            LOG_WARNING("Failed to load WebView function: %s (%s)", func->name, err ? err : "unknown");
        }
    }
}

void load_system_functions(SharedLibrary* lib) {
    // Define System functions - using value_args signature
    const char* system_functions[][3] = {
        {"system_sleep", "kyl_system_sleep", "value_args"},
        {"system_timestamp", "kyl_system_timestamp", "value_args"},
        {NULL, NULL, NULL} // Terminator
    };
    
    for (int i = 0; system_functions[i][0] != NULL; i++) {
        if (lib->function_count >= MAX_FUNCTIONS_PER_LIBRARY) {
            LOG_WARNING("Max functions reached for '%s' while loading System; stopping", lib->name);
            break;
        }
        LibraryFunction* func = &lib->functions[lib->function_count];
        memset(func, 0, sizeof(*func));
        strncpy(func->name, system_functions[i][0], MAX_NAME_LENGTH - 1);
        func->name[MAX_NAME_LENGTH - 1] = '\0';
        strncpy(func->symbol_name, system_functions[i][1], MAX_NAME_LENGTH - 1);
        func->symbol_name[MAX_NAME_LENGTH - 1] = '\0';
        
        // Set signature
        if (strcmp(system_functions[i][2], "value_args") == 0) {
            func->signature = FUNC_SIG_VALUE_ARGS;
        }
        
        // Load the function symbol
        func->function_ptr = dlsym(lib->handle, func->symbol_name);
        if (func->function_ptr) {
            func->is_loaded = true;
            lib->function_count++;
            LOG_INFO("Loaded System function: %s", func->name);
        } else {
            const char* err = dlerror();
            LOG_WARNING("Failed to load System function: %s (%s)", func->name, err ? err : "unknown");
        }
    }
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
    (void)vm;  // Unused parameter
    // Register functions from libraries.conf pre-loaded libraries
    // These get added to g_dynamic_functions[], which the interface system
    // uses via bind_interface_method() to create convenient aliases.
    int total_functions = 0;
    
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (!lib->is_loaded) continue;
        
        for (int j = 0; j < lib->function_count; j++) {
            LibraryFunction* func = &lib->functions[j];
            if (!func->is_loaded) continue;
            
            // Register function with VM's dynamic function system
            vm_register_dynamic_function(func->name, func->function_ptr, func->signature);
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
    (void)args;  // Unused parameter
    (void)arg_count;  // Unused parameter
    void (*func)(void) = (void (*)(void))func_ptr;
    func();
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

Value native_library_call_int_void(int arg_count, Value* args, void* func_ptr) {
    (void)args;  // Unused parameter
    (void)arg_count;  // Unused parameter
    int (*func)(void) = (int (*)(void))func_ptr;
    int result = func();
    Value val;
    val.type = VALUE_NUMBER;
    val.as.number = (double)result;
    return val;
}

Value native_library_call_ptr_string(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
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
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
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
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    void (*func)(void*) = (void (*)(void*))func_ptr;
    void* ptr = (void*)(intptr_t)args[0].as.number;
    func(ptr);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
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