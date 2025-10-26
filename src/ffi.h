#ifndef FFI_H
#define FFI_H

#include "ast.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Foreign Function Interface (FFI) System for Kuyil
// Provides JNI-like functionality for loading and calling functions from .so libraries

// FFI Type System - Maps Kuyil types to C types
typedef enum {
    FFI_TYPE_VOID,
    FFI_TYPE_BOOL,
    FFI_TYPE_INT8,
    FFI_TYPE_INT16, 
    FFI_TYPE_INT32,
    FFI_TYPE_INT64,
    FFI_TYPE_UINT8,
    FFI_TYPE_UINT16,
    FFI_TYPE_UINT32,
    FFI_TYPE_UINT64,
    FFI_TYPE_FLOAT,
    FFI_TYPE_DOUBLE,
    FFI_TYPE_STRING,
    FFI_TYPE_POINTER,
    FFI_TYPE_STRUCT,
    FFI_TYPE_ARRAY
} FFIType;

// Function parameter definition
typedef struct {
    char* name;
    FFIType type;
    size_t size;        // For structs and arrays
    bool is_output;     // true if parameter is output/reference
    void* type_info;    // Additional type information for complex types
} FFIParameter;

// Function signature definition
typedef struct {
    char* name;
    FFIType return_type;
    size_t return_size;
    int param_count;
    FFIParameter* parameters;
    void* function_ptr;
    char* library_name;
} FFIFunction;

// Loaded library information
typedef struct {
    char* name;
    char* path;
    void* handle;       // dlopen handle
    int function_count;
    FFIFunction* functions;
    bool is_loaded;
} FFILibrary;

// Application lifecycle structure (forward declaration)
typedef struct {
    bool (*startup_func)(void);   // Returns true on success
    void (*shutdown_func)(void);  // Always called on exit
    bool startup_called;
    bool shutdown_called;
} FFILifecycle;

// FFI Runtime context
typedef struct {
    int library_count;
    FFILibrary* libraries;
    int max_libraries;
    FFILifecycle lifecycle;       // Application lifecycle management
} FFIContext;

// Core FFI functions
FFIContext* ffi_create_context(void);
void ffi_destroy_context(FFIContext* ctx);

// Library management
FFILibrary* ffi_load_library(FFIContext* ctx, const char* name, const char* path);
void ffi_unload_library(FFIContext* ctx, const char* name);
FFILibrary* ffi_get_library(FFIContext* ctx, const char* name);

// Function registration and calling
FFIFunction* ffi_register_function(FFILibrary* lib, const char* name, 
                                   FFIType return_type, int param_count, 
                                   FFIParameter* parameters);
FFIFunction* ffi_get_function(FFILibrary* lib, const char* name);
Value* ffi_call_function(FFIFunction* func, Value* args, int arg_count);

// Type conversion functions
void* ffi_kuyil_to_c(Value* kuyil_val, FFIType target_type, size_t* size);
Value* ffi_c_to_kuyil(void* c_val, FFIType source_type, size_t size);

// Type checking and validation
bool ffi_validate_parameters(FFIFunction* func, Value* args, int arg_count);
const char* ffi_type_name(FFIType type);
const char* ffi_c_type_name(FFIType type);
size_t ffi_type_size(FFIType type);

// Error handling
typedef enum {
    FFI_ERROR_NONE,
    FFI_ERROR_LIBRARY_NOT_FOUND,
    FFI_ERROR_FUNCTION_NOT_FOUND,
    FFI_ERROR_INVALID_SIGNATURE,
    FFI_ERROR_TYPE_MISMATCH,
    FFI_ERROR_MEMORY_ERROR,
    FFI_ERROR_CALL_FAILED
} FFIError;

FFIError ffi_get_last_error(void);
const char* ffi_error_message(FFIError error);

// Convenience macros for common type definitions
#define FFI_PARAM_IN(name, type) {name, type, 0, false, NULL}
#define FFI_PARAM_OUT(name, type) {name, type, 0, true, NULL}
#define FFI_PARAM_INOUT(name, type) {name, type, 0, true, NULL}

// Standard library integration helpers
FFILibrary* ffi_load_redis_client(FFIContext* ctx, const char* lib_path);
FFILibrary* ffi_load_elasticsearch_client(FFIContext* ctx, const char* lib_path);
FFILibrary* ffi_load_kms_client(FFIContext* ctx, const char* lib_path);

// Memory management helpers
void* ffi_malloc(size_t size);
void ffi_free(void* ptr);
char* ffi_strdup(const char* str);

// Lifecycle management functions
bool ffi_register_startup_function(FFIContext* ctx, const char* lib_name, const char* func_name);
bool ffi_register_shutdown_function(FFIContext* ctx, const char* lib_name, const char* func_name);
bool ffi_call_startup_functions(FFIContext* ctx);
void ffi_call_shutdown_functions(FFIContext* ctx);

// Shared library creation from Kuyil code
// Library metadata and dependency management
typedef struct {
    int major;
    int minor;
    int patch;
    char* pre_release;    // e.g., "alpha.1", "beta.2", "rc.1"
    char* build_info;     // e.g., "20251022.1", "git.abc123"
} KuyilVersion;

typedef struct {
    char* name;                  // Dependency library name
    char* version_requirement;   // Version requirement string (e.g., ">=1.2.0", "^2.1.0")
    bool optional;              // Whether dependency is optional
    char* description;          // What this dependency provides
} KuyilDependency;

typedef struct {
    char* author;         // Library author/maintainer
    char* email;          // Contact email
    char* description;    // Brief library description
    char* long_description; // Detailed description
    char* license;        // License type (MIT, GPL, etc.)
    char* homepage;       // Website/repository URL
    char* documentation;  // Documentation URL
    char** keywords;      // Array of keywords for searchability
    int keyword_count;    // Number of keywords
    char* category;       // Library category (math, string, network, etc.)
} KuyilLibraryInfo;

typedef struct {
    char* function_name;
    char* kuyil_source;
    FFIType return_type;
    int param_count;
    FFIParameter* parameters;
} KuyilExportFunction;

typedef struct {
    char* library_name;
    char* output_path;
    int function_count;
    KuyilExportFunction* functions;
    char* additional_c_code;  // Optional C code to include
    
    // Enhanced metadata
    KuyilVersion version;
    KuyilLibraryInfo info;
    KuyilDependency* dependencies;
    int dependency_count;
    
    // Build configuration
    char* compiler_flags;     // Custom GCC flags
    char* linker_flags;      // Custom linker flags
    char** include_paths;    // Additional include directories
    int include_path_count;
    char** library_paths;    // Additional library directories
    int library_path_count;
    
    // Timestamps and checksums
    time_t created_time;     // When library was created
    char* source_checksum;   // MD5/SHA1 of source files
    char* binary_checksum;   // Checksum of compiled library
} KuyilSharedLibSpec;

// .so creation functions
int ffi_create_shared_library(KuyilSharedLibSpec* spec);
KuyilSharedLibSpec* ffi_create_lib_spec(const char* name, const char* output_path);
void ffi_add_exported_function(KuyilSharedLibSpec* spec, const char* func_name,
                              const char* kuyil_source, FFIType return_type,
                              int param_count, FFIParameter* parameters);
void ffi_free_lib_spec(KuyilSharedLibSpec* spec);

// Function discovery and parsing
typedef struct {
    char* name;
    char* signature;
    int param_count;
    char** param_names;
    FFIType return_type;
    FFIType* param_types;
    char* source_code;
    int line_number;
} KuyilFunctionInfo;

typedef struct {
    char* filename;
    int function_count;
    KuyilFunctionInfo* functions;
    char* library_metadata;
} KuyilSourceAnalysis;

// Automatic function discovery
KuyilSourceAnalysis* ffi_analyze_kuyil_source(const char* source_file);
int ffi_create_smart_shared_library(const char* lib_name, const char* source_file, 
                                   const char* output_path, int flags);
void ffi_free_source_analysis(KuyilSourceAnalysis* analysis);
KuyilSharedLibSpec* ffi_create_lib_spec_from_analysis(const char* lib_name, 
                                                     const char* output_path,
                                                     KuyilSourceAnalysis* analysis);

// Enhanced load/unload with options
typedef enum {
    FFI_LOAD_LAZY = 1,       // Load symbols on first use (RTLD_LAZY)
    FFI_LOAD_NOW = 2,        // Load all symbols immediately (RTLD_NOW)
    FFI_LOAD_GLOBAL = 4,     // Make symbols globally available (RTLD_GLOBAL)
    FFI_LOAD_LOCAL = 8       // Keep symbols local (RTLD_LOCAL)
} FFILoadFlags;

FFILibrary* ffi_load_library_ex(FFIContext* ctx, const char* name, 
                               const char* path, FFILoadFlags flags);
bool ffi_unload_library_ex(FFIContext* ctx, const char* name, bool force);
void ffi_list_loaded_libraries(FFIContext* ctx);
bool ffi_is_library_loaded(FFIContext* ctx, const char* name);

// Enhanced library metadata management
KuyilVersion* ffi_create_version(int major, int minor, int patch, const char* pre_release);
void ffi_free_version(KuyilVersion* version);
int ffi_compare_versions(const KuyilVersion* v1, const KuyilVersion* v2);
bool ffi_version_satisfies(const KuyilVersion* version, const char* requirement);
char* ffi_version_to_string(const KuyilVersion* version);
KuyilVersion ffi_parse_version_string(const char* version_str);

// Library info and dependency management 
KuyilLibraryInfo* ffi_create_library_info(const char* author, const char* description,
                                         const char* license, const char* homepage);
void ffi_free_library_info(KuyilLibraryInfo* info);
KuyilDependency* ffi_create_dependency(const char* name, const char* version_requirement, bool optional);
void ffi_free_dependency(KuyilDependency* dependency);

// Metadata management
void ffi_set_library_metadata(KuyilSharedLibSpec* library, KuyilVersion* version, 
                             KuyilLibraryInfo* info, KuyilDependency* dependencies, int dep_count);

void ffi_set_library_info(KuyilSharedLibSpec* spec, const char* author, const char* description, 
                         const char* license, const char* homepage);
void ffi_add_dependency(KuyilSharedLibSpec* spec, const char* name, KuyilVersion min_version, 
                       KuyilVersion max_version, bool is_optional);
void ffi_add_keyword(KuyilSharedLibSpec* spec, const char* keyword);
void ffi_set_compiler_flags(KuyilSharedLibSpec* spec, const char* flags);

// Library registry and dependency resolution
typedef struct {
    char* name;
    char* registry_path;
    int library_count;
    int max_libraries;
    KuyilSharedLibSpec** libraries;
} KuyilLibraryRegistry;

// Registry management functions 
KuyilLibraryRegistry* ffi_create_registry(const char* registry_path);
KuyilLibraryRegistry* ffi_create_library_registry(void);
void ffi_register_library(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* spec);
bool ffi_registry_add_library(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* library);
KuyilSharedLibSpec* ffi_find_library(KuyilLibraryRegistry* registry, const char* name, KuyilVersion* version);
KuyilSharedLibSpec* ffi_registry_find_library(KuyilLibraryRegistry* registry, const char* name);
bool ffi_registry_remove_library(KuyilLibraryRegistry* registry, const char* name);
void ffi_registry_list_libraries(KuyilLibraryRegistry* registry);
bool ffi_check_dependencies(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* spec);
void ffi_resolve_dependencies(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* spec);
void ffi_destroy_registry(KuyilLibraryRegistry* registry);
void ffi_free_library_registry(KuyilLibraryRegistry* registry);

// Enhanced library creation with metadata
int ffi_create_library_with_metadata(KuyilSharedLibSpec* spec);
void ffi_save_library_metadata(KuyilSharedLibSpec* spec, const char* metadata_file);
KuyilSharedLibSpec* ffi_load_library_metadata(const char* metadata_file);

#endif // FFI_H