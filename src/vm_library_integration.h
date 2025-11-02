#ifndef VM_LIBRARY_INTEGRATION_H
#define VM_LIBRARY_INTEGRATION_H

#include "vm.h"
#include "library_loader.h"

// Library statistics structure
typedef struct {
    int total_libraries;
    int loaded_libraries;
    int total_functions;
} LibraryStats;

// VM library system functions
bool vm_init_library_system(VM* vm);
void vm_cleanup_library_system(void);

// Global VM access for libraries
void set_current_vm(VM* vm);
VM* get_current_vm(void);

// Source path propagation for better error reporting
void set_current_source_path(const char* path);
const char* get_current_source_path(void);

// Invoke a Kuyil function value from C (re-entrant). Returns true on success.
bool call_kuyil_function(Value function_value, int arg_count, Value* args, Value* result_out);

// Look up a global variable by name in the VM (exported for library use)
Value vm_lookup_global(const char* name);

// Dynamic function calling
Value call_dynamic_function(const char* name, int arg_count, Value* args);
bool is_dynamic_function(const char* name);
Value create_dynamic_function_value(const char* name);

// Test/mocking support for dynamic functions (used in --test mode)
void mock_set_return_value(const char* name, Value v);
void mock_clear(const char* name);
int mock_get_call_count(const char* name);

// Library management
LibraryStats get_library_stats(void);
void list_loaded_libraries(void);

// Programmatic access functions for Kuyil scripts
Value vm_get_library_count(int arg_count, Value* args);
Value vm_get_library_name(int arg_count, Value* args);
Value vm_get_library_path(int arg_count, Value* args);
Value vm_is_library_loaded(int arg_count, Value* args);
Value vm_get_library_function_count(int arg_count, Value* args);
Value vm_get_loaded_libraries_info(int arg_count, Value* args);
Value vm_get_total_function_count(int arg_count, Value* args);

// Inline library configuration functions
Value vm_add_library(int arg_count, Value* args);
Value vm_load_library_inline(int arg_count, Value* args);
Value vm_clear_libraries(int arg_count, Value* args);

// Module import functions
Value vm_import_module(int arg_count, Value* args);
Value vm_export_function(int arg_count, Value* args);

// Internal functions (implemented in .c file)
// These are now implemented and don't need separate declarations

#endif