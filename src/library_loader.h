#ifndef LIBRARY_LOADER_H
#define LIBRARY_LOADER_H

#include "vm.h"

#ifdef _WIN32
    #include <windows.h>
    #define RTLD_LAZY 0
    #define dlopen(name, flags) LoadLibraryA(name)
    #define dlsym(handle, name) GetProcAddress((HMODULE)handle, name)
    #define dlclose(handle) FreeLibrary((HMODULE)handle)
    #define dlerror() "Windows DLL error"
    typedef HMODULE dl_handle_t;
#else
    #include <dlfcn.h>
    typedef void* dl_handle_t;
#endif

#define MAX_LIBRARIES 32
#define MAX_FUNCTIONS_PER_LIBRARY 64
#define MAX_NAME_LENGTH 128
#define MAX_PATH_LENGTH 512

// Function signature types
typedef enum {
    FUNC_SIG_VOID_VOID,         // void func(void)
    FUNC_SIG_INT_VOID,          // int func(void)
    FUNC_SIG_PTR_STRING,        // void* func(const char*)
    FUNC_SIG_PTR_STRING_PTR,    // void* func(const char*, void*)
    FUNC_SIG_INT_PTR_STRING,    // int func(void*, const char*)
    FUNC_SIG_INT_PTR,           // int func(void*)
    FUNC_SIG_VOID_PTR,          // void func(void*)
    FUNC_SIG_PTR_VOID,          // void* func(void)
    FUNC_SIG_VALUE_ARGS         // Value func(int, Value*)
} FunctionSignature;

// Library function definition
typedef struct {
    char name[MAX_NAME_LENGTH];
    char symbol_name[MAX_NAME_LENGTH];
    FunctionSignature signature;
    void* function_ptr;
    bool is_loaded;
} LibraryFunction;

// Library definition
typedef struct {
    char name[MAX_NAME_LENGTH];
    char path[MAX_PATH_LENGTH];
    void* handle;
    bool is_loaded;
    bool is_optional;
    int function_count;
    LibraryFunction functions[MAX_FUNCTIONS_PER_LIBRARY];
} SharedLibrary;

// Library registry
typedef struct {
    int library_count;
    SharedLibrary libraries[MAX_LIBRARIES];
    bool initialized;
} LibraryRegistry;

// Global registry
extern LibraryRegistry g_library_registry;

// Library loader functions
bool library_loader_init(void);
bool load_library_config(const char* config_file);
bool load_library(const char* library_name);
bool load_all_libraries(void);
void unload_all_libraries(void);
void* get_library_function(const char* library_name, const char* function_name);
bool register_library_functions(VM* vm);
void library_loader_cleanup(void);

// Optional: VM can provide a function pointer for libraries that accept a Kuyil caller bridge
void library_loader_set_kuyil_caller(void* callback_ptr);

// Library-specific function loaders
void load_webview_functions(SharedLibrary* lib);
void load_system_functions(SharedLibrary* lib);
void load_crypto_functions(SharedLibrary* lib);
void load_compression_functions(SharedLibrary* lib);
void load_sqlite_functions(SharedLibrary* lib);
void load_http_functions(SharedLibrary* lib);
void load_math_functions(SharedLibrary* lib);
void load_str_functions(SharedLibrary* lib);
void load_datetime_functions(SharedLibrary* lib);
void load_rpc_functions(SharedLibrary* lib);
void load_fileio_functions(SharedLibrary* lib);
void load_generic_functions(SharedLibrary* lib, const char* functions[][3], const char* category);

// Native function wrappers for different signatures
Value native_library_call_void_void(int arg_count, Value* args, void* func_ptr);
Value native_library_call_int_void(int arg_count, Value* args, void* func_ptr);
Value native_library_call_ptr_string(int arg_count, Value* args, void* func_ptr);
Value native_library_call_ptr_string_ptr(int arg_count, Value* args, void* func_ptr);
Value native_library_call_int_ptr_string(int arg_count, Value* args, void* func_ptr);
Value native_library_call_int_ptr(int arg_count, Value* args, void* func_ptr);
Value native_library_call_void_ptr(int arg_count, Value* args, void* func_ptr);

#endif