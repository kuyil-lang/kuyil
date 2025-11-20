#ifndef KUYIL_VM_H
#define KUYIL_VM_H

#include "bytecode.h"
#include <stdint.h>
#include <event2/event.h>

// Forward declarations
typedef struct AvatarRuntime AvatarRuntime;
typedef struct AsyncHttpClient AsyncHttpClient;
typedef struct AsyncRequestQueue AsyncRequestQueue;

#define STACK_MAX 2048
#define GLOBALS_MAX 2048

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

    // Test and instrumentation flags
    bool test_mode;
    bool coverage_enabled;

    // Test statistics
    int assertions_total;
    int assertions_failed;

    // Coverage data (per-function instruction hit counts)
    struct {
        struct {
            Function* function;
            int* hits;      // length = function->chunk.count
            int hits_len;
        } entries[256];
        int count;
    } coverage;
    // Optional script path for reporting
    const char* current_source_path;
    
    // Avatar runtime for async execution
    AvatarRuntime* avatar_runtime;
    
    // Async HTTP client
    AsyncHttpClient* async_http;
    struct event_base* event_base;
    
    // Async request queue for non-blocking I/O
    AsyncRequestQueue* request_queue;
    
    // Program arguments (sys.args)
    char** program_args;
    int program_args_count;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void vm_init(VM* vm);
void vm_set_program_args(VM* vm, int argc, char** argv);
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
bool vm_get_global_value(VM* vm, const char* name, Value* out_value);
void vm_copy_global(VM* dest_vm, VM* src_vm, const char* name);

// Dynamic function system (from modular library system)
bool is_dynamic_function(const char* name);
Value call_dynamic_function(const char* name, int arg_count, Value* args);

// Built-in functions
void vm_register_natives(VM* vm);

// Test/assert helpers
void vm_enable_test_mode(VM* vm, bool enabled);
int vm_get_assert_failures(VM* vm);

// Coverage/instrumentation helpers
void vm_enable_coverage(VM* vm, bool enabled, const char* source_path);
void vm_coverage_report_text(VM* vm);
void vm_coverage_report_lcov(VM* vm);

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

// Public function for external libraries to lookup global variables
Value vm_lookup_global(const char* name);

// Shared frame setup utility for both main VM and avatars
// Sets up a call frame with proper slots and stack_top positioning
// stack_top_ptr: pointer to stack_top (will be modified)
// stack: base stack array
// arg_count: number of arguments on stack
// function: function to call
// Returns: pointer to the new call frame
CallFrame* vm_setup_call_frame(CallFrame* frames, int* frame_count, 
                                 Value** stack_top_ptr, Function* function, 
                                 int arg_count);

// Extended version with temp reserve parameter (for avatar runtime)
CallFrame* vm_setup_call_frame_ex(CallFrame* frames, int* frame_count, 
                                    Value** stack_top_ptr, Function* function, 
                                    int arg_count, bool add_reserve);

// Task queue API for thread-safe VM operations
#include "vm_task_queue.h"
TaskQueue* vm_get_task_queue(void);
int vm_process_pending_tasks(int max_tasks);
bool vm_task_queue_empty(void);
size_t vm_task_queue_size(void);

// Avatar runtime API for async execution
int vm_process_avatar_completions(int max_avatars);
size_t vm_avatar_pending_count(void);

// Async request queue API for non-blocking I/O
int vm_process_async_requests(int max_requests);

// Event loop processing for async operations
// Returns number of events processed
int vm_process_events(int timeout_ms);

#endif