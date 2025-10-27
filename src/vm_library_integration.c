#define _POSIX_C_SOURCE 200809L
#include "vm_library_integration.h"
#include "library_loader.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
// dlfcn.h is already handled in library_loader.h with Windows compatibility

// Dynamic function registration system
typedef struct {
    const char* name;
    Value (*handler)(int, Value*);
    void* library_func_ptr;
    FunctionSignature signature;
} DynamicFunction;

static DynamicFunction* g_dynamic_functions = NULL;
static int g_dynamic_function_count = 0;
static int g_dynamic_function_capacity = 0;

// Forward declarations
static void add_default_libraries(void);
static bool register_dynamic_functions(VM* vm);
static void register_system_functions(VM* vm);
static void register_default_library_functions(void);
static void register_dynamic_function(const char* name, void* func_ptr, FunctionSignature signature);
static Value wrapper_void_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_ptr_string(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_ptr_string(int arg_count, Value* args, void* func_ptr);
static Value wrapper_void_ptr(int arg_count, Value* args, void* func_ptr);
static Value wrapper_ptr_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_ptr(int arg_count, Value* args, void* func_ptr);

bool vm_init_library_system(VM* vm) {
    LOG_INFO("Initializing VM library system");
    
    // Initialize library loader
    if (!library_loader_init()) {
        LOG_ERROR("Failed to initialize library loader");
        return false;
    }
    
    // Load library configuration
    const char* config_files[] = {
        "./libraries.conf",
        "libraries.conf",
        NULL
    };
    
    bool config_loaded = false;
    for (int i = 0; config_files[i] != NULL; i++) {
        if (load_library_config(config_files[i])) {
            LOG_INFO("Loaded library config from: %s", config_files[i]);
            config_loaded = true;
            break;
        }
    }
    
    if (!config_loaded) {
        LOG_WARNING("No library configuration file found, using default libraries");
        // Load default libraries programmatically
        add_default_libraries();
    }
    
    // Load all configured libraries
    if (!load_all_libraries()) {
        LOG_WARNING("Some libraries failed to load, continuing with available libraries");
    }
    
    // Register library functions with VM
    if (!register_dynamic_functions(vm)) {
        LOG_ERROR("Failed to register library functions with VM");
        return false;
    }
    
    LOG_INFO("VM library system initialized successfully");
    return true;
}

static void add_default_libraries(void) {
    // Add WebView library
    if (g_library_registry.library_count < MAX_LIBRARIES) {
        SharedLibrary* lib = &g_library_registry.libraries[g_library_registry.library_count];
        strncpy(lib->name, "webview", MAX_NAME_LENGTH - 1);
        strncpy(lib->path, "./shared_libs/webview/libwebview_utils.so", MAX_PATH_LENGTH - 1);
        lib->is_optional = true;
        lib->is_loaded = false;
        lib->handle = NULL;
        lib->function_count = 0;
        g_library_registry.library_count++;
    }
    
    // Add other default libraries as needed
    LOG_INFO("Added default library configurations");
}

static bool register_dynamic_functions(VM* vm) {
    // Allocate dynamic function array
    g_dynamic_function_capacity = 256;
    g_dynamic_functions = malloc(sizeof(DynamicFunction) * g_dynamic_function_capacity);
    if (!g_dynamic_functions) {
        LOG_ERROR("Failed to allocate dynamic function array");
        return false;
    }
    
    // Register functions from all loaded libraries
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (!lib->is_loaded) continue;
        
        for (int j = 0; j < lib->function_count; j++) {
            LibraryFunction* func = &lib->functions[j];
            if (!func->is_loaded) continue;
            
            register_dynamic_function(func->name, func->function_ptr, func->signature);
            
            // Dynamic functions are called through the dispatch system, not globals
            // No need to register in VM's global namespace
        }
    }
    
    // Register built-in library system functions
    register_system_functions(vm);
    
    LOG_INFO("Registered %d dynamic functions", g_dynamic_function_count);
    return true;
}

static void register_system_functions(VM* vm) {
    // Register library introspection functions
    register_dynamic_function("get_library_count", vm_get_library_count, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("get_library_name", vm_get_library_name, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("get_library_path", vm_get_library_path, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("is_library_loaded", vm_is_library_loaded, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("get_library_function_count", vm_get_library_function_count, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("get_loaded_libraries_info", vm_get_loaded_libraries_info, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("get_total_function_count", vm_get_total_function_count, FUNC_SIG_VALUE_ARGS);
    
    // Register inline library configuration functions
    register_dynamic_function("add_library", vm_add_library, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("load_library", vm_load_library_inline, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("clear_libraries", vm_clear_libraries, FUNC_SIG_VALUE_ARGS);
    
    // Register module import functions
    register_dynamic_function("import", vm_import_module, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("export_function", vm_export_function, FUNC_SIG_VALUE_ARGS);
    
    // Register default core library functions
    register_default_library_functions();
    
    // Dynamic functions are handled by the dispatch system, not as globals
    LOG_INFO("Registered 12 library system functions");
}

static void register_default_library_functions(void) {
    LOG_INFO("Registering default library functions using direct dlopen");
    
    // Math library functions - load directly with dlopen
    void* math_lib = dlopen("./libs/libkylmath.so", RTLD_LAZY);
    if (math_lib) {
        void* abs_fn = dlsym(math_lib, "kyl_math_abs");
        void* floor_fn = dlsym(math_lib, "kyl_math_floor");
        void* ceil_fn = dlsym(math_lib, "kyl_math_ceil");
        void* round_fn = dlsym(math_lib, "kyl_math_round");
        void* sqrt_fn = dlsym(math_lib, "kyl_math_sqrt");
        void* pow_fn = dlsym(math_lib, "kyl_math_pow");
        void* sin_fn = dlsym(math_lib, "kyl_math_sin");
        void* cos_fn = dlsym(math_lib, "kyl_math_cos");
        void* tan_fn = dlsym(math_lib, "kyl_math_tan");
        
        if (abs_fn) register_dynamic_function("math_abs", abs_fn, FUNC_SIG_VALUE_ARGS);
        if (floor_fn) register_dynamic_function("math_floor", floor_fn, FUNC_SIG_VALUE_ARGS);
        if (ceil_fn) register_dynamic_function("math_ceil", ceil_fn, FUNC_SIG_VALUE_ARGS);
        if (round_fn) register_dynamic_function("math_round", round_fn, FUNC_SIG_VALUE_ARGS);
        if (sqrt_fn) register_dynamic_function("math_sqrt", sqrt_fn, FUNC_SIG_VALUE_ARGS);
        if (pow_fn) register_dynamic_function("math_pow", pow_fn, FUNC_SIG_VALUE_ARGS);
        if (sin_fn) register_dynamic_function("math_sin", sin_fn, FUNC_SIG_VALUE_ARGS);
        if (cos_fn) register_dynamic_function("math_cos", cos_fn, FUNC_SIG_VALUE_ARGS);
        if (tan_fn) register_dynamic_function("math_tan", tan_fn, FUNC_SIG_VALUE_ARGS);
        LOG_INFO("Loaded math library functions");
    } else {
        LOG_ERROR("Failed to load math library: %s", dlerror());
    }
    
    // String library functions - load directly with dlopen
    void* str_lib = dlopen("./libs/libkylstr.so", RTLD_LAZY);
    if (str_lib) {
        void* length_fn = dlsym(str_lib, "kyl_str_length");
        void* substring_fn = dlsym(str_lib, "kyl_str_substring");
        void* upper_fn = dlsym(str_lib, "kyl_str_upper");
        void* lower_fn = dlsym(str_lib, "kyl_str_lower");
        void* trim_fn = dlsym(str_lib, "kyl_str_trim");
        void* contains_fn = dlsym(str_lib, "kyl_str_contains");
        void* replace_fn = dlsym(str_lib, "kyl_str_replace");
        void* to_number_fn = dlsym(str_lib, "kyl_str_to_number");
        void* to_string_fn = dlsym(str_lib, "kyl_str_to_string");
        
        if (length_fn) register_dynamic_function("str_length", length_fn, FUNC_SIG_VALUE_ARGS);
        if (substring_fn) register_dynamic_function("str_substring", substring_fn, FUNC_SIG_VALUE_ARGS);
        if (upper_fn) register_dynamic_function("str_upper", upper_fn, FUNC_SIG_VALUE_ARGS);
        if (lower_fn) register_dynamic_function("str_lower", lower_fn, FUNC_SIG_VALUE_ARGS);
        if (trim_fn) register_dynamic_function("str_trim", trim_fn, FUNC_SIG_VALUE_ARGS);
        if (contains_fn) register_dynamic_function("str_contains", contains_fn, FUNC_SIG_VALUE_ARGS);
        if (replace_fn) register_dynamic_function("str_replace", replace_fn, FUNC_SIG_VALUE_ARGS);
        if (to_number_fn) register_dynamic_function("to_number", to_number_fn, FUNC_SIG_VALUE_ARGS);
        if (to_string_fn) register_dynamic_function("to_string", to_string_fn, FUNC_SIG_VALUE_ARGS);
        LOG_INFO("Loaded string library functions");
    } else {
        LOG_ERROR("Failed to load string library: %s", dlerror());
    }
    
    // HTTP library functions - load directly with dlopen
    void* http_lib = dlopen("./libs/libkylhttp.so", RTLD_LAZY);
    if (http_lib) {
        // Server functions
        void* server_fn = dlsym(http_lib, "kyl_http_server");
        void* get_fn = dlsym(http_lib, "kyl_http_get");
        void* post_fn = dlsym(http_lib, "kyl_http_post");
        void* put_fn = dlsym(http_lib, "kyl_http_put");
        void* delete_fn = dlsym(http_lib, "kyl_http_delete");
        void* listen_fn = dlsym(http_lib, "kyl_http_listen");
        
        // Client functions
        void* client_get_fn = dlsym(http_lib, "kyl_http_client_get");
        void* client_post_fn = dlsym(http_lib, "kyl_http_client_post");
        
        // Static serving
        void* static_fn = dlsym(http_lib, "kyl_http_static");
        void* cleanup_fn = dlsym(http_lib, "kyl_http_cleanup");
        
        // Register server functions
        if (server_fn) register_dynamic_function("http_server", server_fn, FUNC_SIG_VALUE_ARGS);
        if (get_fn) register_dynamic_function("http_get", get_fn, FUNC_SIG_VALUE_ARGS);
        if (post_fn) register_dynamic_function("http_post", post_fn, FUNC_SIG_VALUE_ARGS);
        if (put_fn) register_dynamic_function("http_put", put_fn, FUNC_SIG_VALUE_ARGS);
        if (delete_fn) register_dynamic_function("http_delete", delete_fn, FUNC_SIG_VALUE_ARGS);
        if (listen_fn) register_dynamic_function("http_listen", listen_fn, FUNC_SIG_VALUE_ARGS);
        
        // Register client functions
        if (client_get_fn) register_dynamic_function("http_client_get", client_get_fn, FUNC_SIG_VALUE_ARGS);
        if (client_post_fn) register_dynamic_function("http_client_post", client_post_fn, FUNC_SIG_VALUE_ARGS);
        
        // Register utility functions
        if (static_fn) register_dynamic_function("http_static", static_fn, FUNC_SIG_VALUE_ARGS);
        if (cleanup_fn) register_dynamic_function("http_cleanup", cleanup_fn, FUNC_SIG_VALUE_ARGS);
        
        LOG_INFO("Loaded HTTP library functions");
    } else {
        LOG_ERROR("Failed to load HTTP library: %s", dlerror());
    }
    
    // DateTime library functions - load directly with dlopen
    void* datetime_lib = dlopen("./libs/libkyldatetime.so", RTLD_LAZY);
    if (datetime_lib) {
        void* date_now_fn = dlsym(datetime_lib, "kyl_date_now");
        void* date_current_fn = dlsym(datetime_lib, "kyl_date_current");
        void* datetime_now_fn = dlsym(datetime_lib, "kyl_datetime_now");
        void* datetime_current_fn = dlsym(datetime_lib, "kyl_datetime_current");
        void* date_add_fn = dlsym(datetime_lib, "kyl_date_add");
        void* date_sub_fn = dlsym(datetime_lib, "kyl_date_sub");
        void* date_diff_fn = dlsym(datetime_lib, "kyl_date_diff");
        void* date_unix_fn = dlsym(datetime_lib, "kyl_date_unix");
        void* date_from_unix_fn = dlsym(datetime_lib, "kyl_date_from_unix");
        void* date_iso_fn = dlsym(datetime_lib, "kyl_date_iso");
        void* date_format_fn = dlsym(datetime_lib, "kyl_date_format");
        
        if (date_now_fn) register_dynamic_function("date_now", date_now_fn, FUNC_SIG_VALUE_ARGS);
        if (date_current_fn) register_dynamic_function("date_current", date_current_fn, FUNC_SIG_VALUE_ARGS);
        if (datetime_now_fn) register_dynamic_function("datetime_now", datetime_now_fn, FUNC_SIG_VALUE_ARGS);
        if (datetime_current_fn) register_dynamic_function("datetime_current", datetime_current_fn, FUNC_SIG_VALUE_ARGS);
        if (date_add_fn) register_dynamic_function("date_add", date_add_fn, FUNC_SIG_VALUE_ARGS);
        if (date_sub_fn) register_dynamic_function("date_sub", date_sub_fn, FUNC_SIG_VALUE_ARGS);
        if (date_diff_fn) register_dynamic_function("date_diff", date_diff_fn, FUNC_SIG_VALUE_ARGS);
        if (date_unix_fn) register_dynamic_function("date_unix", date_unix_fn, FUNC_SIG_VALUE_ARGS);
        if (date_from_unix_fn) register_dynamic_function("date_from_unix", date_from_unix_fn, FUNC_SIG_VALUE_ARGS);
        if (date_iso_fn) register_dynamic_function("date_iso", date_iso_fn, FUNC_SIG_VALUE_ARGS);
        if (date_format_fn) register_dynamic_function("date_format", date_format_fn, FUNC_SIG_VALUE_ARGS);
        LOG_INFO("Loaded datetime library functions");
    } else {
        LOG_ERROR("Failed to load datetime library: %s", dlerror());
    }
    
    LOG_INFO("Finished registering default library functions");
}

static void register_dynamic_function(const char* name, void* func_ptr, FunctionSignature signature) {
    if (g_dynamic_function_count >= g_dynamic_function_capacity) {
        // Expand array if needed
        g_dynamic_function_capacity *= 2;
        g_dynamic_functions = realloc(g_dynamic_functions, 
            sizeof(DynamicFunction) * g_dynamic_function_capacity);
        if (!g_dynamic_functions) {
            LOG_ERROR("Failed to expand dynamic function array");
            return;
        }
    }
    
    DynamicFunction* df = &g_dynamic_functions[g_dynamic_function_count];
    df->name = strdup(name);
    df->library_func_ptr = func_ptr;
    df->signature = signature;
    
    // Store handler for FUNC_SIG_VALUE_ARGS, others use wrappers
    if (signature == FUNC_SIG_VALUE_ARGS) {
        df->handler = (Value (*)(int, Value*))func_ptr;
    } else {
        df->handler = NULL; // Will use wrapper functions via call_dynamic_function
    }
    
    g_dynamic_function_count++;
    LOG_DEBUG("Registered dynamic function: %s", name);
}

// Check if a name is a registered dynamic function
bool is_dynamic_function(const char* name) {
    if (!g_dynamic_functions || !name) return false;
    
    for (int i = 0; i < g_dynamic_function_count; i++) {
        if (strcmp(g_dynamic_functions[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// Create a function value for a dynamic function (placeholder)
Value create_dynamic_function_value(const char* name) {
    // For now, we'll create a special marker value that the VM can recognize
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(name); // This will be freed properly in vm_free
    return result;
}

// Wrapper functions for different signatures
static Value wrapper_void_void(int arg_count, Value* args, void* func_ptr) {
    (void)arg_count; (void)args; // Unused parameters
    void (*func)(void) = (void (*)(void))func_ptr;
    func();
    Value result = {VALUE_NIL};
    return result;
}

static Value wrapper_int_void(int arg_count, Value* args, void* func_ptr) {
    (void)arg_count; (void)args; // Unused parameters
    int (*func)(void) = (int (*)(void))func_ptr;
    int result = func();
    Value val;
    val.type = VALUE_NUMBER;
    val.as.number = (double)result;
    return val;
}

static Value wrapper_ptr_string(int arg_count, Value* args, void* func_ptr) {
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

static Value wrapper_int_ptr_string(int arg_count, Value* args, void* func_ptr) {
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

static Value wrapper_void_ptr(int arg_count, Value* args, void* func_ptr) {
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

static Value wrapper_ptr_void(int arg_count, Value* args, void* func_ptr) {
    (void)arg_count; (void)args; // Unused parameters
    
    void* (*func)(void) = (void* (*)(void))func_ptr;
    void* ptr = func();
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)(intptr_t)ptr;
    return result;
}

static Value wrapper_int_ptr(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int (*func)(void*) = (int (*)(void*))func_ptr;
    void* ptr = (void*)(intptr_t)args[0].as.number;
    int result = func(ptr);
    
    Value val;
    val.type = VALUE_NUMBER;
    val.as.number = (double)result;
    return val;
}

static Value wrapper_ptr_string_ptr(int arg_count, Value* args, void* func_ptr) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    void* (*func)(const char*, void*) = (void* (*)(const char*, void*))func_ptr;
    const char* str = args[0].as.string;
    void* ptr_arg = (void*)(intptr_t)args[1].as.number;
    void* result_ptr = func(str, ptr_arg);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)(intptr_t)result_ptr;
    return result;
}

// Lookup function for VM function calls
Value call_dynamic_function(const char* name, int arg_count, Value* args) {
    for (int i = 0; i < g_dynamic_function_count; i++) {
        if (strcmp(g_dynamic_functions[i].name, name) == 0) {
            DynamicFunction* df = &g_dynamic_functions[i];
            
            // Call the appropriate wrapper based on signature
            switch (df->signature) {
                case FUNC_SIG_VOID_VOID:
                    return wrapper_void_void(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_INT_VOID:
                    return wrapper_int_void(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_PTR_STRING:
                    return wrapper_ptr_string(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_PTR_STRING_PTR:
                    return wrapper_ptr_string_ptr(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_INT_PTR_STRING:
                    return wrapper_int_ptr_string(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_VOID_PTR:
                    return wrapper_void_ptr(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_PTR_VOID:
                    return wrapper_ptr_void(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_INT_PTR:
                    return wrapper_int_ptr(arg_count, args, df->library_func_ptr);
                case FUNC_SIG_VALUE_ARGS: {
                    if (df->library_func_ptr) {
                        // Use explicit function pointer typedef for proper calling convention
                        typedef Value (*ValueArgFunc)(int, Value*);
                        ValueArgFunc func = (ValueArgFunc)df->library_func_ptr;
                        Value result = func(arg_count, args);
                        return result;
                    } else {
                        LOG_WARNING("Library function pointer is NULL for %s", name);
                    }
                    break;
                }
                default:
                    LOG_WARNING("Unimplemented function signature for %s", name);
                    break;
            }
        }
    }
    
    // Function not found
    LOG_WARNING("Dynamic function not found: %s", name);
    Value result = {VALUE_NIL};
    return result;
}

void vm_cleanup_library_system(void) {
    // Clean up dynamic functions
    if (g_dynamic_functions) {
        for (int i = 0; i < g_dynamic_function_count; i++) {
            free((void*)g_dynamic_functions[i].name);
        }
        free(g_dynamic_functions);
        g_dynamic_functions = NULL;
    }
    
    g_dynamic_function_count = 0;
    g_dynamic_function_capacity = 0;
    
    // Clean up library loader
    library_loader_cleanup();
    
    LOG_INFO("VM library system cleanup completed");
}

// Function to get library statistics
LibraryStats get_library_stats(void) {
    LibraryStats stats = {0};
    
    stats.total_libraries = g_library_registry.library_count;
    stats.loaded_libraries = 0;
    stats.total_functions = g_dynamic_function_count;
    
    for (int i = 0; i < g_library_registry.library_count; i++) {
        if (g_library_registry.libraries[i].is_loaded) {
            stats.loaded_libraries++;
        }
    }
    
    return stats;
}

// Programmatic access functions for Kuyil scripts

// Get library count
Value vm_get_library_count(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)g_library_registry.library_count;
    return result;
}

// Get library name by index
Value vm_get_library_name(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int index = (int)args[0].as.number;
    if (index < 0 || index >= g_library_registry.library_count) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(g_library_registry.libraries[index].name);
    return result;
}

// Get library path by index
Value vm_get_library_path(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int index = (int)args[0].as.number;
    if (index < 0 || index >= g_library_registry.library_count) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(g_library_registry.libraries[index].path);
    return result;
}

// Check if library is loaded by index
Value vm_is_library_loaded(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int index = (int)args[0].as.number;
    if (index < 0 || index >= g_library_registry.library_count) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = g_library_registry.libraries[index].is_loaded;
    return result;
}

// Get library function count by index
Value vm_get_library_function_count(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    int index = (int)args[0].as.number;
    if (index < 0 || index >= g_library_registry.library_count) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)g_library_registry.libraries[index].function_count;
    return result;
}

// Get loaded libraries as formatted string
Value vm_get_loaded_libraries_info(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    
    // Calculate needed buffer size
    int total_size = 1024; // Base size
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        total_size += strlen(lib->name) + strlen(lib->path) + 200;
    }
    
    char* info = malloc(total_size);
    if (!info) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    strcpy(info, "Loaded Libraries:\n");
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        char lib_info[512];
        snprintf(lib_info, sizeof(lib_info), 
            "[%d] %s: %s (%s, %d functions)\n",
            i, lib->name, 
            lib->is_loaded ? "LOADED" : "FAILED",
            lib->path,
            lib->function_count);
        strcat(info, lib_info);
    }
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = info;
    return result;
}

// Get total function count across all libraries
Value vm_get_total_function_count(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)g_dynamic_function_count;
    return result;
}

// Inline Library Configuration Functions

// Add a library configuration inline: add_library("name", "path", optional)
Value vm_add_library(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* name = args[0].as.string;
    const char* path = args[1].as.string;
    bool optional = (arg_count >= 3 && args[2].type == VALUE_BOOL) ? args[2].as.boolean : true;
    
    // Add library to registry
    if (g_library_registry.library_count >= MAX_LIBRARIES) {
        LOG_WARNING("Maximum libraries reached, cannot add: %s", name);
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    SharedLibrary* lib = &g_library_registry.libraries[g_library_registry.library_count];
    strncpy(lib->name, name, MAX_NAME_LENGTH - 1);
    lib->name[MAX_NAME_LENGTH - 1] = '\0';
    strncpy(lib->path, path, MAX_PATH_LENGTH - 1);
    lib->path[MAX_PATH_LENGTH - 1] = '\0';
    lib->is_optional = optional;
    lib->is_loaded = false;
    lib->handle = NULL;
    lib->function_count = 0;
    
    g_library_registry.library_count++;
    
    LOG_INFO("Added library configuration: %s -> %s", name, path);
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Load a specific library by name: load_library("webview")
Value vm_load_library_inline(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* name = args[0].as.string;
    
    // Find library in registry
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (strcmp(lib->name, name) == 0) {
            if (!lib->is_loaded) {
                if (load_library(lib->name)) {
                    LOG_INFO("Successfully loaded library: %s", name);
                    Value result;
                    result.type = VALUE_BOOL;
                    result.as.boolean = true;
                    return result;
                } else {
                    LOG_WARNING("Failed to load library: %s", name);
                    Value result;
                    result.type = VALUE_BOOL;
                    result.as.boolean = false;
                    return result;
                }
            } else {
                LOG_INFO("Library already loaded: %s", name);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = true;
                return result;
            }
        }
    }
    
    LOG_WARNING("Library not found in registry: %s", name);
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = false;
    return result;
}

// Clear all library configurations: clear_libraries()
Value vm_clear_libraries(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    
    // Unload all loaded libraries
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (lib->is_loaded && lib->handle) {
            dlclose(lib->handle);
            lib->handle = NULL;
            lib->is_loaded = false;
            lib->function_count = 0;
        }
    }
    
    // Clear registry
    g_library_registry.library_count = 0;
    
    LOG_INFO("Cleared all library configurations");
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Module Import System

typedef struct {
    char name[256];
    char path[512];
    void* exports;  // Could be function table or other exports
} ImportedModule;

static ImportedModule g_imported_modules[64];
static int g_imported_module_count = 0;

// Import functions from another Kuyil module: import("path/to/module.kyl")
Value vm_import_module(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        Value result;
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* module_path = args[0].as.string;
    
    // Check if module already imported
    for (int i = 0; i < g_imported_module_count; i++) {
        if (strcmp(g_imported_modules[i].path, module_path) == 0) {
            LOG_INFO("Module already imported: %s", module_path);
            Value result;
            result.type = VALUE_BOOL;
            result.as.boolean = true;
            return result;
        }
    }
    
    // Read and compile the module file
    FILE* file = fopen(module_path, "r");
    if (!file) {
        LOG_ERROR("Cannot open module file: %s", module_path);
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    char* source = malloc(file_size + 1);
    if (!source) {
        fclose(file);
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    fread(source, 1, file_size, file);
    source[file_size] = '\0';
    fclose(file);
    
    // Extract module name from path
    const char* module_name = strrchr(module_path, '/');
    if (module_name) {
        module_name++; // Skip the '/'
    } else {
        module_name = module_path;
    }
    
    // Remove .kyl extension for module name
    char clean_name[256];
    strncpy(clean_name, module_name, sizeof(clean_name) - 1);
    char* dot = strrchr(clean_name, '.');
    if (dot) *dot = '\0';
    
    // Store imported module info
    if (g_imported_module_count < 64) {
        ImportedModule* module = &g_imported_modules[g_imported_module_count];
        strncpy(module->name, clean_name, sizeof(module->name) - 1);
        strncpy(module->path, module_path, sizeof(module->path) - 1);
        module->exports = NULL; // Will be populated when functions are exported
        g_imported_module_count++;
    }
    
    LOG_INFO("Module imported: %s from %s", clean_name, module_path);
    
    // TODO: Actually parse and execute the module to register its functions
    // For now, we simulate successful import
    free(source);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(clean_name);
    return result;
}

// Export a function from current module: export_function("function_name")
Value vm_export_function(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        Value result;
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* function_name = args[0].as.string;
    
    // TODO: Register function as exportable
    // For now, we just log the export
    LOG_INFO("Function exported: %s", function_name);
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Function to list loaded libraries
void list_loaded_libraries(void) {
    printf("\n=== Loaded Libraries ===\n");
    for (int i = 0; i < g_library_registry.library_count; i++) {
        SharedLibrary* lib = &g_library_registry.libraries[i];
        if (lib->is_loaded) {
            printf("Library: %s\n", lib->name);
            printf("  Path: %s\n", lib->path);
            printf("  Functions: %d\n", lib->function_count);
            
            for (int j = 0; j < lib->function_count && j < 5; j++) {
                if (lib->functions[j].is_loaded) {
                    printf("    - %s\n", lib->functions[j].name);
                }
            }
            if (lib->function_count > 5) {
                printf("    ... and %d more\n", lib->function_count - 5);
            }
        } else {
            printf("Library: %s (NOT LOADED)\n", lib->name);
        }
    }
    printf("Total: %d libraries, %d functions\n", 
            get_library_stats().loaded_libraries, g_dynamic_function_count);
}