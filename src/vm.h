#ifndef KUYIL_VM_H
#define KUYIL_VM_H

#include "bytecode.h"
#include <stdint.h>

#define STACK_MAX 256
#define GLOBALS_MAX 256

typedef struct {
    Function* function;
    uint8_t* ip;  // instruction pointer
    Value* slots; // local variables and stack for this frame
} CallFrame;

#define FRAMES_MAX 64

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    
    Value stack[STACK_MAX];
    Value* stack_top;
    
    // Global variables
    struct {
        char* name;
        Value value;
    } globals[GLOBALS_MAX];
    int global_count;
    
    // Built-in functions
    Function* print_fn;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void vm_init(VM* vm);
void vm_free(VM* vm);
InterpretResult vm_interpret(VM* vm, const char* source);
InterpretResult vm_interpret_bytecode(VM* vm, const char* bytecode_path);
InterpretResult vm_run(VM* vm);

// Stack operations
void vm_push(VM* vm, Value value);
Value vm_pop(VM* vm);
Value vm_peek(VM* vm, int distance);

// Global variable management
void define_global(VM* vm, const char* name, Value value);

// Dynamic function system (from modular library system)
bool is_dynamic_function(const char* name);
Value call_dynamic_function(const char* name, int arg_count, Value* args);

// Built-in functions
void vm_register_natives(VM* vm);

// Dynamic execution
typedef enum {
    LIBRARY_CORE = 1 << 0,    // Basic print, variables
    LIBRARY_STRING = 1 << 1,  // String manipulation functions
    LIBRARY_MATH = 1 << 2,    // Math functions
    LIBRARY_DATE = 1 << 3,    // Date/time functions
    LIBRARY_LOG = 1 << 4,     // Logging functions
    LIBRARY_ENV = 1 << 5,     // Environment variables
    LIBRARY_FFI = 1 << 6,     // Foreign function interface
    LIBRARY_HTTP = 1 << 7,    // HTTP client/server
    LIBRARY_ALL = 0xFF        // All libraries
} LibraryFlags;

// Compiled script handle for reuse
typedef struct {
    Function* function;       // Compiled bytecode
    LibraryFlags libraries;   // Required libraries
    int is_valid;            // Handle validity flag
    int handle_id;           // Unique identifier
} CompiledScript;

InterpretResult vm_execute_dynamic(VM* vm, const char* source, 
                                 Value* input_params, int param_count,
                                 LibraryFlags allowed_libraries,
                                 Value* result);

// Compile-once, execute-multiple API
int vm_compile_script(const char* source, LibraryFlags required_libraries);
InterpretResult vm_execute_compiled(int script_handle, Value* input_params, 
                                   int param_count, Value* result);
void vm_free_script(int script_handle);

#endif