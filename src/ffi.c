#define _GNU_SOURCE
#include "ffi.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

// Global FFI error state
static FFIError g_last_error = FFI_ERROR_NONE;

// FFI Context Management

FFIContext* ffi_create_context(void) {
    kuyil_log_debug("Creating FFI context");
    
    FFIContext* ctx = malloc(sizeof(FFIContext));
    if (!ctx) {
        g_last_error = FFI_ERROR_MEMORY_ERROR;
        kuyil_log_error("Failed to allocate FFI context");
        return NULL;
    }
    
    ctx->library_count = 0;
    ctx->max_libraries = 16;  // Start with space for 16 libraries
    ctx->libraries = malloc(ctx->max_libraries * sizeof(FFILibrary));
    
    if (!ctx->libraries) {
        free(ctx);
        g_last_error = FFI_ERROR_MEMORY_ERROR;
        kuyil_log_error("Failed to allocate FFI libraries array");
        return NULL;
    }
    
    // Initialize lifecycle structure
    ctx->lifecycle.startup_func = NULL;
    ctx->lifecycle.shutdown_func = NULL;
    ctx->lifecycle.startup_called = false;
    ctx->lifecycle.shutdown_called = false;
    
    kuyil_log_info("FFI context created successfully");
    return ctx;
}

void ffi_destroy_context(FFIContext* ctx) {
    if (!ctx) return;
    
    kuyil_log_debug("Destroying FFI context with %d libraries", ctx->library_count);
    
    // Unload all libraries
    for (int i = 0; i < ctx->library_count; i++) {
        FFILibrary* lib = &ctx->libraries[i];
        if (lib->is_loaded && lib->handle) {
            kuyil_log_debug("Unloading library: %s", lib->name);
            dlclose(lib->handle);
        }
        
        // Free library resources
        if (lib->name) free(lib->name);
        if (lib->path) free(lib->path);
        if (lib->functions) {
            for (int j = 0; j < lib->function_count; j++) {
                FFIFunction* func = &lib->functions[j];
                if (func->name) free(func->name);
                if (func->library_name) free(func->library_name);
                if (func->parameters) {
                    for (int k = 0; k < func->param_count; k++) {
                        if (func->parameters[k].name) free(func->parameters[k].name);
                        if (func->parameters[k].type_info) free(func->parameters[k].type_info);
                    }
                    free(func->parameters);
                }
            }
            free(lib->functions);
        }
    }
    
    free(ctx->libraries);
    free(ctx);
    
    kuyil_log_info("FFI context destroyed");
}

// Library Management

FFILibrary* ffi_load_library(FFIContext* ctx, const char* name, const char* path) {
    if (!ctx || !name || !path) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_error("Invalid parameters for ffi_load_library");
        return NULL;
    }
    
    kuyil_log_info("Loading FFI library: %s from %s", name, path);
    
    // Check if library already loaded
    for (int i = 0; i < ctx->library_count; i++) {
        if (strcmp(ctx->libraries[i].name, name) == 0) {
            kuyil_log_warning("Library %s already loaded", name);
            return &ctx->libraries[i];
        }
    }
    
    // Expand libraries array if needed
    if (ctx->library_count >= ctx->max_libraries) {
        ctx->max_libraries *= 2;
        ctx->libraries = realloc(ctx->libraries, ctx->max_libraries * sizeof(FFILibrary));
        if (!ctx->libraries) {
            g_last_error = FFI_ERROR_MEMORY_ERROR;
            kuyil_log_error("Failed to expand libraries array");
            return NULL;
        }
    }
    
    // Load the shared library
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_error("Failed to load library %s: %s", path, dlerror());
        return NULL;
    }
    
    // Initialize library structure
    FFILibrary* lib = &ctx->libraries[ctx->library_count];
    lib->name = strdup(name);
    lib->path = strdup(path);
    lib->handle = handle;
    lib->function_count = 0;
    lib->functions = NULL;
    lib->is_loaded = true;
    
    ctx->library_count++;
    
    kuyil_log_info("Successfully loaded library: %s", name);
    g_last_error = FFI_ERROR_NONE;
    return lib;
}

void ffi_unload_library(FFIContext* ctx, const char* name) {
    if (!ctx || !name) return;
    
    kuyil_log_debug("Unloading FFI library: %s", name);
    
    for (int i = 0; i < ctx->library_count; i++) {
        FFILibrary* lib = &ctx->libraries[i];
        if (strcmp(lib->name, name) == 0) {
            if (lib->is_loaded && lib->handle) {
                dlclose(lib->handle);
                lib->is_loaded = false;
                kuyil_log_info("Unloaded library: %s", name);
            }
            return;
        }
    }
    
    kuyil_log_warning("Library not found for unloading: %s", name);
}

FFILibrary* ffi_get_library(FFIContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;
    
    for (int i = 0; i < ctx->library_count; i++) {
        if (strcmp(ctx->libraries[i].name, name) == 0) {
            return &ctx->libraries[i];
        }
    }
    
    return NULL;
}

// Function Registration and Management

FFIFunction* ffi_register_function(FFILibrary* lib, const char* name, 
                                   FFIType return_type, int param_count, 
                                   FFIParameter* parameters) {
    if (!lib || !name) {
        g_last_error = FFI_ERROR_INVALID_SIGNATURE;
        kuyil_log_error("Invalid parameters for ffi_register_function");
        return NULL;
    }
    
    kuyil_log_debug("Registering FFI function: %s in library %s", name, lib->name);
    
    // Look up the function symbol in the loaded library
    void* func_ptr = dlsym(lib->handle, name);
    if (!func_ptr) {
        g_last_error = FFI_ERROR_FUNCTION_NOT_FOUND;
        kuyil_log_error("Function not found in library: %s (%s)", name, dlerror());
        return NULL;
    }
    
    // Expand functions array if needed
    if (lib->function_count == 0) {
        lib->functions = malloc(sizeof(FFIFunction));
    } else {
        lib->functions = realloc(lib->functions, (lib->function_count + 1) * sizeof(FFIFunction));
    }
    
    if (!lib->functions) {
        g_last_error = FFI_ERROR_MEMORY_ERROR;
        kuyil_log_error("Failed to allocate memory for function");
        return NULL;
    }
    
    // Initialize function structure
    FFIFunction* func = &lib->functions[lib->function_count];
    func->name = strdup(name);
    func->return_type = return_type;
    func->return_size = ffi_type_size(return_type);
    func->param_count = param_count;
    func->function_ptr = func_ptr;
    func->library_name = strdup(lib->name);
    
    // Copy parameters if provided
    if (param_count > 0 && parameters) {
        func->parameters = malloc(param_count * sizeof(FFIParameter));
        for (int i = 0; i < param_count; i++) {
            func->parameters[i] = parameters[i];
            if (parameters[i].name) {
                func->parameters[i].name = strdup(parameters[i].name);
            }
            // Deep copy type_info if needed
            if (parameters[i].type_info) {
                // For now, just copy the pointer - implement deep copy later
                func->parameters[i].type_info = parameters[i].type_info;
            }
        }
    } else {
        func->parameters = NULL;
    }
    
    lib->function_count++;
    
    kuyil_log_info("Registered function: %s (%s)", name, ffi_type_name(return_type));
    g_last_error = FFI_ERROR_NONE;
    return func;
}

FFIFunction* ffi_get_function(FFILibrary* lib, const char* name) {
    if (!lib || !name) return NULL;
    
    for (int i = 0; i < lib->function_count; i++) {
        if (strcmp(lib->functions[i].name, name) == 0) {
            return &lib->functions[i];
        }
    }
    
    return NULL;
}

// Type System

const char* ffi_type_name(FFIType type) {
    switch (type) {
        case FFI_TYPE_VOID: return "void";
        case FFI_TYPE_BOOL: return "bool";
        case FFI_TYPE_INT8: return "int8";
        case FFI_TYPE_INT16: return "int16";
        case FFI_TYPE_INT32: return "int32";
        case FFI_TYPE_INT64: return "int64";
        case FFI_TYPE_UINT8: return "uint8";
        case FFI_TYPE_UINT16: return "uint16";
        case FFI_TYPE_UINT32: return "uint32";
        case FFI_TYPE_UINT64: return "uint64";
        case FFI_TYPE_FLOAT: return "float";
        case FFI_TYPE_DOUBLE: return "double";
        case FFI_TYPE_FLOAT16: return "float16";
        case FFI_TYPE_FLOAT32: return "float32";
        case FFI_TYPE_FLOAT64: return "float64";
        case FFI_TYPE_STRING: return "string";
        case FFI_TYPE_POINTER: return "pointer";
        case FFI_TYPE_STRUCT: return "struct";
        case FFI_TYPE_ARRAY: return "array";
        default: return "unknown";
    }
}

const char* ffi_c_type_name(FFIType type) {
    switch (type) {
        case FFI_TYPE_VOID: return "void";
        case FFI_TYPE_BOOL: return "bool";
        case FFI_TYPE_INT8: return "int8_t";
        case FFI_TYPE_INT16: return "int16_t";
        case FFI_TYPE_INT32: return "int32_t";
        case FFI_TYPE_INT64: return "int64_t";
        case FFI_TYPE_UINT8: return "uint8_t";
        case FFI_TYPE_UINT16: return "uint16_t";
        case FFI_TYPE_UINT32: return "uint32_t";
        case FFI_TYPE_UINT64: return "uint64_t";
        case FFI_TYPE_FLOAT: return "float";      // alias float32
        case FFI_TYPE_DOUBLE: return "double";    // alias float64
        case FFI_TYPE_FLOAT16: return "_Float16"; // if unsupported, treat as 16-bit storage
        case FFI_TYPE_FLOAT32: return "float";
        case FFI_TYPE_FLOAT64: return "double";
        case FFI_TYPE_STRING: return "char*";
        case FFI_TYPE_POINTER: return "void*";
        case FFI_TYPE_STRUCT: return "struct";
        case FFI_TYPE_ARRAY: return "array";
        default: return "void";
    }
}

size_t ffi_type_size(FFIType type) {
    switch (type) {
        case FFI_TYPE_VOID: return 0;
        case FFI_TYPE_BOOL: return sizeof(bool);
        case FFI_TYPE_INT8: return sizeof(int8_t);
        case FFI_TYPE_INT16: return sizeof(int16_t);
        case FFI_TYPE_INT32: return sizeof(int32_t);
        case FFI_TYPE_INT64: return sizeof(int64_t);
        case FFI_TYPE_UINT8: return sizeof(uint8_t);
        case FFI_TYPE_UINT16: return sizeof(uint16_t);
        case FFI_TYPE_UINT32: return sizeof(uint32_t);
        case FFI_TYPE_UINT64: return sizeof(uint64_t);
        case FFI_TYPE_FLOAT: return sizeof(float);
        case FFI_TYPE_DOUBLE: return sizeof(double);
        case FFI_TYPE_FLOAT16: return 2;
        case FFI_TYPE_FLOAT32: return 4;
        case FFI_TYPE_FLOAT64: return 8;
        case FFI_TYPE_STRING: return sizeof(char*);
        case FFI_TYPE_POINTER: return sizeof(void*);
        case FFI_TYPE_STRUCT: return 0;  // Variable size
        case FFI_TYPE_ARRAY: return 0;   // Variable size
        default: return 0;
    }
}

FFIType ffi_type_from_string(const char* s) {
    if (!s) return FFI_TYPE_VOID;
    // normalize lowercase
    char buf[64]; size_t n = strlen(s);
    if (n >= sizeof(buf)) n = sizeof(buf)-1; 
    for (size_t i = 0; i < n; i++) { char c = s[i]; buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
    buf[n] = '\0';
    if (strcmp(buf, "void") == 0) return FFI_TYPE_VOID;
    if (strcmp(buf, "bool") == 0 || strcmp(buf, "boolean") == 0) return FFI_TYPE_BOOL;
    if (strcmp(buf, "byte") == 0 || strcmp(buf, "uint8") == 0) return FFI_TYPE_UINT8;
    if (strcmp(buf, "int8") == 0) return FFI_TYPE_INT8;
    if (strcmp(buf, "int16") == 0) return FFI_TYPE_INT16;
    if (strcmp(buf, "int32") == 0 || strcmp(buf, "int") == 0) return FFI_TYPE_INT32;
    if (strcmp(buf, "int64") == 0 || strcmp(buf, "long") == 0) return FFI_TYPE_INT64;
    if (strcmp(buf, "uint16") == 0) return FFI_TYPE_UINT16;
    if (strcmp(buf, "uint32") == 0) return FFI_TYPE_UINT32;
    if (strcmp(buf, "uint64") == 0) return FFI_TYPE_UINT64;
    if (strcmp(buf, "float16") == 0) return FFI_TYPE_FLOAT16;
    if (strcmp(buf, "float32") == 0 || strcmp(buf, "float") == 0) return FFI_TYPE_FLOAT32;
    if (strcmp(buf, "float64") == 0 || strcmp(buf, "double") == 0) return FFI_TYPE_FLOAT64;
    if (strcmp(buf, "string") == 0) return FFI_TYPE_STRING;
    if (strcmp(buf, "pointer") == 0 || strcmp(buf, "ptr") == 0) return FFI_TYPE_POINTER;
    if (strcmp(buf, "struct") == 0) return FFI_TYPE_STRUCT;
    if (strcmp(buf, "array") == 0) return FFI_TYPE_ARRAY;
    return FFI_TYPE_POINTER; // default fallback
}

// Error Handling

FFIError ffi_get_last_error(void) {
    return g_last_error;
}

const char* ffi_error_message(FFIError error) {
    switch (error) {
        case FFI_ERROR_NONE: return "No error";
        case FFI_ERROR_LIBRARY_NOT_FOUND: return "Library not found";
        case FFI_ERROR_FUNCTION_NOT_FOUND: return "Function not found";
        case FFI_ERROR_INVALID_SIGNATURE: return "Invalid function signature";
        case FFI_ERROR_TYPE_MISMATCH: return "Type mismatch";
        case FFI_ERROR_MEMORY_ERROR: return "Memory allocation error";
        case FFI_ERROR_CALL_FAILED: return "Function call failed";
        default: return "Unknown error";
    }
}

// Memory Management Helpers

void* ffi_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        g_last_error = FFI_ERROR_MEMORY_ERROR;
        kuyil_log_error("FFI memory allocation failed: %zu bytes", size);
    }
    return ptr;
}

void ffi_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

char* ffi_strdup(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* copy = ffi_malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

// Type Conversion System

void* ffi_kuyil_to_c(Value* kuyil_val, FFIType target_type, size_t* size) {
    if (!kuyil_val) {
        g_last_error = FFI_ERROR_TYPE_MISMATCH;
        return NULL;
    }
    
    kuyil_log_debug("Converting Kuyil value to C type: %s", ffi_type_name(target_type));
    
    void* result = NULL;
    *size = 0;
    
    switch (target_type) {
        case FFI_TYPE_VOID:
            result = NULL;
            *size = 0;
            break;
            
        case FFI_TYPE_BOOL: {
            bool* val = ffi_malloc(sizeof(bool));
            if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean;
            } else if (kuyil_val->type == VALUE_NUMBER) {
                *val = (kuyil_val->as.number != 0.0);
            } else if (kuyil_val->type == VALUE_NIL) {
                *val = false;
            } else {
                *val = true; // Non-nil values are truthy
            }
            result = val;
            *size = sizeof(bool);
            break;
        }
        
        case FFI_TYPE_INT8: {
            int8_t* val = ffi_malloc(sizeof(int8_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (int8_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(int8_t); break;
        }
        case FFI_TYPE_INT16: {
            int16_t* val = ffi_malloc(sizeof(int16_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (int16_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(int16_t); break;
        }
        case FFI_TYPE_INT32: {
            int32_t* val = ffi_malloc(sizeof(int32_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (int32_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else {
                g_last_error = FFI_ERROR_TYPE_MISMATCH;
                ffi_free(val);
                return NULL;
            }
            result = val;
            *size = sizeof(int32_t);
            break;
        }
        
        case FFI_TYPE_INT64: {
            int64_t* val = ffi_malloc(sizeof(int64_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (int64_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else {
                g_last_error = FFI_ERROR_TYPE_MISMATCH;
                ffi_free(val);
                return NULL;
            }
            result = val;
            *size = sizeof(int64_t);
            break;
        }
        case FFI_TYPE_UINT8: {
            uint8_t* val = ffi_malloc(sizeof(uint8_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (uint8_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(uint8_t); break;
        }
        case FFI_TYPE_UINT16: {
            uint16_t* val = ffi_malloc(sizeof(uint16_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (uint16_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(uint16_t); break;
        }
        case FFI_TYPE_UINT32: {
            uint32_t* val = ffi_malloc(sizeof(uint32_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (uint32_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(uint32_t); break;
        }
        case FFI_TYPE_UINT64: {
            uint64_t* val = ffi_malloc(sizeof(uint64_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (uint64_t)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1 : 0;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(uint64_t); break;
        }
        
        case FFI_TYPE_DOUBLE:
        case FFI_TYPE_FLOAT64: {
            double* val = ffi_malloc(sizeof(double));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1.0 : 0.0;
            } else {
                g_last_error = FFI_ERROR_TYPE_MISMATCH;
                ffi_free(val);
                return NULL;
            }
            result = val;
            *size = sizeof(double);
            break;
        }
        case FFI_TYPE_FLOAT:
        case FFI_TYPE_FLOAT32: {
            float* val = ffi_malloc(sizeof(float));
            if (kuyil_val->type == VALUE_NUMBER) {
                *val = (float)kuyil_val->as.number;
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = kuyil_val->as.boolean ? 1.0f : 0.0f;
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(float); break;
        }
        case FFI_TYPE_FLOAT16: {
            // Store as 16-bit half precision; approximate by casting to float and truncating bits
            // Note: This is a placeholder; actual half conversion is not implemented
            uint16_t* val = ffi_malloc(sizeof(uint16_t));
            if (kuyil_val->type == VALUE_NUMBER) {
                float f = (float)kuyil_val->as.number;
                // naive quantization: scale not accurate; store zero for simplicity
                (void)f; *val = 0; 
            } else if (kuyil_val->type == VALUE_BOOL) {
                *val = 0; // 0 or 1 not represented precisely here
            } else { g_last_error = FFI_ERROR_TYPE_MISMATCH; ffi_free(val); return NULL; }
            result = val; *size = sizeof(uint16_t); break;
        }
        
        case FFI_TYPE_STRING: {
            char** str_ptr = ffi_malloc(sizeof(char*));
            if (kuyil_val->type == VALUE_STRING) {
                *str_ptr = ffi_strdup(kuyil_val->as.string);
                *size = strlen(*str_ptr) + 1;
            } else if (kuyil_val->type == VALUE_NIL) {
                *str_ptr = NULL;
                *size = 0;
            } else {
                g_last_error = FFI_ERROR_TYPE_MISMATCH;
                ffi_free(str_ptr);
                return NULL;
            }
            result = str_ptr;
            break;
        }
        
        case FFI_TYPE_POINTER: {
            void** ptr = ffi_malloc(sizeof(void*));
            if (kuyil_val->type == VALUE_STRING) {
                // Treat string as pointer to its data
                *ptr = (void*)kuyil_val->as.string;
            } else if (kuyil_val->type == VALUE_NIL) {
                *ptr = NULL;
            } else {
                // For other types, return pointer to the value itself
                *ptr = (void*)kuyil_val;
            }
            result = ptr;
            *size = sizeof(void*);
            break;
        }
        
        default:
            kuyil_log_error("Unsupported FFI type conversion: %s", ffi_type_name(target_type));
            g_last_error = FFI_ERROR_TYPE_MISMATCH;
            return NULL;
    }
    
    g_last_error = FFI_ERROR_NONE;
    return result;
}

Value* ffi_c_to_kuyil(void* c_val, FFIType source_type, size_t size) {
    if (!c_val && source_type != FFI_TYPE_VOID) {
        Value* nil_val = ffi_malloc(sizeof(Value));
        nil_val->type = VALUE_NIL;
        return nil_val;
    }
    
    kuyil_log_debug("Converting C value to Kuyil type: %s", ffi_type_name(source_type));
    
    Value* result = ffi_malloc(sizeof(Value));
    
    switch (source_type) {
        case FFI_TYPE_VOID:
            result->type = VALUE_NIL;
            break;
            
        case FFI_TYPE_BOOL: {
            bool val = *(bool*)c_val;
            result->type = VALUE_BOOL;
            result->as.boolean = val;
            break;
        }
        
        case FFI_TYPE_INT8:
        case FFI_TYPE_INT16:
        case FFI_TYPE_INT32: {
            int32_t val = *(int32_t*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = (double)val;
            break;
        }
        
        case FFI_TYPE_INT64: {
            int64_t val = *(int64_t*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = (double)val;
            break;
        }
        
        case FFI_TYPE_UINT8:
        case FFI_TYPE_UINT16:
        case FFI_TYPE_UINT32: {
            uint32_t val = *(uint32_t*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = (double)val;
            break;
        }
        
        case FFI_TYPE_UINT64: {
            uint64_t val = *(uint64_t*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = (double)val;
            break;
        }
        
        case FFI_TYPE_FLOAT:
        case FFI_TYPE_FLOAT32: {
            float val = *(float*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = (double)val;
            break;
        }
        
        case FFI_TYPE_DOUBLE:
        case FFI_TYPE_FLOAT64: {
            double val = *(double*)c_val;
            result->type = VALUE_NUMBER;
            result->as.number = val;
            break;
        }
        case FFI_TYPE_FLOAT16: {
            // Placeholder: interpret as zero
            result->type = VALUE_NUMBER;
            result->as.number = 0.0;
            break;
        }
        
        case FFI_TYPE_STRING: {
            char* str = *(char**)c_val;
            result->type = VALUE_STRING;
            if (str) {
                result->as.string = ffi_strdup(str);
            } else {
                result->type = VALUE_NIL;
            }
            break;
        }
        
        case FFI_TYPE_POINTER: {
            void* ptr = *(void**)c_val;
            if (ptr) {
                result->type = VALUE_STRING;
                // Convert pointer to hex string representation
                char* hex_str = ffi_malloc(32);
                snprintf(hex_str, 32, "0x%lx", (unsigned long)ptr);
                result->as.string = hex_str;
            } else {
                result->type = VALUE_NIL;
            }
            break;
        }
        
        default:
            kuyil_log_error("Unsupported C to Kuyil type conversion: %s", ffi_type_name(source_type));
            result->type = VALUE_NIL;
            g_last_error = FFI_ERROR_TYPE_MISMATCH;
            break;
    }
    
    return result;
}

// Parameter validation
bool ffi_validate_parameters(FFIFunction* func, Value* args, int arg_count) {
    if (!func) return false;
    
    if (arg_count != func->param_count) {
        kuyil_log_error("Parameter count mismatch: expected %d, got %d", func->param_count, arg_count);
        g_last_error = FFI_ERROR_TYPE_MISMATCH;
        return false;
    }
    
    for (int i = 0; i < arg_count; i++) {
        Value* arg = &args[i];
        FFIParameter* param = &func->parameters[i];
        
        // Basic type compatibility checking
        bool compatible = false;
        
        switch (param->type) {
            case FFI_TYPE_BOOL:
                compatible = (arg->type == VALUE_BOOL || arg->type == VALUE_NUMBER || arg->type == VALUE_NIL);
                break;
            case FFI_TYPE_INT8:
            case FFI_TYPE_INT16:
            case FFI_TYPE_INT32:
            case FFI_TYPE_INT64:
            case FFI_TYPE_UINT8:
            case FFI_TYPE_UINT16:
            case FFI_TYPE_UINT32:
            case FFI_TYPE_UINT64:
            case FFI_TYPE_FLOAT:
            case FFI_TYPE_DOUBLE:
                compatible = (arg->type == VALUE_NUMBER || arg->type == VALUE_BOOL);
                break;
            case FFI_TYPE_STRING:
                compatible = (arg->type == VALUE_STRING || arg->type == VALUE_NIL);
                break;
            case FFI_TYPE_POINTER:
                compatible = true; // Any type can be converted to pointer
                break;
            default:
                compatible = true; // Allow other types for now
                break;
        }
        
        if (!compatible) {
            kuyil_log_error("Type mismatch for parameter %d (%s): expected %s compatible type", 
                          i, param->name ? param->name : "unknown", ffi_type_name(param->type));
            g_last_error = FFI_ERROR_TYPE_MISMATCH;
            return false;
        }
    }
    
    return true;
}

// Function Calling System

Value* ffi_call_function(FFIFunction* func, Value* args, int arg_count) {
    if (!func) {
        g_last_error = FFI_ERROR_FUNCTION_NOT_FOUND;
        kuyil_log_error("FFI function is NULL");
        return NULL;
    }
    
    kuyil_log_debug("Calling FFI function: %s with %d arguments", func->name, arg_count);
    
    // Validate parameters
    if (!ffi_validate_parameters(func, args, arg_count)) {
        return NULL;
    }
    
    // Convert Kuyil arguments to C values
    void** c_args = NULL;
    size_t* arg_sizes = NULL;
    
    if (arg_count > 0) {
        c_args = ffi_malloc(arg_count * sizeof(void*));
        arg_sizes = ffi_malloc(arg_count * sizeof(size_t));
        
        for (int i = 0; i < arg_count; i++) {
            c_args[i] = ffi_kuyil_to_c(&args[i], func->parameters[i].type, &arg_sizes[i]);
            if (!c_args[i] && func->parameters[i].type != FFI_TYPE_VOID) {
                kuyil_log_error("Failed to convert argument %d for function %s", i, func->name);
                
                // Cleanup already converted arguments
                for (int j = 0; j < i; j++) {
                    ffi_free(c_args[j]);
                }
                ffi_free(c_args);
                ffi_free(arg_sizes);
                return NULL;
            }
        }
    }
    
    // Call the function based on parameter count and types
    // This is a simplified implementation - a full implementation would use libffi
    // or assembly code to handle arbitrary function signatures
    
    void* result = NULL;
    
    switch (func->param_count) {
        case 0: {
            typedef void* (*func0_t)(void);
            func0_t f = (func0_t)func->function_ptr;
            result = f();
            break;
        }
        
        case 1: {
            void* arg0 = c_args[0] ? *(void**)c_args[0] : NULL;
            typedef void* (*func1_t)(void*);
            func1_t f = (func1_t)func->function_ptr;
            result = f(arg0);
            break;
        }
        
        case 2: {
            void* arg0 = c_args[0] ? *(void**)c_args[0] : NULL;
            void* arg1 = c_args[1] ? *(void**)c_args[1] : NULL;
            typedef void* (*func2_t)(void*, void*);
            func2_t f = (func2_t)func->function_ptr;
            result = f(arg0, arg1);
            break;
        }
        
        case 3: {
            void* arg0 = c_args[0] ? *(void**)c_args[0] : NULL;
            void* arg1 = c_args[1] ? *(void**)c_args[1] : NULL;
            void* arg2 = c_args[2] ? *(void**)c_args[2] : NULL;
            typedef void* (*func3_t)(void*, void*, void*);
            func3_t f = (func3_t)func->function_ptr;
            result = f(arg0, arg1, arg2);
            break;
        }
        
        default:
            kuyil_log_error("FFI function calls with %d parameters not yet supported", func->param_count);
            g_last_error = FFI_ERROR_CALL_FAILED;
            
            // Cleanup
            if (c_args) {
                for (int i = 0; i < arg_count; i++) {
                    ffi_free(c_args[i]);
                }
                ffi_free(c_args);
            }
            ffi_free(arg_sizes);
            return NULL;
    }
    
    // Convert result back to Kuyil value
    Value* kuyil_result = ffi_c_to_kuyil(&result, func->return_type, func->return_size);
    
    // Cleanup arguments
    if (c_args) {
        for (int i = 0; i < arg_count; i++) {
            ffi_free(c_args[i]);
        }
        ffi_free(c_args);
    }
    ffi_free(arg_sizes);
    
    kuyil_log_info("FFI function %s completed successfully", func->name);
    g_last_error = FFI_ERROR_NONE;
    return kuyil_result;
}

// Standard Library Integration Helpers

FFILibrary* ffi_load_redis_client(FFIContext* ctx, const char* lib_path) {
    kuyil_log_info("Loading Redis client library from: %s", lib_path);
    
    FFILibrary* lib = ffi_load_library(ctx, "redis_client", lib_path);
    if (!lib) return NULL;
    
    // Register common Redis functions
    FFIParameter connect_params[] = {
        FFI_PARAM_IN("host", FFI_TYPE_STRING),
        FFI_PARAM_IN("port", FFI_TYPE_INT32)
    };
    ffi_register_function(lib, "redis_connect", FFI_TYPE_POINTER, 2, connect_params);
    
    FFIParameter set_params[] = {
        FFI_PARAM_IN("connection", FFI_TYPE_POINTER),
        FFI_PARAM_IN("key", FFI_TYPE_STRING),
        FFI_PARAM_IN("value", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "redis_set", FFI_TYPE_INT32, 3, set_params);
    
    FFIParameter get_params[] = {
        FFI_PARAM_IN("connection", FFI_TYPE_POINTER),
        FFI_PARAM_IN("key", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "redis_get", FFI_TYPE_STRING, 2, get_params);
    
    FFIParameter close_params[] = {
        FFI_PARAM_IN("connection", FFI_TYPE_POINTER)
    };
    ffi_register_function(lib, "redis_close", FFI_TYPE_VOID, 1, close_params);
    
    kuyil_log_info("Redis client library loaded with %d functions", lib->function_count);
    return lib;
}

FFILibrary* ffi_load_elasticsearch_client(FFIContext* ctx, const char* lib_path) {
    kuyil_log_info("Loading Elasticsearch client library from: %s", lib_path);
    
    FFILibrary* lib = ffi_load_library(ctx, "elasticsearch_client", lib_path);
    if (!lib) return NULL;
    
    // Register common Elasticsearch functions
    FFIParameter connect_params[] = {
        FFI_PARAM_IN("url", FFI_TYPE_STRING),
        FFI_PARAM_IN("timeout", FFI_TYPE_INT32)
    };
    ffi_register_function(lib, "es_connect", FFI_TYPE_POINTER, 2, connect_params);
    
    FFIParameter index_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER),
        FFI_PARAM_IN("index", FFI_TYPE_STRING),
        FFI_PARAM_IN("type", FFI_TYPE_STRING),
        FFI_PARAM_IN("id", FFI_TYPE_STRING),
        FFI_PARAM_IN("document", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "es_index", FFI_TYPE_INT32, 5, index_params);
    
    FFIParameter search_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER),
        FFI_PARAM_IN("index", FFI_TYPE_STRING),
        FFI_PARAM_IN("query", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "es_search", FFI_TYPE_STRING, 3, search_params);
    
    FFIParameter close_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER)
    };
    ffi_register_function(lib, "es_close", FFI_TYPE_VOID, 1, close_params);
    
    kuyil_log_info("Elasticsearch client library loaded with %d functions", lib->function_count);
    return lib;
}

FFILibrary* ffi_load_kms_client(FFIContext* ctx, const char* lib_path) {
    kuyil_log_info("Loading KMS client library from: %s", lib_path);
    
    FFILibrary* lib = ffi_load_library(ctx, "kms_client", lib_path);
    if (!lib) return NULL;
    
    // Register common KMS functions
    FFIParameter init_params[] = {
        FFI_PARAM_IN("region", FFI_TYPE_STRING),
        FFI_PARAM_IN("access_key", FFI_TYPE_STRING),
        FFI_PARAM_IN("secret_key", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "kms_init", FFI_TYPE_POINTER, 3, init_params);
    
    FFIParameter encrypt_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER),
        FFI_PARAM_IN("key_id", FFI_TYPE_STRING),
        FFI_PARAM_IN("plaintext", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "kms_encrypt", FFI_TYPE_STRING, 3, encrypt_params);
    
    FFIParameter decrypt_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER),
        FFI_PARAM_IN("ciphertext", FFI_TYPE_STRING)
    };
    ffi_register_function(lib, "kms_decrypt", FFI_TYPE_STRING, 2, decrypt_params);
    
    FFIParameter cleanup_params[] = {
        FFI_PARAM_IN("client", FFI_TYPE_POINTER)
    };
    ffi_register_function(lib, "kms_cleanup", FFI_TYPE_VOID, 1, cleanup_params);
    
    kuyil_log_info("KMS client library loaded with %d functions", lib->function_count);
    return lib;
}

// Application lifecycle functions implementation

bool ffi_register_startup_function(FFIContext* ctx, const char* lib_name, const char* func_name) {
    if (!ctx || !lib_name || !func_name) {
        g_last_error = FFI_ERROR_INVALID_SIGNATURE;
        kuyil_log_error("Invalid parameters for startup function registration");
        return false;
    }
    
    // Find the library
    FFILibrary* lib = NULL;
    for (int i = 0; i < ctx->library_count; i++) {
        if (strcmp(ctx->libraries[i].name, lib_name) == 0) {
            lib = &ctx->libraries[i];
            break;
        }
    }
    
    if (!lib || !lib->is_loaded) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_error("Library '%s' not found for startup function registration", lib_name);
        return false;
    }
    
    // Look up the function symbol
    void* func_ptr = dlsym(lib->handle, func_name);
    if (!func_ptr) {
        g_last_error = FFI_ERROR_FUNCTION_NOT_FOUND;
        kuyil_log_error("Startup function '%s' not found in library '%s': %s", func_name, lib_name, dlerror());
        return false;
    }
    
    // Store the startup function
    ctx->lifecycle.startup_func = (bool(*)(void))func_ptr;
    kuyil_log_info("Registered startup function '%s' from library '%s'", func_name, lib_name);
    return true;
}

bool ffi_register_shutdown_function(FFIContext* ctx, const char* lib_name, const char* func_name) {
    if (!ctx || !lib_name || !func_name) {
        g_last_error = FFI_ERROR_INVALID_SIGNATURE;
        kuyil_log_error("Invalid parameters for shutdown function registration");
        return false;
    }
    
    // Find the library
    FFILibrary* lib = NULL;
    for (int i = 0; i < ctx->library_count; i++) {
        if (strcmp(ctx->libraries[i].name, lib_name) == 0) {
            lib = &ctx->libraries[i];
            break;
        }
    }
    
    if (!lib || !lib->is_loaded) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_error("Library '%s' not found for shutdown function registration", lib_name);
        return false;
    }
    
    // Look up the function symbol
    void* func_ptr = dlsym(lib->handle, func_name);
    if (!func_ptr) {
        g_last_error = FFI_ERROR_FUNCTION_NOT_FOUND;
        kuyil_log_error("Shutdown function '%s' not found in library '%s': %s", func_name, lib_name, dlerror());
        return false;
    }
    
    // Store the shutdown function
    ctx->lifecycle.shutdown_func = (void(*)(void))func_ptr;
    kuyil_log_info("Registered shutdown function '%s' from library '%s'", func_name, lib_name);
    return true;
}

bool ffi_call_startup_functions(FFIContext* ctx) {
    if (!ctx) {
        kuyil_log_error("Invalid FFI context for startup functions");
        return false;
    }
    
    if (ctx->lifecycle.startup_called) {
        kuyil_log_warning("Startup functions already called");
        return true;
    }
    
    kuyil_log_info("Calling application startup functions...");
    
    if (ctx->lifecycle.startup_func) {
        kuyil_log_debug("Executing registered startup function");
        bool result = ctx->lifecycle.startup_func();
        if (!result) {
            kuyil_log_error("Startup function failed");
            return false;
        }
        kuyil_log_info("Startup function completed successfully");
    } else {
        kuyil_log_debug("No startup function registered");
    }
    
    ctx->lifecycle.startup_called = true;
    return true;
}

void ffi_call_shutdown_functions(FFIContext* ctx) {
    if (!ctx) {
        kuyil_log_error("Invalid FFI context for shutdown functions");
        return;
    }
    
    if (ctx->lifecycle.shutdown_called) {
        kuyil_log_warning("Shutdown functions already called");
        return;
    }
    
    kuyil_log_info("Calling application shutdown functions...");
    
    if (ctx->lifecycle.shutdown_func) {
        kuyil_log_debug("Executing registered shutdown function");
        ctx->lifecycle.shutdown_func();
        kuyil_log_info("Shutdown function completed");
    } else {
        kuyil_log_debug("No shutdown function registered");
    }
    
    ctx->lifecycle.shutdown_called = true;
}

// Enhanced load/unload functions
FFILibrary* ffi_load_library_ex(FFIContext* ctx, const char* name, const char* path, FFILoadFlags flags) {
    if (!ctx || !name || !path) {
        g_last_error = FFI_ERROR_INVALID_SIGNATURE;
        kuyil_log_error("Invalid parameters for ffi_load_library_ex");
        return NULL;
    }
    
    // Check if library already loaded
    if (ffi_is_library_loaded(ctx, name)) {
        kuyil_log_warning("Library '%s' is already loaded", name);
        return ffi_get_library(ctx, name);
    }
    
    // Convert flags to dlopen flags
#ifndef _WIN32
    int dlopen_flags = RTLD_LAZY; // Default
    if (flags & FFI_LOAD_NOW) dlopen_flags = RTLD_NOW;
    if (flags & FFI_LOAD_GLOBAL) dlopen_flags |= RTLD_GLOBAL;
    if (flags & FFI_LOAD_LOCAL) dlopen_flags |= RTLD_LOCAL;
#else
    int dlopen_flags = 0; // Windows doesn't use these flags
#endif
    
    kuyil_log_info("Loading library '%s' from '%s' with flags: %d", name, path, flags);
    
    // Try to load the library with specified flags
    void* handle = dlopen(path, dlopen_flags);
    if (!handle) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_error("Failed to load library '%s': %s", path, dlerror());
        return NULL;
    }
    
    // Expand library array if needed
    if (ctx->library_count >= ctx->max_libraries) {
        ctx->max_libraries = ctx->max_libraries ? ctx->max_libraries * 2 : 4;
        ctx->libraries = realloc(ctx->libraries, ctx->max_libraries * sizeof(FFILibrary));
        if (!ctx->libraries) {
            g_last_error = FFI_ERROR_MEMORY_ERROR;
            dlclose(handle);
            return NULL;
        }
    }
    
    // Initialize new library
    FFILibrary* lib = &ctx->libraries[ctx->library_count];
    lib->name = ffi_strdup(name);
    lib->path = ffi_strdup(path);
    lib->handle = handle;
    lib->function_count = 0;
    lib->functions = NULL;
    lib->is_loaded = true;
    
    ctx->library_count++;
    
    kuyil_log_info("Successfully loaded library '%s' (handle: %p)", name, handle);
    return lib;
}

bool ffi_unload_library_ex(FFIContext* ctx, const char* name, bool force) {
    if (!ctx || !name) {
        g_last_error = FFI_ERROR_INVALID_SIGNATURE;
        kuyil_log_error("Invalid parameters for ffi_unload_library_ex");
        return false;
    }
    
    // Find the library
    FFILibrary* lib = NULL;
    int lib_index = -1;
    for (int i = 0; i < ctx->library_count; i++) {
        if (strcmp(ctx->libraries[i].name, name) == 0) {
            lib = &ctx->libraries[i];
            lib_index = i;
            break;
        }
    }
    
    if (!lib) {
        g_last_error = FFI_ERROR_LIBRARY_NOT_FOUND;
        kuyil_log_warning("Library '%s' not found for unloading", name);
        return false;
    }
    
    if (!lib->is_loaded && !force) {
        kuyil_log_warning("Library '%s' is not currently loaded", name);
        return true;
    }
    
    kuyil_log_info("Unloading library '%s' (force: %s)", name, force ? "true" : "false");
    
    // Clean up functions
    if (lib->functions) {
        for (int i = 0; i < lib->function_count; i++) {
            free(lib->functions[i].name);
            if (lib->functions[i].parameters) {
                for (int j = 0; j < lib->functions[i].param_count; j++) {
                    free(lib->functions[i].parameters[j].name);
                }
                free(lib->functions[i].parameters);
            }
            free(lib->functions[i].library_name);
        }
        free(lib->functions);
    }
    
    // Close the library handle
    if (lib->handle) {
        if (dlclose(lib->handle) != 0) {
            kuyil_log_error("Error closing library '%s': %s", name, dlerror());
            if (!force) return false;
        }
    }
    
    // Free library metadata
    free(lib->name);
    free(lib->path);
    
    // Remove from array by shifting remaining libraries
    for (int i = lib_index; i < ctx->library_count - 1; i++) {
        ctx->libraries[i] = ctx->libraries[i + 1];
    }
    ctx->library_count--;
    
    kuyil_log_info("Successfully unloaded library '%s'", name);
    return true;
}

void ffi_list_loaded_libraries(FFIContext* ctx) {
    if (!ctx) {
        printf("FFI context is NULL\n");
        return;
    }
    
    printf("Loaded Libraries (%d):\n", ctx->library_count);
    printf("%-20s %-40s %-10s %-10s\n", "Name", "Path", "Handle", "Functions");
    printf("%-20s %-40s %-10s %-10s\n", "----", "----", "------", "---------");
    
    for (int i = 0; i < ctx->library_count; i++) {
        FFILibrary* lib = &ctx->libraries[i];
        printf("%-20s %-40s %p %-10d\n", 
               lib->name ? lib->name : "NULL",
               lib->path ? lib->path : "NULL",
               lib->handle,
               lib->function_count);
    }
}

bool ffi_is_library_loaded(FFIContext* ctx, const char* name) {
    if (!ctx || !name) return false;
    
    for (int i = 0; i < ctx->library_count; i++) {
        if (ctx->libraries[i].name && strcmp(ctx->libraries[i].name, name) == 0) {
            return ctx->libraries[i].is_loaded;
        }
    }
    return false;
}

// Shared library creation functions
KuyilSharedLibSpec* ffi_create_lib_spec(const char* name, const char* output_path) {
    if (!name || !output_path) {
        kuyil_log_error("Invalid parameters for ffi_create_lib_spec");
        return NULL;
    }
    
    KuyilSharedLibSpec* spec = malloc(sizeof(KuyilSharedLibSpec));
    if (!spec) {
        kuyil_log_error("Memory allocation failed for library spec");
        return NULL;
    }
    
    spec->library_name = ffi_strdup(name);
    spec->output_path = ffi_strdup(output_path);
    spec->function_count = 0;
    spec->functions = NULL;
    spec->additional_c_code = NULL;
    
    kuyil_log_info("Created library specification for '%s' -> '%s'", name, output_path);
    return spec;
}

void ffi_add_exported_function(KuyilSharedLibSpec* spec, const char* func_name,
                              const char* kuyil_source, FFIType return_type,
                              int param_count, FFIParameter* parameters) {
    if (!spec || !func_name || !kuyil_source) {
        kuyil_log_error("Invalid parameters for ffi_add_exported_function");
        return;
    }
    
    // Expand functions array
    spec->functions = realloc(spec->functions, 
                             (spec->function_count + 1) * sizeof(KuyilExportFunction));
    if (!spec->functions) {
        kuyil_log_error("Memory allocation failed for exported function");
        return;
    }
    
    KuyilExportFunction* func = &spec->functions[spec->function_count];
    func->function_name = ffi_strdup(func_name);
    func->kuyil_source = ffi_strdup(kuyil_source);
    func->return_type = return_type;
    func->param_count = param_count;
    
    // Copy parameters if provided
    if (param_count > 0 && parameters) {
        func->parameters = malloc(param_count * sizeof(FFIParameter));
        for (int i = 0; i < param_count; i++) {
            func->parameters[i].name = ffi_strdup(parameters[i].name);
            func->parameters[i].type = parameters[i].type;
            func->parameters[i].size = parameters[i].size;
            func->parameters[i].is_output = parameters[i].is_output;
            func->parameters[i].type_info = parameters[i].type_info;
        }
    } else {
        func->parameters = NULL;
    }
    
    spec->function_count++;
    kuyil_log_info("Added exported function '%s' to library spec '%s'", 
                   func_name, spec->library_name);
}

int ffi_create_shared_library(KuyilSharedLibSpec* spec) {
    if (!spec || !spec->library_name || !spec->output_path) {
        kuyil_log_error("Invalid library specification");
        return -1;
    }
    
    kuyil_log_info("Creating shared library '%s' with %d functions", 
                   spec->library_name, spec->function_count);
    
    // Generate C source file
    char temp_c_file[512];
    snprintf(temp_c_file, sizeof(temp_c_file), "/tmp/%s_generated.c", spec->library_name);
    
    FILE* c_file = fopen(temp_c_file, "w");
    if (!c_file) {
        kuyil_log_error("Failed to create temporary C file: %s", temp_c_file);
        return -1;
    }
    
    // Write C file header
    fprintf(c_file, "// Generated C code for Kuyil shared library: %s\n", spec->library_name);
    fprintf(c_file, "#include <stdio.h>\n");
    fprintf(c_file, "#include <stdlib.h>\n");
    fprintf(c_file, "#include <string.h>\n");
    fprintf(c_file, "#include <stdbool.h>\n");
    fprintf(c_file, "#include <stdint.h>\n\n");
    
    // Include additional C code if provided
    if (spec->additional_c_code) {
        fprintf(c_file, "// Additional C code\n");
        fprintf(c_file, "%s\n\n", spec->additional_c_code);
    }
    
    // Generate wrapper functions for each Kuyil function
    for (int i = 0; i < spec->function_count; i++) {
        KuyilExportFunction* func = &spec->functions[i];
        
        fprintf(c_file, "// Exported function: %s\n", func->function_name);
        
        // Function signature
        const char* return_type_str = ffi_c_type_name(func->return_type);
        fprintf(c_file, "%s %s(", return_type_str, func->function_name);
        
        // Parameters
        for (int j = 0; j < func->param_count; j++) {
            if (j > 0) fprintf(c_file, ", ");
            const char* param_type_str = ffi_c_type_name(func->parameters[j].type);
            fprintf(c_file, "%s %s", param_type_str, func->parameters[j].name);
        }
        
        fprintf(c_file, ") {\n");
        
        // TODO: Here we would need to integrate the Kuyil interpreter
        // For now, generate a placeholder that calls printf
        fprintf(c_file, "    printf(\"Called Kuyil function: %s\\n\");\n", func->function_name);
        
        if (func->return_type != FFI_TYPE_VOID) {
            switch (func->return_type) {
                case FFI_TYPE_INT32:
                    fprintf(c_file, "    return 42;\n");
                    break;
                case FFI_TYPE_DOUBLE:
                    fprintf(c_file, "    return 3.14159;\n");
                    break;
                case FFI_TYPE_STRING:
                    fprintf(c_file, "    return \"Hello from %s\";\n", func->function_name);
                    break;
                default:
                    fprintf(c_file, "    return 0;\n");
                    break;
            }
        }
        
        fprintf(c_file, "}\n\n");
    }
    
    fclose(c_file);
    
    // Compile to shared library
    char compile_cmd[1024];
    snprintf(compile_cmd, sizeof(compile_cmd), 
             "gcc -shared -fPIC -o %s %s", spec->output_path, temp_c_file);
    
    kuyil_log_info("Compiling shared library: %s", compile_cmd);
    
    int result = system(compile_cmd);
    
    // Clean up temporary file
    unlink(temp_c_file);
    
    if (result == 0) {
        kuyil_log_info("Successfully created shared library: %s", spec->output_path);
        return 0;
    } else {
        kuyil_log_error("Failed to compile shared library (exit code: %d)", result);
        return result;
    }
}

void ffi_free_lib_spec(KuyilSharedLibSpec* spec) {
    if (!spec) return;
    
    free(spec->library_name);
    free(spec->output_path);
    free(spec->additional_c_code);
    
    if (spec->functions) {
        for (int i = 0; i < spec->function_count; i++) {
            KuyilExportFunction* func = &spec->functions[i];
            free(func->function_name);
            free(func->kuyil_source);
            
            if (func->parameters) {
                for (int j = 0; j < func->param_count; j++) {
                    free(func->parameters[j].name);
                }
                free(func->parameters);
            }
        }
        free(spec->functions);
    }
    
    free(spec);
}

// Automatic function discovery and parsing implementations
KuyilSourceAnalysis* ffi_analyze_kuyil_source(const char* source_file) {
    if (!source_file) {
        kuyil_log_error("Source file path is NULL");
        return NULL;
    }
    
    kuyil_log_info("Analyzing Kuyil source file: %s", source_file);
    
    FILE* file = fopen(source_file, "r");
    if (!file) {
        kuyil_log_error("Failed to open source file: %s", source_file);
        return NULL;
    }
    
    KuyilSourceAnalysis* analysis = malloc(sizeof(KuyilSourceAnalysis));
    if (!analysis) {
        kuyil_log_error("Memory allocation failed for source analysis");
        fclose(file);
        return NULL;
    }
    
    analysis->filename = ffi_strdup(source_file);
    analysis->function_count = 0;
    analysis->functions = NULL;
    analysis->library_metadata = NULL;
    
    char line[1024];
    int line_number = 0;
    bool in_function = false;
    char current_function_name[256] = {0};
    char current_function_body[4096] = {0};
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        
        // Remove trailing newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Skip empty lines and comments
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0' || strncmp(trimmed, "//", 2) == 0) {
            continue;
        }
        
        // Look for function definitions
        if (strncmp(trimmed, "fun ", 4) == 0) {
            char* func_start = trimmed + 4;
            char* paren = strchr(func_start, '(');
            
            if (paren) {
                // Extract function name
                size_t name_len = paren - func_start;
                if (name_len > 0 && name_len < sizeof(current_function_name) - 1) {
                    strncpy(current_function_name, func_start, name_len);
                    current_function_name[name_len] = '\0';
                    
                    // Trim whitespace from function name
                    char* name_end = current_function_name + name_len - 1;
                    while (name_end > current_function_name && (*name_end == ' ' || *name_end == '\t')) {
                        *name_end = '\0';
                        name_end--;
                    }
                    
                    in_function = true;
                    strcpy(current_function_body, line);
                    strcat(current_function_body, "\n");
                    
                    kuyil_log_debug("Found function: %s at line %d", current_function_name, line_number);
                }
            }
        } else if (in_function) {
            // Continue collecting function body
            strcat(current_function_body, line);
            strcat(current_function_body, "\n");
            
            // Check if function ends (simple heuristic: closing brace at start of line)
            if (trimmed[0] == '}') {
                in_function = false;
                
                // Add function to analysis
                analysis->functions = realloc(analysis->functions, 
                                            (analysis->function_count + 1) * sizeof(KuyilFunctionInfo));
                
                if (analysis->functions) {
                    KuyilFunctionInfo* func_info = &analysis->functions[analysis->function_count];
                    func_info->name = ffi_strdup(current_function_name);
                    func_info->source_code = ffi_strdup(current_function_body);
                    func_info->line_number = line_number;
                    func_info->param_count = 0; // TODO: Parse parameters
                    func_info->param_names = NULL;
                    func_info->return_type = FFI_TYPE_VOID; // TODO: Infer return type
                    func_info->param_types = NULL;
                    func_info->signature = ffi_strdup("() -> void"); // TODO: Build actual signature
                    
                    analysis->function_count++;
                    
                    kuyil_log_info("Captured function: %s (%d lines)", 
                                   current_function_name, line_number);
                }
                
                // Reset for next function
                current_function_name[0] = '\0';
                current_function_body[0] = '\0';
            }
        }
    }
    
    fclose(file);
    
    kuyil_log_info("Source analysis complete: found %d functions in %s", 
                   analysis->function_count, source_file);
    
    return analysis;
}

int ffi_create_smart_shared_library(const char* lib_name, const char* source_file, 
                                   const char* output_path, int flags) {
    if (!lib_name || !source_file || !output_path) {
        kuyil_log_error("Invalid parameters for smart library creation");
        return -1;
    }
    
    kuyil_log_info("Creating smart shared library: %s from %s", lib_name, source_file);
    
    // Analyze the source file
    KuyilSourceAnalysis* analysis = ffi_analyze_kuyil_source(source_file);
    if (!analysis) {
        kuyil_log_error("Failed to analyze source file: %s", source_file);
        return -1;
    }
    
    if (analysis->function_count == 0) {
        kuyil_log_warning("No functions found in source file: %s", source_file);
        ffi_free_source_analysis(analysis);
        return -1;
    }
    
    // Create library specification from analysis
    KuyilSharedLibSpec* spec = ffi_create_lib_spec_from_analysis(lib_name, output_path, analysis);
    if (!spec) {
        kuyil_log_error("Failed to create library specification from analysis");
        ffi_free_source_analysis(analysis);
        return -1;
    }
    
    // Create the shared library
    int result = ffi_create_shared_library(spec);
    
    // Cleanup
    ffi_free_lib_spec(spec);
    ffi_free_source_analysis(analysis);
    
    if (result == 0) {
        kuyil_log_info("Successfully created smart shared library: %s", output_path);
    } else {
        kuyil_log_error("Failed to create smart shared library: %s", output_path);
    }
    
    return result;
}

void ffi_free_source_analysis(KuyilSourceAnalysis* analysis) {
    if (!analysis) return;
    
    free(analysis->filename);
    free(analysis->library_metadata);
    
    if (analysis->functions) {
        for (int i = 0; i < analysis->function_count; i++) {
            KuyilFunctionInfo* func = &analysis->functions[i];
            free(func->name);
            free(func->signature);
            free(func->source_code);
            
            if (func->param_names) {
                for (int j = 0; j < func->param_count; j++) {
                    free(func->param_names[j]);
                }
                free(func->param_names);
            }
            
            free(func->param_types);
        }
        free(analysis->functions);
    }
    
    free(analysis);
}

KuyilSharedLibSpec* ffi_create_lib_spec_from_analysis(const char* lib_name, 
                                                     const char* output_path,
                                                     KuyilSourceAnalysis* analysis) {
    if (!lib_name || !output_path || !analysis) {
        kuyil_log_error("Invalid parameters for creating lib spec from analysis");
        return NULL;
    }
    
    KuyilSharedLibSpec* spec = ffi_create_lib_spec(lib_name, output_path);
    if (!spec) {
        return NULL;
    }
    
    kuyil_log_info("Creating library spec from analysis: %d functions discovered", 
                   analysis->function_count);
    
    // Add each discovered function to the spec
    for (int i = 0; i < analysis->function_count; i++) {
        KuyilFunctionInfo* func_info = &analysis->functions[i];
        
        // For now, use default parameters (enhanced parsing would extract actual params)
        FFIParameter default_params[] = {
            {"a", FFI_TYPE_INT32, sizeof(int32_t), false, NULL},
            {"b", FFI_TYPE_INT32, sizeof(int32_t), false, NULL}
        };
        
        // Add function to spec (use simple heuristics for type inference)
        FFIType return_type = FFI_TYPE_INT32; // Default assumption
        int param_count = 2; // Default assumption
        
        // Try to infer return type from function name
        if (strstr(func_info->name, "print") || strstr(func_info->name, "log")) {
            return_type = FFI_TYPE_VOID;
            param_count = 1;
        } else if (strstr(func_info->name, "string") || strstr(func_info->name, "text")) {
            return_type = FFI_TYPE_STRING;
        } else if (strstr(func_info->name, "bool") || strstr(func_info->name, "is_") || 
                   strstr(func_info->name, "has_") || strstr(func_info->name, "check_")) {
            return_type = FFI_TYPE_BOOL;
        }
        
        ffi_add_exported_function(spec, func_info->name, func_info->source_code, 
                                 return_type, param_count, default_params);
        
        kuyil_log_debug("Added function to spec: %s -> %s", 
                       func_info->name, ffi_type_name(return_type));
    }
    
    return spec;
}

// Enhanced Library Metadata Management Functions

KuyilVersion* ffi_create_version(int major, int minor, int patch, const char* pre_release) {
    KuyilVersion* version = malloc(sizeof(KuyilVersion));
    if (!version) {
        kuyil_log_error("Failed to allocate memory for version");
        return NULL;
    }
    
    version->major = major;
    version->minor = minor;
    version->patch = patch;
    version->pre_release = pre_release ? ffi_strdup(pre_release) : NULL;
    
    kuyil_log_debug("Created version: %d.%d.%d%s%s", major, minor, patch,
                   pre_release ? "-" : "", pre_release ? pre_release : "");
    return version;
}

void ffi_free_version(KuyilVersion* version) {
    if (!version) return;
    free(version->pre_release);
    free(version);
}

int ffi_compare_versions(const KuyilVersion* v1, const KuyilVersion* v2) {
    if (!v1 || !v2) return 0;
    
    // Compare major version
    if (v1->major != v2->major) {
        return (v1->major > v2->major) ? 1 : -1;
    }
    
    // Compare minor version
    if (v1->minor != v2->minor) {
        return (v1->minor > v2->minor) ? 1 : -1;
    }
    
    // Compare patch version
    if (v1->patch != v2->patch) {
        return (v1->patch > v2->patch) ? 1 : -1;
    }
    
    // Compare pre-release (if both exist, do string comparison; if only one exists, stable > pre-release)
    if (v1->pre_release && v2->pre_release) {
        return strcmp(v1->pre_release, v2->pre_release);
    } else if (v1->pre_release && !v2->pre_release) {
        return -1; // v2 (stable) is greater than v1 (pre-release)
    } else if (!v1->pre_release && v2->pre_release) {
        return 1;  // v1 (stable) is greater than v2 (pre-release)
    }
    
    return 0; // Versions are equal
}

bool ffi_version_satisfies(const KuyilVersion* version, const char* requirement) {
    if (!version || !requirement) return false;
    
    // Simple version requirement parsing (>=, <=, ==, >, <)
    char op[8] = {0};
    int req_major = 0, req_minor = 0, req_patch = 0;
    
    // Parse requirement string like ">=1.2.3", "==2.0.0", etc.
    if (sscanf(requirement, "%7s%d.%d.%d", op, &req_major, &req_minor, &req_patch) != 4) {
        kuyil_log_error("Invalid version requirement format: %s", requirement);
        return false;
    }
    
    KuyilVersion req_version = {req_major, req_minor, req_patch, NULL, NULL};
    int cmp = ffi_compare_versions(version, &req_version);
    
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, "==") == 0) return cmp == 0;
    if (strcmp(op, ">") == 0) return cmp > 0;
    if (strcmp(op, "<") == 0) return cmp < 0;
    
    kuyil_log_error("Unsupported version operator: %s", op);
    return false;
}

char* ffi_version_to_string(const KuyilVersion* version) {
    if (!version) return NULL;
    
    char* str = malloc(128);
    if (!str) return NULL;
    
    if (version->pre_release) {
        snprintf(str, 128, "%d.%d.%d-%s", version->major, version->minor, 
                version->patch, version->pre_release);
    } else {
        snprintf(str, 128, "%d.%d.%d", version->major, version->minor, version->patch);
    }
    
    return str;
}

KuyilLibraryInfo* ffi_create_library_info(const char* author, const char* description,
                                         const char* license, const char* homepage) {
    KuyilLibraryInfo* info = malloc(sizeof(KuyilLibraryInfo));
    if (!info) {
        kuyil_log_error("Failed to allocate memory for library info");
        return NULL;
    }
    
    info->author = author ? ffi_strdup(author) : NULL;
    info->description = description ? ffi_strdup(description) : NULL;
    info->license = license ? ffi_strdup(license) : NULL;
    info->homepage = homepage ? ffi_strdup(homepage) : NULL;
    
    kuyil_log_debug("Created library info for author: %s", author ? author : "unknown");
    return info;
}

void ffi_free_library_info(KuyilLibraryInfo* info) {
    if (!info) return;
    
    free(info->author);
    free(info->description);
    free(info->license);
    free(info->homepage);
    free(info);
}

KuyilDependency* ffi_create_dependency(const char* name, const char* version_requirement, bool optional) {
    if (!name) {
        kuyil_log_error("Dependency name cannot be NULL");
        return NULL;
    }
    
    KuyilDependency* dep = malloc(sizeof(KuyilDependency));
    if (!dep) {
        kuyil_log_error("Failed to allocate memory for dependency");
        return NULL;
    }
    
    dep->name = ffi_strdup(name);
    dep->version_requirement = version_requirement ? ffi_strdup(version_requirement) : NULL;
    dep->optional = optional;
    
    kuyil_log_debug("Created dependency: %s %s%s", name, 
                   version_requirement ? version_requirement : "any",
                   optional ? " (optional)" : "");
    return dep;
}

void ffi_free_dependency(KuyilDependency* dependency) {
    if (!dependency) return;
    
    free(dependency->name);
    free(dependency->version_requirement);
    free(dependency);
}

KuyilLibraryRegistry* ffi_create_library_registry(void) {
    KuyilLibraryRegistry* registry = malloc(sizeof(KuyilLibraryRegistry));
    if (!registry) {
        kuyil_log_error("Failed to allocate memory for library registry");
        return NULL;
    }
    
    registry->library_count = 0;
    registry->max_libraries = 16;
    registry->libraries = malloc(registry->max_libraries * sizeof(KuyilSharedLibSpec*));
    
    if (!registry->libraries) {
        free(registry);
        kuyil_log_error("Failed to allocate memory for registry libraries array");
        return NULL;
    }
    
    kuyil_log_info("Created library registry with capacity for %d libraries", registry->max_libraries);
    return registry;
}

bool ffi_registry_add_library(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* library) {
    if (!registry || !library) {
        kuyil_log_error("Invalid parameters for registry add library");
        return false;
    }
    
    // Check if library already exists
    for (int i = 0; i < registry->library_count; i++) {
        if (registry->libraries[i] && registry->libraries[i]->library_name &&
            strcmp(registry->libraries[i]->library_name, library->library_name) == 0) {
            kuyil_log_warning("Library '%s' already exists in registry", library->library_name);
            return false;
        }
    }
    
    // Expand array if needed
    if (registry->library_count >= registry->max_libraries) {
        registry->max_libraries *= 2;
        registry->libraries = realloc(registry->libraries, 
                                     registry->max_libraries * sizeof(KuyilSharedLibSpec*));
        if (!registry->libraries) {
            kuyil_log_error("Failed to expand registry libraries array");
            return false;
        }
    }
    
    registry->libraries[registry->library_count] = library;
    registry->library_count++;
    
    kuyil_log_info("Added library '%s' to registry (%d total libraries)", 
                   library->library_name, registry->library_count);
    return true;
}

KuyilSharedLibSpec* ffi_registry_find_library(KuyilLibraryRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    
    for (int i = 0; i < registry->library_count; i++) {
        if (registry->libraries[i] && registry->libraries[i]->library_name &&
            strcmp(registry->libraries[i]->library_name, name) == 0) {
            return registry->libraries[i];
        }
    }
    
    kuyil_log_debug("Library '%s' not found in registry", name);
    return NULL;
}

bool ffi_registry_remove_library(KuyilLibraryRegistry* registry, const char* name) {
    if (!registry || !name) return false;
    
    for (int i = 0; i < registry->library_count; i++) {
        if (registry->libraries[i] && registry->libraries[i]->library_name &&
            strcmp(registry->libraries[i]->library_name, name) == 0) {
            
            // Shift remaining libraries down
            for (int j = i; j < registry->library_count - 1; j++) {
                registry->libraries[j] = registry->libraries[j + 1];
            }
            registry->library_count--;
            
            kuyil_log_info("Removed library '%s' from registry", name);
            return true;
        }
    }
    
    kuyil_log_warning("Library '%s' not found for removal from registry", name);
    return false;
}

void ffi_registry_list_libraries(KuyilLibraryRegistry* registry) {
    if (!registry) {
        printf("Registry is NULL\n");
        return;
    }
    
    printf("Library Registry (%d libraries):\n", registry->library_count);
    printf("%-25s %-15s %-40s\n", "Name", "Version", "Output Path");
    printf("%-25s %-15s %-40s\n", "----", "-------", "-----------");
    
    for (int i = 0; i < registry->library_count; i++) {
        KuyilSharedLibSpec* lib = registry->libraries[i];
        if (lib) {
            char* version_str = "unknown";
            if (lib->version.major != 0 || lib->version.minor != 0 || lib->version.patch != 0) {
                version_str = ffi_version_to_string(&lib->version);
            }
            
            printf("%-25s %-15s %-40s\n",
                   lib->library_name ? lib->library_name : "NULL",
                   version_str,
                   lib->output_path ? lib->output_path : "NULL");
            
            if (strcmp(version_str, "unknown") != 0) {
                free(version_str);
            }
        }
    }
}

void ffi_free_library_registry(KuyilLibraryRegistry* registry) {
    if (!registry) return;
    
    // Note: We don't free the individual libraries here as they might be owned elsewhere
    // The caller is responsible for freeing the KuyilSharedLibSpec objects
    free(registry->libraries);
    free(registry);
    
    kuyil_log_info("Freed library registry");
}

bool ffi_check_dependencies(KuyilLibraryRegistry* registry, KuyilSharedLibSpec* library) {
    if (!registry || !library) return false;
    
    if (library->dependency_count == 0) {
        kuyil_log_debug("Library '%s' has no dependencies", library->library_name);
        return true;
    }
    
    kuyil_log_info("Checking %d dependencies for library '%s'", 
                   library->dependency_count, library->library_name);
    
    for (int i = 0; i < library->dependency_count; i++) {
        KuyilDependency* dep = &library->dependencies[i];
        if (!dep->name) continue;
        
        // Find dependency in registry
        KuyilSharedLibSpec* dep_lib = ffi_registry_find_library(registry, dep->name);
        if (!dep_lib) {
            if (!dep->optional) {
                kuyil_log_error("Required dependency '%s' not found for library '%s'", 
                               dep->name, library->library_name);
                return false;
            } else {
                kuyil_log_warning("Optional dependency '%s' not found for library '%s'", 
                                 dep->name, library->library_name);
                continue;
            }
        }
        
        // Check version requirement if specified
        if (dep->version_requirement && dep_lib->version.major != 0) {
            if (!ffi_version_satisfies(&dep_lib->version, dep->version_requirement)) {
                kuyil_log_error("Dependency version mismatch: '%s' requires '%s' but found '%s'",
                               dep->name, dep->version_requirement,
                               ffi_version_to_string(&dep_lib->version));
                return false;
            }
        }
        
        kuyil_log_debug("Dependency '%s' satisfied", dep->name);
    }
    
    kuyil_log_info("All dependencies satisfied for library '%s'", library->library_name);
    return true;
}

void ffi_set_library_metadata(KuyilSharedLibSpec* library, KuyilVersion* version, 
                             KuyilLibraryInfo* info, KuyilDependency* dependencies, int dep_count) {
    if (!library) return;
    
    // Copy version if provided
    if (version) {
        library->version = *version;
    }
    
    // Copy info if provided  
    if (info) {
        library->info = *info;
    }
    
    // Set dependencies
    library->dependencies = dependencies;
    library->dependency_count = dep_count;
    library->created_time = time(NULL);
    
    kuyil_log_info("Set metadata for library '%s'", library->library_name);
}