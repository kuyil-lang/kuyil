#define _POSIX_C_SOURCE 200809L
#include "vm_library_integration.h"
#include "library_loader.h"
#include "logging.h"
#include "heapfs.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
// dlfcn.h is already handled in library_loader.h with Windows compatibility

// Forward declaration - vm_interpret is the public API for compiling and executing source
extern InterpretResult vm_interpret(VM* vm, const char* source);

// Global VM pointer for library access
static VM* g_current_vm = NULL;
// Global source path for error reporting across callbacks
static const char* g_current_source_path = NULL;
// Serialize re-entrant VM calls from libraries (e.g., HTTP threads)
static pthread_mutex_t g_vm_call_mutex = PTHREAD_MUTEX_INITIALIZER;

void set_current_vm(VM* vm) {
    g_current_vm = vm;
}

VM* get_current_vm(void) {
    return g_current_vm;
}

void set_current_source_path(const char* path) {
    g_current_source_path = path;
}

const char* get_current_source_path(void) {
    return g_current_source_path;
}

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

// Interface namespace registry (for export interface support)
#define MAX_INTERFACE_NAMES 128
static char* g_interface_names[MAX_INTERFACE_NAMES];
static int g_interface_count = 0;

static void register_interface_name(const char* name) {
    if (g_interface_count < MAX_INTERFACE_NAMES) {
        // Check if already registered
        for (int i = 0; i < g_interface_count; i++) {
            if (strcmp(g_interface_names[i], name) == 0) return;
        }
        g_interface_names[g_interface_count++] = strdup(name);
        fprintf(stderr, "[INTERFACE] Registered namespace: %s\n", name); fflush(stderr);
    }
}

bool is_interface_name(const char* name) {
    for (int i = 0; i < g_interface_count; i++) {
        if (strcmp(g_interface_names[i], name) == 0) return true;
    }
    return false;
}
static pthread_mutex_t g_dynamic_functions_mutex = PTHREAD_MUTEX_INITIALIZER;

// Hash table for O(1) function lookup
#define FUNCTION_HASH_SIZE 256
typedef struct FunctionHashEntry {
    const char* name;
    int function_index;  // Index into g_dynamic_functions array
    struct FunctionHashEntry* next;
} FunctionHashEntry;

static FunctionHashEntry* g_function_hash_table[FUNCTION_HASH_SIZE] = {NULL};

// Simple hash function for function names
static unsigned int hash_function_name(const char* name) {
    unsigned int hash = 5381;
    int c;
    while ((c = *name++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash % FUNCTION_HASH_SIZE;
}

// Alias binding metadata for interface-bound methods with type checks
typedef struct {
    char* alias;           // e.g., "substring" or "str.substring"
    char* target;          // e.g., "str_substring" or raw method name like "to_string"
    // Direct pointer to the resolved dynamic function for fast, recursion-safe dispatch
    // Note: this pointer refers to an entry in g_dynamic_functions; do not free.
    struct {
        const char* name;              // cached name for diagnostics
        void* library_func_ptr;        // underlying func ptr
        FunctionSignature signature;   // signature for dispatch
    } target_df;
    int param_count;       // number of params
    char** param_specs;    // e.g., ["inputStr:string", "indexStart:number|int32", ...]
    char* return_spec;     // e.g., "string" (optional)
} AliasBinding;

#define MAX_ALIAS_BINDINGS 512
static AliasBinding g_alias_bindings[MAX_ALIAS_BINDINGS];
static int g_alias_binding_count = 0;

static int find_alias_binding(const char* name) {
    for (int i = 0; i < g_alias_binding_count; i++) {
        if (strcmp(g_alias_bindings[i].alias, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void free_alias_bindings(void) {
    for (int i = 0; i < g_alias_binding_count; i++) {
        free(g_alias_bindings[i].alias);
        free(g_alias_bindings[i].target);
        if (g_alias_bindings[i].param_specs) {
            for (int j = 0; j < g_alias_bindings[i].param_count; j++) free(g_alias_bindings[i].param_specs[j]);
            free(g_alias_bindings[i].param_specs);
        }
        if (g_alias_bindings[i].return_spec) free(g_alias_bindings[i].return_spec);
    }
    g_alias_binding_count = 0;
}

/* moved add_alias_binding_entry below forward declarations */

// Case conversion helpers for aliasing and symbol discovery
static char* to_snake_case(const char* in) {
    if (!in) return NULL;
    size_t len = strlen(in);
    char* out = (char*)malloc(len * 2 + 1);
    if (!out) return NULL;
    char* w = out;
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') {
            if (i != 0 && in[i-1] != '_' && !(in[i-1] >= 'A' && in[i-1] <= 'Z')) *w++ = '_';
            *w++ = (char)(c - 'A' + 'a');
        } else {
            *w++ = c;
        }
    }
    *w = '\0';
    return out;
}

    // Forward declaration for helper used by add_alias_if_missing
    static void add_alias_binding_entry(const char* alias_name,
                                                    const char* target,
                                                    DynamicFunction* df,
                                                    int param_count,
                                                    char** param_specs,
                                                    const char* return_spec);

static char* to_camel_case(const char* in) {
    if (!in) return NULL;
    size_t len = strlen(in);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    size_t w = 0; bool up = false;
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        if (c == '_') { up = true; continue; }
        if (up && c >= 'a' && c <= 'z') { out[w++] = (char)(c - 'a' + 'A'); up = false; }
        else { out[w++] = c; up = false; }
    }
    out[w] = '\0';
    if (w > 0 && out[0] >= 'A' && out[0] <= 'Z') out[0] = (char)(out[0] - 'A' + 'a');
    return out;
}

static void add_alias_if_missing(const char* alias, const char* target, DynamicFunction* df,
                                 int pcount, char** pspecs, const char* rspec) {
    if (!alias || !*alias) return;
    if (find_alias_binding(alias) == -1) {
        add_alias_binding_entry(alias, target, df, pcount, pspecs, rspec);
    }
}

// Forward declaration
static int find_dynamic_function_idx(const char* name);

// Simple mock registry for testing
typedef struct {
    char* name;
    int call_count;
    int has_return;
    Value return_value;
} MockEntry;

static MockEntry g_mocks[256];
static int g_mock_count = 0;

static int find_mock(const char* name) {
    for (int i = 0; i < g_mock_count; i++) {
        if (strcmp(g_mocks[i].name, name) == 0) return i;
    }
    return -1;
}

void mock_set_return_value(const char* name, Value v) {
    int idx = find_mock(name);
    if (idx == -1) {
        if (g_mock_count >= 256) return;
        idx = g_mock_count++;
        g_mocks[idx].name = strdup(name);
        g_mocks[idx].call_count = 0;
    }
    g_mocks[idx].has_return = 1;
    g_mocks[idx].return_value = v;
}

void mock_clear(const char* name) {
    if (name == NULL) {
        // Clear all
        for (int i = 0; i < g_mock_count; i++) {
            free(g_mocks[i].name);
        }
        g_mock_count = 0;
        return;
    }
    int idx = find_mock(name);
    if (idx != -1) {
        free(g_mocks[idx].name);
        // shift down
        for (int i = idx; i < g_mock_count - 1; i++) {
            g_mocks[i] = g_mocks[i + 1];
        }
        g_mock_count--;
    }
}

int mock_get_call_count(const char* name) {
    int idx = find_mock(name);
    if (idx == -1) return 0;
    return g_mocks[idx].call_count;
}

// Forward declarations
static bool register_dynamic_functions(VM* vm);
static void register_system_functions(VM* vm);
static void register_dynamic_function(const char* name, void* func_ptr, FunctionSignature signature);
static Value wrapper_void_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_ptr_string(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_ptr_string(int arg_count, Value* args, void* func_ptr);
static Value wrapper_void_ptr(int arg_count, Value* args, void* func_ptr);
static Value wrapper_ptr_void(int arg_count, Value* args, void* func_ptr);
static Value wrapper_int_ptr(int arg_count, Value* args, void* func_ptr);

// Forward declaration for dlopen_only (loads library without registering functions)
Value vm_dlopen_only(int arg_count, Value* args);
// Forward declaration for interface method binder
Value vm_bind_interface_method(int arg_count, Value* args);

// Now that register_dynamic_function is declared, define the helper
static void add_alias_binding_entry(const char* alias_name,
                                    const char* target,
                                    DynamicFunction* df,
                                    int param_count,
                                    char** param_specs,
                                    const char* return_spec) {
    if (!is_dynamic_function(alias_name)) {
        register_dynamic_function(alias_name, df->library_func_ptr, df->signature);
    }
    if (g_alias_binding_count < MAX_ALIAS_BINDINGS) {
        AliasBinding* ab = &g_alias_bindings[g_alias_binding_count++];
        ab->alias = strdup(alias_name);
        ab->target = strdup(target);
        // Cache resolved dynamic function details to avoid re-entry via name-based resolution
        ab->target_df.name = df->name;
        ab->target_df.library_func_ptr = df->library_func_ptr;
        ab->target_df.signature = df->signature;
        ab->param_count = param_count;
        if (param_count > 0 && param_specs) {
            ab->param_specs = (char**)malloc(sizeof(char*) * param_count);
            for (int i = 0; i < param_count; i++) ab->param_specs[i] = strdup(param_specs[i] ? param_specs[i] : "");
        } else {
            ab->param_specs = NULL;
        }
        ab->return_spec = return_spec ? strdup(return_spec) : NULL;
    }
    LOG_INFO("Bound interface alias: %s -> %s", alias_name, target);
}

// Register a placeholder dynamic function alias (for resolution) and record
// a type-checked alias binding that forwards to the target name.
static void add_alias_binding_entry(const char* alias_name,
                                    const char* target,
                                    DynamicFunction* df,
                                    int param_count,
                                    char** param_specs,
                                    const char* return_spec);

static void build_path(char* out, size_t out_sz, const char* a, const char* b) {
    // join a + "/" + b with simple logic
    size_t la = strlen(a);
    bool need_slash = la > 0 && a[la-1] != '/';
    snprintf(out, out_sz, need_slash ? "%s/%s" : "%s%s", a, b);
}

static bool file_exists_simple(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return true; }
    return false;
}

static void get_executable_dir(char* out, size_t out_sz) {
#ifdef __linux__
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (len > 0) {
        buf[len] = '\0';
        // strip filename to directory
        char* last = strrchr(buf, '/');
        if (last) *last = '\0';
        strncpy(out, buf, out_sz-1);
        out[out_sz-1] = '\0';
        return;
    }
#endif
    // Fallback to current directory
    strncpy(out, ".", out_sz-1);
    out[out_sz-1] = '\0';
}

bool vm_init_library_system(VM* vm) {
    LOG_INFO("Initializing VM library system");
    
    // Initialize library loader
    if (!library_loader_init()) {
        LOG_ERROR("Failed to initialize library loader");
        return false;
    }
    // Provide Kuyil caller bridge to loader (generic, no library-specific logic here)
    library_loader_set_kuyil_caller((void*)call_kuyil_function);
    
    // Libraries are now loaded on-demand via interface imports
    // No automatic loading from libraries.conf
    LOG_INFO("Library system ready - use interface imports to load libraries on-demand");
    
    // Register library functions with VM
    if (!register_dynamic_functions(vm)) {
        LOG_ERROR("Failed to register library functions with VM");
        return false;
    }
    
    LOG_INFO("VM library system initialized successfully");
    return true;
}

static bool register_dynamic_functions(VM* vm) {
    // Allocate dynamic function array
    g_dynamic_function_capacity = 256;
    g_dynamic_functions = malloc(sizeof(DynamicFunction) * g_dynamic_function_capacity);
    if (!g_dynamic_functions) {
        LOG_ERROR("Failed to allocate dynamic function array");
        return false;
    }
    
    // No library-specific injections here; handled by loader

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
    // Load library handle only without registering functions (for interface from syntax)
    register_dynamic_function("dlopen_only", vm_dlopen_only, FUNC_SIG_VALUE_ARGS);
    // Bind an interface method name to an underlying dynamic function
    register_dynamic_function("bind_interface_method", vm_bind_interface_method, FUNC_SIG_VALUE_ARGS);
    
    // Register module import functions
    register_dynamic_function("import", vm_import_module, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("import_as", vm_import_as, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("export_function", vm_export_function, FUNC_SIG_VALUE_ARGS);
    
    // Register HeapFS embedded filesystem functions
    register_dynamic_function("heapfs_info", kuyil_heapfs_info, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("heapfs_exists", kuyil_heapfs_exists, FUNC_SIG_VALUE_ARGS);
    register_dynamic_function("heapfs_list", kuyil_heapfs_list, FUNC_SIG_VALUE_ARGS);
    
    // Keep VM generic; avoid direct library-specific dlopen here
    
    // Dynamic functions are handled by the dispatch system, not as globals
    LOG_INFO("Registered 15 library system functions");
}

/* removed library-specific default dlopen registration to keep VM generic */

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
    
    // Add to hash table for O(1) lookup
    unsigned int hash = hash_function_name(name);
    FunctionHashEntry* entry = malloc(sizeof(FunctionHashEntry));
    if (entry) {
        entry->name = df->name;  // Use the same strdup'd pointer
        entry->function_index = g_dynamic_function_count;
        entry->next = g_function_hash_table[hash];
        g_function_hash_table[hash] = entry;
    }
    
    g_dynamic_function_count++;
    LOG_DEBUG("Registered dynamic function: %s", name);
}

// Public wrapper for register_dynamic_function (called from library_loader)
void vm_register_dynamic_function(const char* name, void* func_ptr, FunctionSignature signature) {
    register_dynamic_function(name, func_ptr, signature);
}

// Check if a name is a registered dynamic function
bool is_dynamic_function(const char* name) {
    if (!g_dynamic_functions || !name) return false;
    
    pthread_mutex_lock(&g_dynamic_functions_mutex);
    
    // Use hash table for O(1) lookup
    unsigned int hash = hash_function_name(name);
    FunctionHashEntry* entry = g_function_hash_table[hash];
    
    // Debug: fprintf(stderr, "[LOOKUP] Checking if '%s' is dynamic (hash=%u)...\n", name, hash); fflush(stderr);
    
    while (entry) {
        // Debug: fprintf(stderr, "[LOOKUP]   Found entry: '%s'\n", entry->name); fflush(stderr);
        if (strcmp(entry->name, name) == 0) {
            // Debug: fprintf(stderr, "[LOOKUP] '%s' = TRUE\n", name); fflush(stderr);
            pthread_mutex_unlock(&g_dynamic_functions_mutex);
            return true;
        }
        entry = entry->next;
    }
    
    // Debug: fprintf(stderr, "[LOOKUP] '%s' = FALSE\n", name); fflush(stderr);
    pthread_mutex_unlock(&g_dynamic_functions_mutex);
    return false;
}

// Shared function lookup logic for both main VM and avatars
// Returns VALUE_NIL if not found, otherwise returns the function as VALUE_STRING
Value vm_lookup_function_shared(const char* name, VM* main_vm) {
    if (!name) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    // Priority 1: Check dynamic functions via O(1) hash table
    if (is_dynamic_function(name)) {
        Value result;
        result.type = VALUE_STRING;
        result.as.string = (char*)name; // Safe: name is from constants
        return result;
    }
    
    // Priority 2: Check user-defined globals (linear search fallback)
    if (main_vm) {
        for (int i = 0; i < main_vm->global_count; i++) {
            if (main_vm->globals[i].name && strcmp(main_vm->globals[i].name, name) == 0) {
                return main_vm->globals[i].value;
            }
        }
    }
    
    // Not found
    Value nil = {VALUE_NIL};
    return nil;
}

// Create a function value for a dynamic function (placeholder)
Value create_dynamic_function_value(const char* name) {
    // For now, we'll create a special marker value that the VM can recognize
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(name); // This will be freed properly in vm_free
    return result;
}

// Look up a global variable by name in the VM (exported for library use)
__attribute__((visibility("default")))
Value vm_lookup_global(const char* name) {
    VM* vm = g_current_vm;
    if (!vm) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    for (int i = 0; i < vm->global_count; i++) {
        if (vm->globals[i].name && strcmp(vm->globals[i].name, name) == 0) {
            return vm->globals[i].value;
        }
    }
    
    Value nil = {VALUE_NIL};
    return nil;
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
    // Trace dynamic function calls for debugging crashes (e.g., datetime/date_unix)
    LOG_DEBUG("call_dynamic_function: name=%s argc=%d", name ? name : "<null>", arg_count);
    // If this name is an interface alias, enforce type checks and dispatch to target
    int alias_idx = find_alias_binding(name);
    if (alias_idx != -1) {
        AliasBinding* ab = &g_alias_bindings[alias_idx];
        // Arity check - but skip for VALUE_ARGS functions (they handle their own arity)
        if (ab->target_df.signature != FUNC_SIG_VALUE_ARGS && ab->param_count != arg_count) {
            LOG_ERROR("Arity mismatch calling %s: expected %d, got %d", name, ab->param_count, arg_count);
            Value nilv = {VALUE_NIL};
            return nilv;
        }

        // Check each argument against allowed typeset
        for (int i = 0; i < ab->param_count; i++) {
            const char* spec = ab->param_specs ? ab->param_specs[i] : NULL;
            if (!spec) continue; // no spec, skip
            // Find ':' delimiter to skip name
            const char* colon = strchr(spec, ':');
            const char* types = colon ? colon + 1 : spec;
            // Skip leading whitespace
            while (*types == ' ' || *types == '\t') types++;
            // Make a mutable copy to tokenize by '|'
            char buf[256];
            strncpy(buf, types, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
            bool ok = false;
            char* saveptr = NULL;
            char* tok = strtok_r(buf, "|", &saveptr);
            while (tok) {
                // Trim each token
                while (*tok == ' ' || *tok == '\t') tok++;
                char* end = tok + strlen(tok) - 1;
                while (end >= tok && (*end == ' ' || *end == '\t')) { *end = '\0'; end--; }

                // Map token to Value type acceptance
                if (strcasecmp(tok, "string") == 0) {
                    if (args[i].type == VALUE_STRING) { ok = true; break; }
                } else if (
                    // Generic number
                    strcasecmp(tok, "number") == 0 ||
                    // Signed integers: int, int16/32/64
                    strncasecmp(tok, "int", 3) == 0 ||
                    // Unsigned integers: uint16/32/64 and aliases
                    strncasecmp(tok, "uint", 4) == 0 ||
                    strcasecmp(tok, "byte") == 0 ||
                    // Floating-point: float, double, float16/32/64
                    strcasecmp(tok, "float") == 0 ||
                    strcasecmp(tok, "double") == 0 ||
                    strncasecmp(tok, "float", 5) == 0
                ) {
                    if (args[i].type == VALUE_NUMBER) { ok = true; break; }
                } else if (strcasecmp(tok, "bool") == 0 || strcasecmp(tok, "boolean") == 0) {
                    if (args[i].type == VALUE_BOOL) { ok = true; break; }
                } else if (strcasecmp(tok, "nil") == 0 || strcasecmp(tok, "null") == 0) {
                    if (args[i].type == VALUE_NIL) { ok = true; break; }
                } else if (strcasecmp(tok, "array") == 0) {
                    if (args[i].type == VALUE_ARRAY) { ok = true; break; }
                } else if (strcasecmp(tok, "object") == 0 || strcasecmp(tok, "map") == 0) {
                    if (args[i].type == VALUE_OBJECT) { ok = true; break; }
                } else {
                    // Unknown type token: be permissive
                    ok = true; break;
                }
                tok = strtok_r(NULL, "|", &saveptr);
            }
            if (!ok) {
                LOG_ERROR("Type mismatch calling %s: param %d does not match types '%s'", name, i+1, types);
                // TEMP: Continue anyway for compatibility during type system transition
                // Value nilv = {VALUE_NIL};
                // return nilv;
            }
        }
        
        // Dispatch to target function (directly via cached function pointer when possible)
        Value result;
        if (ab->target_df.library_func_ptr != NULL) {
            switch (ab->target_df.signature) {
                case FUNC_SIG_VALUE_ARGS: {
                    typedef Value (*ValueArgFunc)(int, Value*);
                    ValueArgFunc func = (ValueArgFunc)ab->target_df.library_func_ptr;
            LOG_DEBUG("[alias-dispatch] '%s' -> '%s' (ptr=%p) with %d args",
                  name, ab->target_df.name, ab->target_df.library_func_ptr, arg_count);
                    result = func(arg_count, args);
            LOG_DEBUG("[alias-dispatch] '%s' returned type=%d", name, result.type);
                    break;
                }
                case FUNC_SIG_VOID_VOID:
                case FUNC_SIG_INT_VOID:
                case FUNC_SIG_PTR_STRING:
                case FUNC_SIG_PTR_STRING_PTR:
                case FUNC_SIG_INT_PTR_STRING:
                case FUNC_SIG_VOID_PTR:
                case FUNC_SIG_PTR_VOID:
                case FUNC_SIG_INT_PTR: {
                    // For non-VALUE_ARGS signatures, fall back to name-based dispatch via the general wrapper
                    // to reuse existing adapters.
                    result = call_dynamic_function(ab->target, arg_count, args);
                    break;
                }
                default: {
                    // Unknown signature; defensively use name-based dispatch
                    result = call_dynamic_function(ab->target, arg_count, args);
                    break;
                }
            }
        } else {
            // Fallback: name-based dispatch (should be rare)
            result = call_dynamic_function(ab->target, arg_count, args);
        }

        // Best-effort return type check
        if (ab->return_spec && ab->return_spec[0]) {
            const char* types = ab->return_spec;
            char buf[256];
            strncpy(buf, types, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
            bool ok = false;
            char* saveptr2 = NULL;
            char* tok2 = strtok_r(buf, "|", &saveptr2);
            while (tok2) {
                while (*tok2 == ' ' || *tok2 == '\t') tok2++;
                char* end2 = tok2 + strlen(tok2) - 1;
                while (end2 >= tok2 && (*end2 == ' ' || *end2 == '\t')) { *end2 = '\0'; end2--; }
                if (strcasecmp(tok2, "string") == 0) { if (result.type == VALUE_STRING) { ok = true; break; } }
                else if (
                    strcasecmp(tok2, "number") == 0 ||
                    strncasecmp(tok2, "int", 3) == 0 ||
                    strncasecmp(tok2, "uint", 4) == 0 ||
                    strcasecmp(tok2, "byte") == 0 ||
                    strcasecmp(tok2, "float") == 0 ||
                    strcasecmp(tok2, "double") == 0 ||
                    strncasecmp(tok2, "float", 5) == 0
                ) { if (result.type == VALUE_NUMBER) { ok = true; break; } }
                else if (strcasecmp(tok2, "bool") == 0 || strcasecmp(tok2, "boolean") == 0) { if (result.type == VALUE_BOOL) { ok = true; break; } }
                else if (strcasecmp(tok2, "nil") == 0 || strcasecmp(tok2, "null") == 0) { if (result.type == VALUE_NIL) { ok = true; break; } }
                else if (strcasecmp(tok2, "array") == 0) { if (result.type == VALUE_ARRAY) { ok = true; break; } }
                else if (strcasecmp(tok2, "object") == 0 || strcasecmp(tok2, "map") == 0) { if (result.type == VALUE_OBJECT) { ok = true; break; } }
                else { ok = true; break; }
                tok2 = strtok_r(NULL, "|", &saveptr2);
            }
            if (!ok) {
                // Allow graceful fallback: if expected is string but got NIL (e.g. file read fail), suppress noisy warning
                const char* type_names[] = {"NIL", "BOOL", "NUMBER", "STRING", "ARRAY", "OBJECT", "FUNCTION"};
                const char* type_name = (result.type >= 0 && result.type < 7) ? type_names[result.type] : "UNKNOWN";
                // Suppress noisy warnings for NIL fallback or unknown out-of-range types
                if ((strcasecmp(ab->return_spec, "string") == 0 && result.type == VALUE_NIL) || strcmp(type_name, "UNKNOWN") == 0) {
                    LOG_DEBUG("Return type check suppressed for %s (expected '%s', got %s type=%d)", name, ab->return_spec, type_name, result.type);
                } else {
                    LOG_WARNING("Return type mismatch calling %s: expected '%s', got %s (type=%d)", name, ab->return_spec, type_name, result.type);
                }
            }
        }

        return result;
    }
    // Mock interception: record calls and optionally return a mocked value
    int midx = find_mock(name);
    if (midx != -1) {
        g_mocks[midx].call_count++;
        if (g_mocks[midx].has_return) {
            return g_mocks[midx].return_value;
        }
        // fallthrough to real call if no forced return
    }
    
    pthread_mutex_lock(&g_dynamic_functions_mutex);
    
    // Use hash table for O(1) lookup
    unsigned int hash = hash_function_name(name);
    FunctionHashEntry* entry = g_function_hash_table[hash];
    DynamicFunction* df = NULL;
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            df = &g_dynamic_functions[entry->function_index];
            break;
        }
        entry = entry->next;
    }
    
    if (df) {
        // Unlock before calling the actual function (which may take time)
        pthread_mutex_unlock(&g_dynamic_functions_mutex);
        
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
                        Value result = ((Value (*)(int, Value*))df->library_func_ptr)(arg_count, args);
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
            // If we reach here, return nil
            Value nilv = {VALUE_NIL};
            return nilv;
    }
    
    pthread_mutex_unlock(&g_dynamic_functions_mutex);
    
    // Function not found
    LOG_WARNING("Dynamic function not found: %s", name);
    Value result = {VALUE_NIL};
    return result;
}

// Call a Kuyil function from C code
// This is used by library code (like HTTP callbacks) to invoke Kuyil functions
bool call_kuyil_function(Value function_value, int arg_count, Value* args, Value* result_out) {
    pthread_mutex_lock(&g_vm_call_mutex);
    VM* vm = g_current_vm;
    if (!vm) {
        LOG_ERROR("Cannot call Kuyil function: VM not available");
        pthread_mutex_unlock(&g_vm_call_mutex);
        return false;
    }

    if (function_value.type != VALUE_FUNCTION) {
        LOG_ERROR("Cannot call Kuyil function: value is not a function");
        pthread_mutex_unlock(&g_vm_call_mutex);
        return false;
    }

    Function* function = function_value.as.function.function;
    if (!function) {
        LOG_ERROR("Cannot call Kuyil function: function pointer is NULL");
        pthread_mutex_unlock(&g_vm_call_mutex);
        return false;
    }

    // Arity check (best effort)
    if (function->arity != arg_count) {
        LOG_WARNING("Arity mismatch calling Kuyil function: expected %d, got %d", function->arity, arg_count);
    }

    // If function has no code, simulate a no-op call returning nil
    if (function->chunk.count == 0) {
        Value nilv = {VALUE_NIL};
        if (result_out) *result_out = nilv;
        pthread_mutex_unlock(&g_vm_call_mutex);
        return true;
    }

    // Save current VM execution state
    // CRITICAL FIX: For nested calls (webview JS callbacks during execution),
    // we must preserve the ENTIRE stack to avoid corrupting caller's local variables
    int saved_frame_count = vm->frame_count;
    Value* saved_stack_top = vm->stack_top;
    
    // For nested calls, backup the entire stack
    Value* saved_stack = NULL;
    size_t saved_stack_size = 0;
    bool is_nested = (saved_frame_count > 0);
    
    if (is_nested) {
        // Calculate stack size and backup
        saved_stack_size = saved_stack_top - vm->stack;
        if (saved_stack_size > 0) {
            saved_stack = malloc(saved_stack_size * sizeof(Value));
            if (saved_stack) {
                memcpy(saved_stack, vm->stack, saved_stack_size * sizeof(Value));
                LOG_DEBUG("Backed up %zu stack values for nested call", saved_stack_size);
            } else {
                LOG_ERROR("Failed to allocate stack backup for nested call");
                pthread_mutex_unlock(&g_vm_call_mutex);
                return false;
            }
        }
        // Reset to clean state for nested execution
        vm->frame_count = 0;
        vm->stack_top = vm->stack;
    } else {
        // Top-level callback after script finished - just reset
        vm->frame_count = 0;
        vm->stack_top = vm->stack;
    }

    // Push args then function
    for (int i = 0; i < arg_count; i++) vm_push(vm, args[i]);
    vm_push(vm, function_value);

    // Create a call frame
    if (vm->frame_count >= FRAMES_MAX) {
        LOG_ERROR("VM frame overflow during callback");
        vm->stack_top = saved_stack_top;
        vm->frame_count = saved_frame_count;
        pthread_mutex_unlock(&g_vm_call_mutex);
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack_top - arg_count - 1;
    
    // CRITICAL: With proper SET_LOCAL/GET_LOCAL implementation:
    // - frame->slots[0..arg_count-1] = parameters (already on stack)
    // - frame->slots[arg_count..local_count-1] = local variables (need stack space!)
    // - Must allocate stack space for ALL locals, not just arguments
    int local_count = function->local_count;
    vm->stack_top = frame->slots + arg_count;
    
    // Allocate and initialize local variable slots (beyond parameters)
    for (int i = arg_count; i < local_count; i++) {
        if (vm->stack_top >= vm->stack + STACK_MAX) {
            LOG_ERROR("VM stack overflow allocating locals during callback");
            vm->stack_top = saved_stack_top;
            vm->frame_count = saved_frame_count;
            if (is_nested && saved_stack) {
                free(saved_stack);
            }
            pthread_mutex_unlock(&g_vm_call_mutex);
            return false;
        }
        Value nil_val = {VALUE_NIL};
        *vm->stack_top++ = nil_val;
    }

    // Ensure source path is set for stack traces during callbacks
    if (vm->current_source_path == NULL) {
        const char* src = get_current_source_path();
        if (src) vm->current_source_path = src;
    }

    // Run the VM - it will execute until OP_RETURN brings frame_count back to 0
    InterpretResult r = vm_run(vm);

    bool ok = (r == INTERPRET_OK);
    if (ok && result_out) {
        // The result should be on the stack
        if (vm->stack_top > vm->stack) {
            *result_out = *(vm->stack_top - 1);
        } else {
            result_out->type = VALUE_NIL;
        }
    }

    // Restore VM state
    if (is_nested && saved_stack) {
        // Restore the entire stack for nested calls
        memcpy(vm->stack, saved_stack, saved_stack_size * sizeof(Value));
        free(saved_stack);
        LOG_DEBUG("Restored %zu stack values after nested call", saved_stack_size);
    }
    vm->stack_top = saved_stack_top;
    vm->frame_count = saved_frame_count;

    pthread_mutex_unlock(&g_vm_call_mutex);
    return ok;
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

    // Clean up mocks
    for (int i = 0; i < g_mock_count; i++) {
        free(g_mocks[i].name);
    }
    g_mock_count = 0;
    // Clean up alias bindings
    free_alias_bindings();
    
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
    VM* vm;         // Keep the VM alive for function references
} ImportedModule;

static ImportedModule g_imported_modules[64];
static int g_imported_module_count = 0;

// Import functions from another Kuyil module: import("path/to/module.kyl")
Value vm_import_module(int arg_count, Value* args) {
    LOG_DEBUG("vm_import_module called with arg_count=%d", arg_count);
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        LOG_DEBUG("vm_import_module: Invalid arguments");
        Value result;
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* module_path = args[0].as.string;
    LOG_DEBUG("vm_import_module: Importing '%s'", module_path);
    const char* original_module_name = module_path;
    
    // Read and compile the module file (try relative to caller first)
    FILE* file = fopen(module_path, "r");
    
    // If simple name without path separators and .kyl extension, try multiple locations
    if (!file && !strchr(module_path, '/') && !strstr(module_path, ".kyl")) {
        LOG_DEBUG("Trying to resolve module: %s", module_path);
        
        // 1. Try interfaces/interface_<name>.kyl (primary for built-in interfaces)
        char interface_path[1024];
        snprintf(interface_path, sizeof(interface_path), "interfaces/interface_%s.kyl", module_path);
        LOG_DEBUG("Checking interface path: %s", interface_path);
        file = fopen(interface_path, "r");
        if (file) {
            module_path = strdup(interface_path);
            LOG_INFO("Resolved '%s' to interface file: %s", original_module_name, interface_path);
        }
        
        // 2. Fallback to kyllibs/<name>.kyl (for user libraries)
        if (!file) {
            char kyllibs_path[1024];
            snprintf(kyllibs_path, sizeof(kyllibs_path), "kyllibs/%s.kyl", module_path);
            LOG_DEBUG("Checking kyllibs path: %s", kyllibs_path);
            file = fopen(kyllibs_path, "r");
            if (file) {
                module_path = strdup(kyllibs_path);
                LOG_INFO("Resolved '%s' to kyllibs: %s", original_module_name, kyllibs_path);
            }
        }
    }
    
    // Check if module already imported (AFTER path resolution)
    for (int i = 0; i < g_imported_module_count; i++) {
        if (strcmp(g_imported_modules[i].path, module_path) == 0) {
            LOG_INFO("Module already imported: %s", module_path);
            if (file) fclose(file);
            Value result;
            result.type = VALUE_BOOL;
            result.as.boolean = true;
            return result;
        }
    }
    
    if (!file) {
        // Attempt to resolve relative to the current script directory
        const char* base_path = (g_current_vm && g_current_vm->current_source_path)
                                ? g_current_vm->current_source_path
                                : NULL;
        if (base_path) {
            const char* last_slash = strrchr(base_path, '/');
            if (last_slash) {
                char resolved[1024];
                size_t dir_len = (size_t)(last_slash - base_path);
                if (dir_len >= sizeof(resolved)) dir_len = sizeof(resolved) - 1;
                memcpy(resolved, base_path, dir_len);
                resolved[dir_len] = '\0';
                strncat(resolved, "/", sizeof(resolved) - strlen(resolved) - 1);
                strncat(resolved, module_path, sizeof(resolved) - strlen(resolved) - 1);
                file = fopen(resolved, "r");
                if (file) {
                    module_path = strdup(resolved);
                }
            }
        }
            // Try KUYIL_HOME as fallback (for installed interfaces)
            if (!file) {
                const char* kuyil_home = getenv("KUYIL_HOME");
                if (kuyil_home) {
                    char kuyil_home_path[1024];
                    snprintf(kuyil_home_path, sizeof(kuyil_home_path), "%s/%s", kuyil_home, module_path);
                    file = fopen(kuyil_home_path, "r");
                    if (file) {
                        module_path = strdup(kuyil_home_path);
                    }
                }
            }
        if (!file) {
            LOG_ERROR("Cannot open module file: %s", module_path);
            Value result;
            result.type = VALUE_BOOL;
            result.as.boolean = false;
            return result;
        }
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
    
    LOG_INFO("Module importing: %s from %s", clean_name, module_path);
    
    // Execute the module in the CURRENT VM so exported functions are automatically
    // available as globals in the calling scope.
    if (!g_current_vm) {
        LOG_ERROR("No current VM for module import: %s", module_path);
        free(source);
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    // Save and update source path for better error reporting
    const char* old_source_path = g_current_vm->current_source_path;
    g_current_vm->current_source_path = module_path;
    
    // Save VM state before nested interpretation
    Value* saved_stack_top = g_current_vm->stack_top;
    
    // Clear previous export list before compilation
    compiler_clear_exports();
    
    // Interpret the module source directly in the current VM
    InterpretResult result_code = vm_interpret(g_current_vm, source);
    
    // Restore VM stack state after nested interpretation
    g_current_vm->stack_top = saved_stack_top;
    
    // Restore source path
    g_current_vm->current_source_path = old_source_path;
    
    free(source);
    
    if (result_code != INTERPRET_OK) {
        LOG_ERROR("Failed to execute module: %s", module_path);
        compiler_clear_exports();
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    // Get the list of exported functions
    int export_count = 0;
    const char** exports = compiler_get_exports(&export_count);
    
    if (export_count > 0) {
        LOG_INFO("Module exported %d functions", export_count);
    }
    compiler_clear_exports();
    
    // Store the imported module info
    if (g_imported_module_count < 64) {
        ImportedModule* module = &g_imported_modules[g_imported_module_count];
        strncpy(module->name, clean_name, sizeof(module->name) - 1);
        module->name[sizeof(module->name) - 1] = '\0';
        strncpy(module->path, module_path, sizeof(module->path) - 1);
        module->path[sizeof(module->path) - 1] = '\0';
        module->exports = NULL;
        module->vm = NULL;  // No separate VM needed
        g_imported_module_count++;
    }
    
    LOG_INFO("Module loaded successfully: %s", clean_name);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(clean_name);
    return result;
}

// Import module with namespace: import_as("path", "namespace")
// Creates an object that forwards method calls with namespace prefix
Value vm_import_as(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        LOG_ERROR("import_as requires two string arguments: path and namespace");
        Value result;
        result.type = VALUE_NIL;
        return result;
    }
    
    const char* module_path = args[0].as.string;
    const char* namespace = args[1].as.string;
    
    // First, import the module normally
    Value import_args[1] = {args[0]};
    Value import_result = vm_import_module(1, import_args);
    
    if (import_result.type == VALUE_NIL || (import_result.type == VALUE_BOOL && !import_result.as.boolean)) {
        LOG_ERROR("Failed to import module: %s", module_path);
        Value result;
        result.type = VALUE_NIL;
        return result;
    }
    
    LOG_INFO("Module imported with namespace: %s as %s", module_path, namespace);
    
        // Extract interface name from path (e.g., "../interfaces/interface_str.kyl" -> "str")
        const char* basename = strrchr(module_path, '/');
        basename = basename ? basename + 1 : module_path;
        char interface_name[256] = {0};
        if (strncmp(basename, "interface_", 10) == 0) {
            const char* name_start = basename + 10;
            const char* dot = strrchr(name_start, '.');
            size_t len = dot ? (size_t)(dot - name_start) : strlen(name_start);
            if (len < sizeof(interface_name)) {
                memcpy(interface_name, name_start, len);
                interface_name[len] = '\0';
            }
        }
        LOG_INFO("Derived interface name: '%s'", interface_name[0] ? interface_name : "(none)");

        // Create namespace-prefixed alias bindings for each interface method if possible
        if (interface_name[0]) {
            for (int i = 0; i < g_alias_binding_count; i++) {
                AliasBinding* ab = &g_alias_bindings[i];
                // Look for aliases like "<interface>.<method>"
                size_t iface_len = strlen(interface_name);
                if (strncmp(ab->alias, interface_name, iface_len) == 0 && ab->alias[iface_len] == '.') {
                    const char* method_part = ab->alias + iface_len + 1; // skip interface_name + '.'
                    char ns_alias[256];
                    snprintf(ns_alias, sizeof(ns_alias), "%s.%s", namespace, method_part);
                    // Register alias if not already present
                    add_alias_binding_entry(ns_alias, ab->target, &ab->target_df,
                                            ab->param_count, ab->param_specs, ab->return_spec);
                }
            }
        }
    
    // Create a namespace object with methods as function values
    // For now, return a simple marker object
    Value result;
    result.type = VALUE_OBJECT;
    result.as.object.count = 0;
    result.as.object.keys = NULL;
    result.as.object.values = NULL;
    
        // Store namespace name and interface name as hidden properties
        result.as.object.count = 2;
        result.as.object.keys = malloc(sizeof(char*) * 2);
        result.as.object.values = malloc(sizeof(Value) * 2);
        result.as.object.keys[0] = strdup("__namespace__");
        result.as.object.values[0].type = VALUE_STRING;
        result.as.object.values[0].as.string = strdup(namespace);
        result.as.object.keys[1] = strdup("__interface__");
        result.as.object.values[1].type = VALUE_STRING;
        result.as.object.values[1].as.string = strdup(interface_name);
    
    return result;
}

// Helper: derive a library name from a path like ./libs/libkylstr.so -> str
static void derive_library_name(const char* path, char* out, size_t outsz) {
    if (!path || !out || outsz == 0) return;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    // Copy basename
    char tmp[256];
    strncpy(tmp, base, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    // Strip extension .so if present
    char* dot = strrchr(tmp, '.');
    if (dot && strcmp(dot, ".so") == 0) *dot = '\0';
    // Strip libkyl or lib prefix
    const char* name = tmp;
    if (strncmp(name, "libkyl", 6) == 0) name += 6;
    else if (strncmp(name, "lib", 3) == 0) name += 3;
    // Copy to out with explicit null termination
    size_t len = strlen(name);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

// dlopen_only: Load library handle WITHOUT registering functions
// Used by "interface X from" syntax - functions registered later via bind_interface_method
Value vm_dlopen_only(int arg_count, Value* args) {
    LOG_INFO("[vm_dlopen_only] CALLED with %d arguments", arg_count);
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        LOG_ERROR("dlopen_only: Missing or invalid path argument");
        Value result = {VALUE_NIL};
        return result;
    }

    const char* path = args[0].as.string;
    LOG_INFO("dlopen_only called with path: %s", path);

    // Derive library name from path
    char name_buf[128];
    derive_library_name(path, name_buf, sizeof(name_buf));

    if (name_buf[0] == '\0') {
        LOG_WARNING("dlopen_only: could not derive library name from path: %s", path);
        Value result = {VALUE_BOOL};
        result.as.boolean = false;
        return result;
    }

    // Check if already in registry
    SharedLibrary* lib = NULL;
    for (int i = 0; i < g_library_registry.library_count; i++) {
        if (strcmp(g_library_registry.libraries[i].name, name_buf) == 0) {
            lib = &g_library_registry.libraries[i];
            LOG_INFO("dlopen_only: library '%s' already registered", name_buf);
            break;
        }
    }

    // Add to registry if not present
    if (!lib) {
        if (g_library_registry.library_count >= MAX_LIBRARIES) {
            LOG_WARNING("dlopen_only: maximum libraries reached; cannot add %s", name_buf);
            Value result = {VALUE_BOOL};
            result.as.boolean = false;
            return result;
        }
        lib = &g_library_registry.libraries[g_library_registry.library_count++];
        memset(lib, 0, sizeof(*lib));
        strncpy(lib->name, name_buf, MAX_NAME_LENGTH - 1);
        lib->name[MAX_NAME_LENGTH - 1] = '\0';
        strncpy(lib->path, path, MAX_PATH_LENGTH - 1);
        lib->path[MAX_PATH_LENGTH - 1] = '\0';
        lib->is_optional = true;
        lib->is_loaded = false;
        lib->handle = NULL;
        lib->function_count = 0;
        LOG_INFO("dlopen_only: added library '%s' from path '%s'", lib->name, lib->path);
    }

    // Load ONLY the library handle - do NOT enumerate or register functions
    // This is the key difference from vm_loadlib
    if (!lib->is_loaded && lib->handle == NULL) {
        lib->handle = dlopen(lib->path, RTLD_LAZY);
        if (!lib->handle) {
            LOG_WARNING("dlopen_only: failed to open %s: %s", lib->path, dlerror());
            Value result = {VALUE_BOOL};
            result.as.boolean = false;
            return result;
        }
        lib->is_loaded = true;
        LOG_INFO("dlopen_only: opened library handle for '%s'", lib->name);
    }

    // Return library name
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup(lib->name);
    return result;
}

// Helper: find dynamic function by name and return its index in the registry, or -1
static int find_dynamic_function_idx(const char* name) {
    if (!g_dynamic_functions || !name) return -1;
    for (int i = 0; i < g_dynamic_function_count; i++) {
        if (strcmp(g_dynamic_functions[i].name, name) == 0) return i;
    }
    return -1;
}

// Runtime: bind an interface method name to an underlying dynamic function
// Usage from script (emitted by parser for interface blocks):
//   bind_interface_method("str", "substring", params_array, return_spec, aliases_array, is_exported)
// Args:
//   0: interface name (string)
//   1: method name (string)
//   2: optional param specs array
//   3: optional return type spec string
//   4: optional aliases array
//   5: optional is_exported boolean (if true, only create namespaced aliases like "str.substring")
// This will look for the C function and create convenient aliases
Value vm_bind_interface_method(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value r = {VALUE_BOOL};
        r.as.boolean = false;
        return r;
    }

    const char* iface = args[0].as.string;
    const char* method = args[1].as.string;
    char* method_snake = to_snake_case(method);
    char* method_camel = to_camel_case(method);
    // Optional arguments
    Value param_arr = {VALUE_NIL};
    const char* return_spec = NULL;
    bool is_exported = false;
    
    if (arg_count >= 3) param_arr = args[2];
    if (arg_count >= 4 && args[3].type == VALUE_STRING) return_spec = args[3].as.string;
    // arg 4 is aliases array (handled later)
    // is_exported is ALWAYS the last argument (can be at index 4, 5, or 6 depending on optional args)
    if (arg_count >= 3) {
        Value last_arg = args[arg_count - 1];
        if (last_arg.type == VALUE_BOOL) {
            is_exported = last_arg.as.boolean;
        }
    }
    // Debug: fprintf(stderr, "[BIND] %s.%s: is_exported=%d (arg_count=%d)\n", iface, method, is_exported, arg_count); fflush(stderr);
    
    // Register interface name for namespace support
    // Debug: fprintf(stderr, "[BIND] About to register interface name: %s\n", iface); fflush(stderr);
    register_interface_name(iface);
    // Debug: fprintf(stderr, "[BIND] Registered interface name: %s\n", iface); fflush(stderr);
    
    // Prefer snake_case for underlying C symbols
    char base[256];
    snprintf(base, sizeof(base), "%s_%s", iface, method_snake && method_snake[0] ? method_snake : method);

    int idx = find_dynamic_function_idx(base);
    if (idx < 0) {
        // Not found — best-effort: also try kyl_<iface>_<method> (some libs may expose this)
        snprintf(base, sizeof(base), "kyl_%s_%s", iface, method);
        idx = find_dynamic_function_idx(base);
    }
    if (idx < 0) {
        // Some libraries register certain functions without the library prefix,
        // e.g., "split" instead of "str_split". Try the raw method name.
        idx = find_dynamic_function_idx(method);
    }

    DynamicFunction* df = (idx >= 0) ? &g_dynamic_functions[idx] : NULL;
    void* func_ptr = df ? df->library_func_ptr : NULL;

    // If not found in pre-registered functions, try dlsym on all loaded libraries
    if (!func_ptr) {
        // Try multiple naming conventions
        const char* try_names[9] = {NULL};  // 8 patterns + NULL terminator
        int name_count = 0;
        
    // Build list of names to try (prefer kyl_ wrappers first)
    char kyl_iface_method[256], iface_method[256], kyl_method[256];
    char kyl_iface_method_snake[256], iface_method_snake[256], kyl_method_snake[256];
    snprintf(kyl_iface_method, sizeof(kyl_iface_method), "kyl_%s_%s", iface, method);
    snprintf(iface_method, sizeof(iface_method), "%s_%s", iface, method);
    snprintf(kyl_method, sizeof(kyl_method), "kyl_%s", method);
    // Snake variants (for when method was provided in camelCase)
    const char* m_sn = (method_snake && method_snake[0]) ? method_snake : method;
    snprintf(kyl_iface_method_snake, sizeof(kyl_iface_method_snake), "kyl_%s_%s", iface, m_sn);
    snprintf(iface_method_snake, sizeof(iface_method_snake), "%s_%s", iface, m_sn);
    snprintf(kyl_method_snake, sizeof(kyl_method_snake), "kyl_%s", m_sn);
        
    // Try kyl-prefixed versions FIRST to avoid conflicts with libc (e.g., sleep, time, etc.)
    try_names[name_count++] = kyl_iface_method;  // kyl_file_readText
    try_names[name_count++] = kyl_method;  // kyl_readText  
    try_names[name_count++] = kyl_iface_method_snake;  // kyl_file_read_text
    try_names[name_count++] = kyl_method_snake;  // kyl_read_text
    // Then try unprefixed versions
    try_names[name_count++] = iface_method;  // file_readText
    try_names[name_count++] = method;  // readText
    try_names[name_count++] = iface_method_snake;  // file_read_text
    try_names[name_count++] = m_sn;  // read_text
        
        // Search all loaded libraries
        for (int lib_idx = 0; lib_idx < g_library_registry.library_count && !func_ptr; lib_idx++) {
            SharedLibrary* lib = &g_library_registry.libraries[lib_idx];
            if (!lib->is_loaded || !lib->handle) continue;
            
            // Try each naming convention
            for (int name_idx = 0; try_names[name_idx] && !func_ptr; name_idx++) {
                func_ptr = dlsym(lib->handle, try_names[name_idx]);
                if (func_ptr) {
                    // Found it! Register as dynamic function for future calls
                    fprintf(stderr, "[BIND] Found %s.%s as '%s' at address %p\n", 
                             iface, method, try_names[name_idx], func_ptr);
                    fflush(stderr);
                    LOG_INFO("bind_interface_method: discovered %s.%s as %s via dlsym()", 
                             iface, method, try_names[name_idx]);
                    
                    // For EXPORTED interfaces: register ONLY with qualified name (interface.method)
                    // This prevents direct calls via bare C function names
                    // For non-exported: register with discovered name (allows bare access for backward compat)
                    char qualified[256];
                    snprintf(qualified, sizeof(qualified), "%s.%s", iface, method);
                    const char* register_name = is_exported ? qualified : try_names[name_idx];
                    // Debug: fprintf(stderr, "[REGISTER] is_exported=%d, registering as: %s (qualified=%s, bare=%s)\n", 
                    //        is_exported, register_name, qualified, try_names[name_idx]); fflush(stderr);
                    
                    register_dynamic_function(register_name, func_ptr, FUNC_SIG_VALUE_ARGS);
                    idx = find_dynamic_function_idx(register_name);
                    if (idx >= 0) {
                        df = &g_dynamic_functions[idx];
                    }
                    strncpy(base, register_name, sizeof(base) - 1);
                    base[sizeof(base) - 1] = '\0';
                    break;
                }
            }
        }
        
        if (!func_ptr) {
            LOG_WARNING("bind_interface_method: underlying function not found for %s.%s (tried dlsym on all loaded libraries)", iface, method);
            Value r = {VALUE_BOOL};
            r.as.boolean = false;
            return r;
        }
    }

    if (!df) {
        LOG_WARNING("bind_interface_method: underlying function not found for %s.%s", iface, method);
        Value r = {VALUE_BOOL};
        r.as.boolean = false;
        return r;
    }

    // Prepare alias binding metadata
    char* target_dup = strdup(df->name);
    // Collect param specs if provided
    int param_count = 0;
    char** param_specs = NULL;
    if (param_arr.type == VALUE_ARRAY && param_arr.as.array.count > 0) {
        param_count = param_arr.as.array.count;
        param_specs = (char**)malloc(sizeof(char*) * param_count);
        for (int i = 0; i < param_count; i++) {
            Value v = param_arr.as.array.values[i];
            if (v.type == VALUE_STRING) {
                param_specs[i] = strdup(v.as.string);
            } else {
                param_specs[i] = strdup("");
            }
        }
    }

    // use file-scope helper add_alias_binding_entry

    // Aliases
    // If is_exported=true: ONLY create QUALIFIED aliases (interface.method) - no bare method names
    // If is_exported=false: Create both qualified AND bare aliases for backward compatibility
    
    // Primary qualified alias: interface.method (ALWAYS created)
    char dotted[256]; snprintf(dotted, sizeof(dotted), "%s.%s", iface, method);
    // Debug: fprintf(stderr, "[ALIAS] Creating qualified alias: %s → %s\n", dotted, target_dup); fflush(stderr);
    add_alias_if_missing(dotted, target_dup, df, param_count, param_specs, return_spec);
    
    if (!is_exported) {
        // Create bare method name alias for non-exported interfaces (backward compatibility)
        // BUT: Don't create circular aliases (alias → itself)
        if (strcmp(method, target_dup) != 0) {
            // Debug: fprintf(stderr, "[ALIAS] Creating bare alias: %s → %s (is_exported=false)\n", method, target_dup); fflush(stderr);
            add_alias_if_missing(method, target_dup, df, param_count, param_specs, return_spec);
        }
        
        // Also register alternate casing bare aliases
        if (method_snake && strcmp(method_snake, method) != 0 && strcmp(method_snake, target_dup) != 0) {
            add_alias_if_missing(method_snake, target_dup, df, param_count, param_specs, return_spec);
        }
        if (method_camel && strcmp(method_camel, method) != 0 && strcmp(method_camel, target_dup) != 0) {
            add_alias_if_missing(method_camel, target_dup, df, param_count, param_specs, return_spec);
        }
    } else {
        // Debug: fprintf(stderr, "[ALIAS] SKIPPING bare alias for %s (is_exported=true)\n", method); fflush(stderr);
    }
    
    // Also register alternate casing qualified aliases (always, regardless of export status)
    if (method_snake && strcmp(method_snake, method) != 0) {
        char d2[256]; snprintf(d2, sizeof(d2), "%s.%s", iface, method_snake);
        add_alias_if_missing(d2, target_dup, df, param_count, param_specs, return_spec);
    }
    if (method_camel && strcmp(method_camel, method) != 0) {
        char d3[256]; snprintf(d3, sizeof(d3), "%s.%s", iface, method_camel);
        add_alias_if_missing(d3, target_dup, df, param_count, param_specs, return_spec);
    }

    // Optional extra aliases: either a single string or an array of strings
    // Aliases are always SECOND-TO-LAST (before is_exported)
    // For EXPORTED interfaces: only create qualified aliases, NOT bare aliases
    if (arg_count >= 5) {
        Value aliases = args[arg_count - 2];
        if (aliases.type == VALUE_STRING) {
            if (!is_exported) {
                // Unqualified alias (only for non-exported)
                // Debug: fprintf(stderr, "[ALIAS] Creating unqualified alias from @alias: %s\n", aliases.as.string); fflush(stderr);
                add_alias_binding_entry(aliases.as.string, target_dup, df, param_count, param_specs, return_spec);
            } else {
                // Debug: fprintf(stderr, "[ALIAS] SKIPPING unqualified alias from @alias: %s (is_exported=true)\n", aliases.as.string); fflush(stderr);
            }
            // Dotted alias using iface (always create)
            char d2[256]; snprintf(d2, sizeof(d2), "%s.%s", iface, aliases.as.string);
            add_alias_binding_entry(d2, target_dup, df, param_count, param_specs, return_spec);
        } else if (aliases.type == VALUE_ARRAY) {
            for (int i = 0; i < aliases.as.array.count; i++) {
                Value v = aliases.as.array.values[i];
                if (v.type != VALUE_STRING) continue;
                if (!is_exported) {
                    // Debug: fprintf(stderr, "[ALIAS] Creating unqualified alias from @alias array[%d]: %s\n", i, v.as.string); fflush(stderr);
                    add_alias_binding_entry(v.as.string, target_dup, df, param_count, param_specs, return_spec);
                } else {
                    // Debug: fprintf(stderr, "[ALIAS] SKIPPING unqualified alias from @alias array[%d]: %s (is_exported=true)\n", i, v.as.string); fflush(stderr);
                }
                char d3[256]; snprintf(d3, sizeof(d3), "%s.%s", iface, v.as.string);
                add_alias_binding_entry(d3, target_dup, df, param_count, param_specs, return_spec);
            }
        }
    }

    // Free local param_specs array (bindings have their own copies)
    if (param_specs) {
        for (int i = 0; i < param_count; i++) free(param_specs[i]);
        free(param_specs);
    }
    free(target_dup);
    if (method_snake) free(method_snake);
    if (method_camel) free(method_camel);

    Value r = {VALUE_BOOL};
    r.as.boolean = true;
    return r;
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