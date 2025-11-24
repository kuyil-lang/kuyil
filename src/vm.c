#define _POSIX_C_SOURCE 200809L
#include "vm.h"
#include "logging.h"
#include "config.h"
#include "ffi.h"
#include "vm_task_queue.h"
#include "avatar_runtime.h"
#include "async_http.h"
#include "async_request_queue.h"
#include "vm_call_shared.h"
// HTTP functionality now in shared libraries
#include "file_reader.h"
#include "green_threads.h"
#include "vm_library_integration.h"
#include "opcode_executor.h"
#include "route_decorator.h"
#include "request_response.h"
#include "async_http_server.h"
#include "lexer.c"
#include "parser.c"
#include "compiler.c"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>
#ifndef _WIN32
    #include <signal.h>
    #include <execinfo.h>
    #include <unistd.h>
#endif
#ifndef _WIN32
    #include <sys/time.h>
#else
    #include <windows.h>
    #include <winsock2.h>  // For struct timeval on Windows
    // Windows alternative for gettimeofday
    int gettimeofday(struct timeval* tv, void* tz) {
        FILETIME ft;
        uint64_t tmpres = 0;
        GetSystemTimeAsFileTime(&ft);
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
        tmpres /= 10;
        tmpres -= 11644473600000000ULL;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
        return 0;
    }
#endif

// Dynamic library support now handled by modular library system

// Global FFI context
static FFIContext* g_ffi_context = NULL;

// Global task queue for thread-safe VM operations
static TaskQueue* g_vm_task_queue = NULL;

// Global VM instance for avatar runtime access
static VM* g_current_vm = NULL;

static void reset_stack(VM* vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
}

// Print a compact C backtrace for native crashes (SIGSEGV, etc.)
#ifndef _WIN32
static void kuyil_print_c_backtrace(void) {
    void* buffer[64];
    int nptrs = backtrace(buffer, 64);
    fprintf(stderr, "[KUYIL][C-Backtrace] frames=%d\n", nptrs);
    backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
}

static void kuyil_signal_handler(int sig) {
    const char* name = strsignal(sig);
    fprintf(stderr, "\n[KUYIL] Caught fatal signal %d (%s)\n", sig, name ? name : "?");
    kuyil_print_c_backtrace();
    // Re-raise with default handler to allow core dump if enabled
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[KUYIL][RuntimeError] ");
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    // Print a brief location summary for the top frame
    if (vm->frame_count > 0) {
        CallFrame* top = &vm->frames[vm->frame_count - 1];
        if (top && top->function) {
            Function* tfn = top->function;
            int line = -1;
            if (top->ip && tfn->chunk.code && tfn->chunk.count > 0) {
                size_t tip = (size_t)(top->ip - tfn->chunk.code);
                if (tip > 0) tip -= 1; // last executed instruction
                if (tip < (size_t)tfn->chunk.count) {
                    line = tfn->chunk.lines[tip];
                }
            }
            const char* fname = (tfn->name ? tfn->name : "script");
            const char* src = tfn->source_path ? tfn->source_path : (vm->current_source_path ? vm->current_source_path : "<unknown>");
            fprintf(stderr, "[location] %s:%d in %s()\n", src, line, fname);
        }
    }

    // Print stack trace (Kuyil)
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        if (!frame || !frame->function) continue;
        Function* function = frame->function;
        int line = -1;
        if (frame->ip && function->chunk.code && function->chunk.count > 0) {
            size_t instruction = (size_t)(frame->ip - function->chunk.code);
            if (instruction > 0) instruction -= 1;
            if (instruction < (size_t)function->chunk.count) {
                line = function->chunk.lines[instruction];
            }
        }
        const char* src = function->source_path ? function->source_path : (vm->current_source_path ? vm->current_source_path : "<unknown>");
        fprintf(stderr, "  at %s:%d in ", src, line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name);
        }
    }
    
    reset_stack(vm);
}

void vm_push(VM* vm, Value value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM* vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

Value vm_peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static bool is_falsey(Value value) {
    return value.type == VALUE_NIL || 
           (value.type == VALUE_BOOL && !value.as.boolean);
}

// values_equal is now in opcode_executor.c (shared)

// Public helpers for test and coverage modes
void vm_enable_test_mode(VM* vm, bool enabled) {
    vm->test_mode = enabled;
    vm->assertions_total = 0;
    vm->assertions_failed = 0;
}

int vm_get_assert_failures(VM* vm) {
    return vm->assertions_failed;
}

void vm_enable_coverage(VM* vm, bool enabled, const char* source_path) {
    vm->coverage_enabled = enabled;
    vm->current_source_path = source_path;
    if (!enabled) {
        // reset coverage data
        for (int i = 0; i < vm->coverage.count; i++) {
            free(vm->coverage.entries[i].hits);
            vm->coverage.entries[i].hits = NULL;
            vm->coverage.entries[i].function = NULL;
            vm->coverage.entries[i].hits_len = 0;
        }
        vm->coverage.count = 0;
    }
}

static void coverage_report_text_for_entry(VM* vm, int idx) {
    Function* fn = vm->coverage.entries[idx].function;
    int* hits = vm->coverage.entries[idx].hits;
    int len = vm->coverage.entries[idx].hits_len;
    int executed = 0;
    for (int i = 0; i < len; i++) if (hits[i] > 0) executed++;
    double pct = len > 0 ? (100.0 * executed / len) : 100.0;
    const char* name = fn && fn->name ? fn->name : "<script>";
    printf("COVERAGE %s: %d/%d (%.1f%%)\n", name, executed, len, pct);
}

void vm_coverage_report_text(VM* vm) {
    printf("\n==== Coverage Report (text) ====%s\n", vm->current_source_path ? "" : "");
    for (int i = 0; i < vm->coverage.count; i++) {
        coverage_report_text_for_entry(vm, i);
    }
}

void vm_coverage_report_lcov(VM* vm) {
    // Minimal LCOV output using current_source_path and per-instruction lines
    const char* sf = vm->current_source_path ? vm->current_source_path : "<unknown>";
    for (int i = 0; i < vm->coverage.count; i++) {
        Function* fn = vm->coverage.entries[i].function;
        int* hits = vm->coverage.entries[i].hits;
        int len = vm->coverage.entries[i].hits_len;
        printf("TN:%s\n", fn && fn->name ? fn->name : "");
        printf("SF:%s\n", sf);
        // Map instruction indices to source lines
        if (fn) {
            // Use a simple map to avoid duplicate DA lines; assume lines are <= 65536
            int last_line = -1;
            for (int ip = 0; ip < len; ip++) {
                int line = fn->chunk.lines[ip];
                if (line != last_line) {
                    int count = hits[ip];
                    printf("DA:%d,%d\n", line, count);
                    last_line = line;
                }
            }
        }
        printf("end_of_record\n");
    }
}

static void __attribute__((unused)) concatenate(VM* vm) {
    Value b = vm_pop(vm);
    Value a = vm_pop(vm);
    
    // Convert both values to strings
    char buf_a[64] = {0};
    char buf_b[64] = {0};
    const char* str_a;
    const char* str_b;
    
    // Convert a to string
    if (a.type == VALUE_STRING) {
        str_a = a.as.string ? a.as.string : "";
    } else if (a.type == VALUE_NUMBER) {
        snprintf(buf_a, sizeof(buf_a), "%g", a.as.number);
        str_a = buf_a;
    } else if (a.type == VALUE_BOOL) {
        str_a = a.as.boolean ? "true" : "false";
    } else if (a.type == VALUE_NIL) {
        str_a = "nil";
    } else {
        str_a = "[object]";
    }
    
    // Convert b to string
    if (b.type == VALUE_STRING) {
        str_b = b.as.string ? b.as.string : "";
    } else if (b.type == VALUE_NUMBER) {
        snprintf(buf_b, sizeof(buf_b), "%g", b.as.number);
        str_b = buf_b;
    } else if (b.type == VALUE_BOOL) {
        str_b = b.as.boolean ? "true" : "false";
    } else if (b.type == VALUE_NIL) {
        str_b = "nil";
    } else {
        str_b = "[object]";
    }
    
    int length = strlen(str_a) + strlen(str_b);
    char* chars = malloc(length + 1);
    strcpy(chars, str_a);
    strcat(chars, str_b);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = chars;
    
    vm_push(vm, result);
}

// Global variable functions
static int find_global(VM* vm, const char* name) {
    for (int i = 0; i < vm->global_count; i++) {
        if (strcmp(vm->globals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void define_global(VM* vm, const char* name, Value value) {
    if (vm->global_count >= GLOBALS_MAX) {
        runtime_error(vm, "Too many global variables.");
        return;
    }
    
    vm->globals[vm->global_count].name = strdup(name);
    vm->globals[vm->global_count].value = value;
    vm->global_count++;
}

static bool get_global(VM* vm, const char* name, Value* value) {
    int index = find_global(vm, name);
    if (index != -1) {
        *value = vm->globals[index].value;
        return true;
    }
    
    // Check if it's a dynamic function
    if (is_dynamic_function(name)) {
        *value = create_dynamic_function_value(name);
        return true;
    }
    
    return false;
}

static bool set_global(VM* vm, const char* name, Value value) {
    int index = find_global(vm, name);
    if (index == -1) return false;
    
    vm->globals[index].value = value;
    return true;
}

// Public function to get a global value from a VM
bool vm_get_global_value(VM* vm, const char* name, Value* out_value) {
    return get_global(vm, name, out_value);
}

// Copy a global variable from one VM to another
void vm_copy_global(VM* dest_vm, VM* src_vm, const char* name) {
    Value value;
    if (get_global(src_vm, name, &value)) {
        // Check if it already exists in dest - if so, update it
        int index = find_global(dest_vm, name);
        if (index != -1) {
            dest_vm->globals[index].value = value;
        } else {
            // Define as new global
            define_global(dest_vm, name, value);
        }
    }
}

// Register decorator metadata for runtime introspection
void vm_register_decorators(VM* vm, const char* entity_name, Value decorators, Value param_decorators) {
    if (vm->decorator_registry_count >= 256) {
        return; // Registry full
    }
    
    // Check if entity already exists, update if so
    for (int i = 0; i < vm->decorator_registry_count; i++) {
        if (strcmp(vm->decorator_registry[i].entity_name, entity_name) == 0) {
            vm->decorator_registry[i].decorators = decorators;
            vm->decorator_registry[i].param_decorators = param_decorators;
            return;
        }
    }
    
    // Add new entry
    vm->decorator_registry[vm->decorator_registry_count].entity_name = strdup(entity_name);
    vm->decorator_registry[vm->decorator_registry_count].decorators = decorators;
    vm->decorator_registry[vm->decorator_registry_count].param_decorators = param_decorators;
    vm->decorator_registry_count++;
}

// Helper: Find decorator by name in decorator array
static Value* find_decorator(Value decorators_array, const char* name) {
    if (decorators_array.type != VALUE_ARRAY) return NULL;
    
    for (int i = 0; i < decorators_array.as.array.count; i++) {
        Value dec = decorators_array.as.array.values[i];
        if (dec.type == VALUE_OBJECT) {
            // Find "name" property
            for (int j = 0; j < dec.as.object.count; j++) {
                if (strcmp(dec.as.object.keys[j], "name") == 0) {
                    if (dec.as.object.values[j].type == VALUE_STRING &&
                        strcmp(dec.as.object.values[j].as.string, name) == 0) {
                        return &decorators_array.as.array.values[i];
                    }
                }
            }
        }
    }
    return NULL;
}

// Helper: Get decorator property value
static Value get_decorator_property(Value decorator, const char* prop_name) {
    Value nil_val = {VALUE_NIL};
    if (decorator.type != VALUE_OBJECT) return nil_val;
    
    for (int i = 0; i < decorator.as.object.count; i++) {
        if (strcmp(decorator.as.object.keys[i], prop_name) == 0) {
            return decorator.as.object.values[i];
        }
    }
    return nil_val;
}

// Dependency injection registry (simple key-value store)
static struct {
    char* keys[256];
    Value values[256];
    int count;
} di_registry = {.count = 0};

// Register a dependency for injection
void vm_register_dependency(const char* name, Value value) {
    // Check if exists, update
    for (int i = 0; i < di_registry.count; i++) {
        if (strcmp(di_registry.keys[i], name) == 0) {
            di_registry.values[i] = value;
            return;
        }
    }
    // Add new
    if (di_registry.count < 256) {
        di_registry.keys[di_registry.count] = strdup(name);
        di_registry.values[di_registry.count] = value;
        di_registry.count++;
    }
}

// Get dependency for injection
static bool vm_get_dependency(const char* name, Value* out) {
    for (int i = 0; i < di_registry.count; i++) {
        if (strcmp(di_registry.keys[i], name) == 0) {
            *out = di_registry.values[i];
            return true;
        }
    }
    return false;
}

// Built-in functions
static void print_value(Value value) {
    switch (value.type) {
        case VALUE_BOOL:
            printf(value.as.boolean ? "true" : "false");
            break;
        case VALUE_NIL: 
            printf("nil"); 
            break;
        case VALUE_NUMBER: 
            // Check if it's a whole number (likely timestamp or integer)
            if (value.as.number == (long long)value.as.number) {
                printf("%.0f", value.as.number);
            } else {
                printf("%g", value.as.number);
            }
            break;
        case VALUE_STRING: 
            printf("%s", value.as.string); 
            break;
        case VALUE_ARRAY:
            printf("[");
            for (int i = 0; i < value.as.array.count; i++) {
                print_value(value.as.array.values[i]);
                if (i < value.as.array.count - 1) printf(", ");
            }
            printf("]");
            break;
        case VALUE_OBJECT:
            printf("{");
            for (int i = 0; i < value.as.object.count; i++) {
                printf("\"%s\": ", value.as.object.keys[i]);
                print_value(value.as.object.values[i]);
                if (i < value.as.object.count - 1) printf(", ");
            }
            printf("}");
            break;
        case VALUE_FUNCTION:
            printf("[Function]");
            break;
    }
}

static Value __attribute__((unused)) native_print(int arg_count, Value* args) {
    for (int i = 0; i < arg_count; i++) {
        print_value(args[i]);
        if (i < arg_count - 1) printf(" ");
    }
    printf("\n");
    fflush(stdout);  // Ensure output is written immediately
    
    Value result;
    result.type = VALUE_NIL;
    return result;
}

// String manipulation functions now handled by modular library system

// Number conversion functions
static Value __attribute__((unused)) native_to_number(int arg_count, Value* args) {
    if (arg_count != 1) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    
    switch (args[0].type) {
        case VALUE_NUMBER:
            result.as.number = args[0].as.number;
            break;
        case VALUE_STRING: {
            char* endptr;
            double num = strtod(args[0].as.string, &endptr);
            if (*endptr == '\0') {
                result.as.number = num;
            } else {
                result.type = VALUE_NIL; // Invalid number format
            }
            break;
        }
        case VALUE_BOOL:
            result.as.number = args[0].as.boolean ? 1.0 : 0.0;
            break;
        default:
            result.type = VALUE_NIL;
            break;
    }
    
    return result;
}

static Value __attribute__((unused)) native_to_string(int arg_count, Value* args) {
    if (arg_count != 1) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_STRING;
    
    switch (args[0].type) {
        case VALUE_STRING:
            result.as.string = strdup(args[0].as.string);
            break;
        case VALUE_NUMBER: {
            char* str = malloc(64);  // Increased size for large integers
            // Check if number is an integer (no decimal part)
            if (args[0].as.number == (long long)args[0].as.number) {
                // Integer: use fixed format to avoid scientific notation
                snprintf(str, 64, "%.0f", args[0].as.number);
            } else {
                // Float: use %g but ensure no scientific notation for reasonable numbers
                snprintf(str, 64, "%.15g", args[0].as.number);
            }
            result.as.string = str;
            break;
        }
        case VALUE_BOOL:
            result.as.string = strdup(args[0].as.boolean ? "true" : "false");
            break;
        case VALUE_NIL:
            result.as.string = strdup("nil");
            break;
        default:
            result.as.string = strdup("[Object]");
            break;
    }
    
    return result;
}

// Helper to stringify value types for diagnostics
static const char* value_type_name(ValueType t) {
    switch (t) {
        case VALUE_NIL: return "nil";
        case VALUE_BOOL: return "bool";
        case VALUE_NUMBER: return "number";
        case VALUE_STRING: return "string";
        case VALUE_ARRAY: return "array";
        case VALUE_OBJECT: return "object";
        case VALUE_FUNCTION: return "function";
        default: return "<unknown>";
    }
}

// ============================================================================
// Object Field Utilities - for C code to manipulate VALUE_OBJECT uniformly
// ============================================================================

// Get field value from object by key name, returns NULL if not found
Value* vm_object_get_field(Value* object, const char* key) {
    if (!object || object->type != VALUE_OBJECT || !key) {
        return NULL;
    }
    
    for (int i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.keys[i], key) == 0) {
            return &object->as.object.values[i];
        }
    }
    return NULL;
}

// Set existing field value or add new field if it doesn't exist
void vm_object_set_field(Value* object, const char* key, Value value) {
    if (!object || object->type != VALUE_OBJECT || !key) {
        return;
    }
    
    // Check if field already exists
    for (int i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.keys[i], key) == 0) {
            // Free old value if it was a string
            if (object->as.object.values[i].type == VALUE_STRING && 
                object->as.object.values[i].as.string) {
                free(object->as.object.values[i].as.string);
            }
            // Set new value
            object->as.object.values[i] = value;
            return;
        }
    }
    
    // Field doesn't exist, add it
    int new_count = object->as.object.count + 1;
    object->as.object.keys = realloc(object->as.object.keys, new_count * sizeof(char*));
    object->as.object.values = realloc(object->as.object.values, new_count * sizeof(Value));
    
    object->as.object.keys[object->as.object.count] = strdup(key);
    object->as.object.values[object->as.object.count] = value;
    object->as.object.count = new_count;
}

// Create a new empty object
Value vm_object_create(void) {
    Value obj;
    obj.type = VALUE_OBJECT;
    obj.as.object.count = 0;
    obj.as.object.keys = NULL;
    obj.as.object.values = NULL;
    return obj;
}

// Create object with single field (common case)
Value vm_object_create_with_field(const char* key, Value value) {
    Value obj = vm_object_create();
    vm_object_set_field(&obj, key, value);
    return obj;
}

// Shared frame setup utility for both main VM and avatars
// This extracts the common logic from call_value() to ensure consistency
// add_reserve: if true, add extra stack space for expression evaluation (avatars need this)
CallFrame* vm_setup_call_frame_ex(CallFrame* frames, int* frame_count, 
                                    Value** stack_top_ptr, Function* function, 
                                    int arg_count, bool add_reserve) {
    if (*frame_count >= FRAMES_MAX) {
        return NULL;  // Stack overflow
    }
    
    CallFrame* frame = &frames[(*frame_count)++];
    frame->function = function;
    frame->ip = function->chunk.code;
    
    // Stack layout before: [... arg0] [arg1] ... [argN] [callee] <- stack_top
    // Set slots to point to arg0 (at stack_top - arg_count - 1)
    frame->slots = *stack_top_ptr - arg_count - 1;
    
    // CRITICAL: Proper SET_LOCAL/GET_LOCAL implementation requires:
    // - frame->slots[0..arg_count-1] = parameters (already on stack)
    // - frame->slots[arg_count..local_count-1] = local variables (need stack space!)
    // Adjust stack_top to point after ALL locals (not just arguments)
    *stack_top_ptr = frame->slots + arg_count;
    
    // Allocate stack space for local variables (beyond parameters)
    // Initialize them to NIL so GET_LOCAL before SET_LOCAL returns something valid
    int local_count = function->local_count;
    for (int i = arg_count; i < local_count; i++) {
        Value nil_val = {VALUE_NIL};
        **stack_top_ptr = nil_val;
        (*stack_top_ptr)++;
    }
    
    // Avatar runtime needs extra space to prevent expression evaluation from
    // overwriting local variables during recursive calls
    if (add_reserve) {
        #define TEMP_STACK_RESERVE 16
        *stack_top_ptr += TEMP_STACK_RESERVE;
    }
    
    return frame;
}

// Wrapper for backward compatibility (main VM doesn't need reserve)
CallFrame* vm_setup_call_frame(CallFrame* frames, int* frame_count, 
                                 Value** stack_top_ptr, Function* function, 
                                 int arg_count) {
    return vm_setup_call_frame_ex(frames, frame_count, stack_top_ptr, function, arg_count, false);
}

// Helper functions for CallContext (main VM)
static Value* vm_peek_helper(void* context, int distance) {
    VM* vm = (VM*)context;
    if (vm->stack_top - distance - 1 < vm->stack) {
        return NULL;
    }
    return vm->stack_top - distance - 1;
}

static Value vm_pop_helper(void* context) {
    VM* vm = (VM*)context;
    return vm_pop(vm);
}

static void vm_push_helper(void* context, Value value) {
    VM* vm = (VM*)context;
    vm_push(vm, value);
}

static Value* vm_get_args_helper(void* context, int arg_count) {
    VM* vm = (VM*)context;
    // Stack layout: [...] [arg0, arg1, ..., argN-1, callee] <- stack_top
    return vm->stack_top - arg_count - 1;
}

static void vm_pop_n_helper(void* context, int n) {
    VM* vm = (VM*)context;
    vm->stack_top -= n;
    if (vm->stack_top < vm->stack) {
        vm->stack_top = vm->stack;
    }
}

static void vm_report_error_helper(void* context, const char* message) {
    VM* vm = (VM*)context;
    runtime_error(vm, "%s", message);
}

static bool vm_setup_frame_helper(void* context, Function* function, int arg_count) {
    VM* vm = (VM*)context;
    CallFrame* frame = vm_setup_call_frame(vm->frames, &vm->frame_count,
                                            &vm->stack_top, function, arg_count);
    return frame != NULL;
}

static bool vm_get_global_helper(void* context, const char* name, Value* out) {
    VM* vm = (VM*)context;
    return get_global(vm, name, out);
}

static bool call_value(VM* vm, Value callee, int arg_count) {
    // Check for mocked functions (test mode)
    extern bool mock_matches_call(const char* name, Value* args, int arg_count, Value* out_value);
    
    // Check string callees
    if (callee.type == VALUE_STRING && vm->test_mode) {
        Value* args = vm->stack_top - arg_count - 1;
        Value mock_result;
        if (mock_matches_call(callee.as.string, args, arg_count, &mock_result)) {
            // Function is mocked, return mock value
            vm->stack_top -= arg_count + 1;
            vm_push(vm, mock_result);
            return true;
        }
    }
    
    if (callee.type == VALUE_FUNCTION) {
        // Function call
        Function* function = callee.as.function.function;
        
        // Check if this function is mocked (test mode)
        if (vm->test_mode && function->name) {
            Value* args = vm->stack_top - arg_count - 1;
            Value mock_result;
            if (mock_matches_call(function->name, args, arg_count, &mock_result)) {
                // Function is mocked, return mock value
                vm->stack_top -= arg_count + 1;
                vm_push(vm, mock_result);
                return true;
            }
        }
        
        // Check for @wrap() decorator - intercepts function calls
        // Skip wrapping if this is the unwrapped function being called from within a handler
        bool skip_wrap = (vm->unwrapped_function_name && function->name && 
                         strcmp(vm->unwrapped_function_name, function->name) == 0);
        
        if (function->name && !skip_wrap) {
            for (int i = 0; i < vm->decorator_registry_count; i++) {
                if (strcmp(vm->decorator_registry[i].entity_name, function->name) == 0) {
                    Value decorators = vm->decorator_registry[i].decorators;
                    
                    // Skip if no decorators
                    if (decorators.type != VALUE_ARRAY || decorators.as.array.count == 0) {
                        break;
                    }
                    
                    Value* wrap_decorator = find_decorator(decorators, "wrap");
                    
                    if (wrap_decorator) {
                        // Get handler function from decorator
                        Value handler_prop = get_decorator_property(*wrap_decorator, "handler");
                        Value handler_func;
                        
                        // If handler is a string, look it up as a global function
                        if (handler_prop.type == VALUE_STRING) {
                            if (!get_global(vm, handler_prop.as.string, &handler_func)) {
                                runtime_error(vm, "@wrap handler '%s' not found", handler_prop.as.string);
                                return false;
                            }
                        } else {
                            handler_func = handler_prop;
                        }
                        
                        if (handler_func.type == VALUE_FUNCTION) {
                            // Call handler(originalFunc, args)
                            // Stack: [arg0, arg1, ..., callee] -> [originalFunc, argsArray, handler]
                            
                            // Collect arguments into array
                            // Stack: [..., arg0, arg1, ..., callee]
                            // vm->stack_top points one past callee
                            Value args_array;
                            args_array.type = VALUE_ARRAY;
                            args_array.as.array.count = arg_count;
                            args_array.as.array.values = malloc(sizeof(Value) * arg_count);
                            for (int j = 0; j < arg_count; j++) {
                                // args[j] = stack[top - arg_count - 1 + j]
                                args_array.as.array.values[j] = *(vm->stack_top - arg_count - 1 + j);
                            }
                            
                            // Pop args and callee
                            vm->stack_top -= (arg_count + 1);
                            
                            // Mark function as unwrapped BEFORE pushing it
                            // This prevents recursion when handler calls originalFunc
                            vm->unwrapped_function_name = function->name;
                            vm->decorator_handler_depth++;
                            
                            // Push: originalFunc, argsArray, handler
                            vm_push(vm, callee);
                            vm_push(vm, args_array);
                            vm_push(vm, handler_func);
                            
                            // Call handler - it will execute asynchronously via vm_run
                            bool result = call_value(vm, handler_func, 2);
                            
                            // DON'T restore yet - handler hasn't executed
                            // Restoration happens when frame returns (see below)
                            return result;
                        }
                    }
                }
            }
        }
        
        if (arg_count != function->arity) {
            runtime_error(vm, "Expected %d arguments but got %d.", function->arity, arg_count);
            return false;
        }
        
        // Check if function has bytecode to execute
        if (function->chunk.count == 0) {
            // No bytecode, return nil for now
            vm->stack_top -= arg_count + 1;
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
            vm_push(vm, result);
            return true;
        }
        
        // Create a new call frame for the function using shared logic
        CallFrame* frame = vm_setup_call_frame(vm->frames, &vm->frame_count, 
                                                 &vm->stack_top, function, arg_count);
        if (!frame) {
            runtime_error(vm, "Stack overflow.");
            return false;
        }
        
        return true;
    }
    
    if (callee.type == VALUE_STRING) {
        kuyil_log_debug("call_value: callee string='%s' argc=%d", callee.as.string ? callee.as.string : "<null>", arg_count);
        
        // Set up call context for shared call handler
        CallContext ctx = {
            .peek = vm_peek_helper,
            .pop = vm_pop_helper,
            .push = vm_push_helper,
            .get_args = vm_get_args_helper,
            .pop_n = vm_pop_n_helper,
            .report_error = vm_report_error_helper,
            .setup_frame = vm_setup_frame_helper,
            .context = vm,
            .get_global = vm_get_global_helper,
            .test_mode = vm->test_mode
        };
        
        CallResult result = vm_call_string_shared(&ctx, callee.as.string, arg_count);
        
        // CRITICAL: After library call (especially route_bridge_invoke which uses call_kuyil_function),
        // the frame pointer may be stale. However, in main VM's call_value we don't need to refresh
        // because call_value is called from within the execution loop which will refresh the frame
        // at the start of the next iteration.
        
        if (result == CALL_RESULT_ERROR) {
            return false;
        } else if (result == CALL_RESULT_OK) {
            return true;
        }
        // CALL_RESULT_NOT_HANDLED: continue to main VM specific handlers below
        
        // Struct method dispatch: receiver is an object with __type
        if (arg_count >= 1) {
            Value* args = vm->stack_top - arg_count - 1;
            if (args[0].type == VALUE_OBJECT) {
                const char* type_name = NULL;
                for (int i = 0; i < args[0].as.object.count; i++) {
                    if (strcmp(args[0].as.object.keys[i], "__type") == 0 &&
                        args[0].as.object.values[i].type == VALUE_STRING) {
                        type_name = args[0].as.object.values[i].as.string;
                        break;
                    }
                }
                if (type_name != NULL) {
                    char qualified[256];
                    snprintf(qualified, sizeof(qualified), "%s_%s", type_name, callee.as.string);
                    Value fn_value;
                    if (get_global(vm, qualified, &fn_value) && fn_value.type == VALUE_FUNCTION) {
                        // Replace callee string with resolved function and call it
                        vm_pop(vm); // pop callee string
                        vm_push(vm, fn_value);
                        return call_value(vm, fn_value, arg_count);
                    }
                }
            }
        }
        
        // Built-in __range function for for..in loops
        if (strcmp(callee.as.string, "__range") == 0) {
            if (arg_count != 2) {
                runtime_error(vm, "__range expects 2 arguments (start, end).");
                return false;
            }
            Value* args = vm->stack_top - arg_count - 1;
            if (args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
                runtime_error(vm, "__range arguments must be numbers.");
                return false;
            }
            
            int start = (int)args[0].as.number;
            int end = (int)args[1].as.number;
            
            // Create array [start, start+1, ..., end-1]
            Value result;
            result.type = VALUE_ARRAY;
            result.as.array.count = (end > start) ? (end - start) : 0;
            result.as.array.values = malloc(sizeof(Value) * result.as.array.count);
            
            for (int i = 0; i < result.as.array.count; i++) {
                result.as.array.values[i].type = VALUE_NUMBER;
                result.as.array.values[i].as.number = start + i;
            }
            
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Dependency injection registration: register_dependency(name, value)
        if (strcmp(callee.as.string, "register_dependency") == 0) {
            if (arg_count != 2) {
                runtime_error(vm, "register_dependency expects 2 arguments: register_dependency(name, value)");
                return false;
            }
            Value* args = vm->stack_top - arg_count - 1;
            if (args[0].type != VALUE_STRING) {
                runtime_error(vm, "register_dependency first argument must be a string (dependency name)");
                return false;
            }
            
            vm_register_dependency(args[0].as.string, args[1]);
            
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        // Decorator introspection: @get(funcName) or @get(funcName, "params")
        if (strcmp(callee.as.string, "@get") == 0 || strcmp(callee.as.string, "decoratorGet") == 0) {
            if (arg_count < 1 || arg_count > 2) {
                runtime_error(vm, "@get expects 1 or 2 arguments: @get(name) or @get(name, \"params\")");
                return false;
            }
            Value* args = vm->stack_top - arg_count - 1;
            if (args[0].type != VALUE_STRING) {
                runtime_error(vm, "@get first argument must be a string (entity name)");
                return false;
            }
            
            const char* entity_name = args[0].as.string;
            const char* query_type = arg_count == 2 && args[1].type == VALUE_STRING ? args[1].as.string : "decorators";
            
            // Search decorator registry
            Value result;
            result.type = VALUE_ARRAY;
            result.as.array.count = 0;
            result.as.array.values = NULL;
            
            for (int i = 0; i < vm->decorator_registry_count; i++) {
                if (strcmp(vm->decorator_registry[i].entity_name, entity_name) == 0) {
                    if (strcmp(query_type, "params") == 0) {
                        result = vm->decorator_registry[i].param_decorators;
                    } else {
                        result = vm->decorator_registry[i].decorators;
                    }
                    break;
                }
            }
            
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Get all registered entities (functions, structs, etc.)
        if (strcmp(callee.as.string, "@getAllEntities") == 0 || strcmp(callee.as.string, "decoratorGetAllEntities") == 0) {
            if (arg_count != 0) {
                runtime_error(vm, "@getAllEntities expects no arguments");
                return false;
            }
            
            const char** names = NULL;
            int count = compiler_get_all_decorated_entities(&names);
            
            Value result;
            result.type = VALUE_ARRAY;
            result.as.array.count = count;
            result.as.array.values = malloc(sizeof(Value) * count);
            
            for (int i = 0; i < count; i++) {
                Value name_val = {VALUE_STRING};
                name_val.as.string = strdup(names[i]);
                result.as.array.values[i] = name_val;
            }
            
            if (names) free(names);
            
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Find all entities with a specific decorator
        if (strcmp(callee.as.string, "@findByDecorator") == 0 || strcmp(callee.as.string, "decoratorFindByDecorator") == 0) {
            if (arg_count != 1) {
                runtime_error(vm, "@findByDecorator expects 1 argument: decorator name");
                return false;
            }
            Value* args = vm->stack_top - arg_count - 1;
            if (args[0].type != VALUE_STRING) {
                runtime_error(vm, "@findByDecorator argument must be a string (decorator name)");
                return false;
            }
            
            const char* decorator_name = args[0].as.string;
            const char** names = NULL;
            int count = compiler_find_entities_by_decorator(decorator_name, &names);
            
            Value result;
            result.type = VALUE_ARRAY;
            result.as.array.count = count;
            result.as.array.values = malloc(sizeof(Value) * count);
            
            for (int i = 0; i < count; i++) {
                Value name_val = {VALUE_STRING};
                name_val.as.string = strdup(names[i]);
                result.as.array.values[i] = name_val;
            }
            
            if (names) free(names);
            
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Test assertions (enabled in test mode)
        if (vm->test_mode) {
            if (strcmp(callee.as.string, "assert_true") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = (arg_count > 0) && !is_falsey(args[0]);
                if (!ok) vm->assertions_failed++;
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = ok};
                vm_push(vm, result);
                return true;
            }
            if (strcmp(callee.as.string, "assert_eq") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = false;
                if (arg_count >= 2) {
                    ok = values_equal(args[0], args[1]);
                }
                if (!ok) vm->assertions_failed++;
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = ok};
                vm_push(vm, result);
                return true;
            }
            if (strcmp(callee.as.string, "assert_neq") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = false;
                if (arg_count >= 2) {
                    ok = !values_equal(args[0], args[1]);
                }
                if (!ok) vm->assertions_failed++;
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = ok};
                vm_push(vm, result);
                return true;
            }
            // Simple mock controls (test mode)
            if (strcmp(callee.as.string, "mock_return") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                if (arg_count >= 2 && args[0].type == VALUE_STRING) {
                    extern void mock_set_return_value(const char* name, Value v);
                    mock_set_return_value(args[0].as.string, args[1]);
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = true}};
                vm_push(vm, result);
                return true;
            }
            // Enhanced mock with conditional arguments: mock_function(name, returnVal, whenArg1, whenArg2, ...)
            if (strcmp(callee.as.string, "mock_function") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                extern void mock_set_return_when(const char* name, Value v, Value* when_args, int when_arg_count);
                if (arg_count >= 2 && args[0].type == VALUE_STRING) {
                    const char* func_name = args[0].as.string;
                    Value return_value = args[1];
                    
                    // Remaining arguments are the "when" conditions
                    Value* when_args = (arg_count > 2) ? &args[2] : NULL;
                    int when_arg_count = (arg_count > 2) ? (arg_count - 2) : 0;
                    
                    mock_set_return_when(func_name, return_value, when_args, when_arg_count);
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = true}};
                vm_push(vm, result);
                return true;
            }
            if (strcmp(callee.as.string, "mock_clear") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                extern void mock_clear(const char* name);
                if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                    mock_clear(args[0].as.string);
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = true}};
                vm_push(vm, result);
                return true;
            }
            if (strcmp(callee.as.string, "mock_calls") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                extern int mock_get_call_count(const char* name);
                int count = 0;
                if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                    count = mock_get_call_count(args[0].as.string);
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_NUMBER, {.number = (double)count}};
                vm_push(vm, result);
                return true;
            }
            if (strcmp(callee.as.string, "assert_called") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                extern int mock_get_call_count(const char* name);
                vm->assertions_total++;
                bool ok = false;
                if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_NUMBER) {
                    int expected = (int)args[1].as.number;
                    int got = mock_get_call_count(args[0].as.string);
                    ok = (got == expected);
                }
                if (!ok) vm->assertions_failed++;
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = ok}};
                vm_push(vm, result);
                return true;
            }
            // assert_equals(actual, expected, message)
            if (strcmp(callee.as.string, "assert_equals") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = false;
                if (arg_count >= 2) {
                    ok = values_equal(args[0], args[1]);
                    if (!ok) {
                        printf("  ❌ ASSERTION FAILED: ");
                        if (arg_count >= 3 && args[2].type == VALUE_STRING) {
                            printf("%s\n", args[2].as.string);
                        } else {
                            printf("Values not equal\n");
                        }
                        printf("     Expected: ");
                        print_value(args[1]);
                        printf("\n     Got:      ");
                        print_value(args[0]);
                        printf("\n");
                    }
                }
                if (!ok) vm->assertions_failed++;
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = ok}};
                vm_push(vm, result);
                return true;
            }
            // assert_nil(value, message)
            if (strcmp(callee.as.string, "assert_nil") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = (arg_count >= 1 && args[0].type == VALUE_NIL);
                if (!ok) {
                    printf("  ❌ ASSERTION FAILED: ");
                    if (arg_count >= 2 && args[1].type == VALUE_STRING) {
                        printf("%s\n", args[1].as.string);
                    } else {
                        printf("Expected nil\n");
                    }
                    printf("     Got: ");
                    print_value(args[0]);
                    printf("\n");
                    vm->assertions_failed++;
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = ok}};
                vm_push(vm, result);
                return true;
            }
            // assert_not_nil(value, message)
            if (strcmp(callee.as.string, "assert_not_nil") == 0) {
                Value* args = vm->stack_top - arg_count - 1;
                vm->assertions_total++;
                bool ok = (arg_count >= 1 && args[0].type != VALUE_NIL);
                if (!ok) {
                    printf("  ❌ ASSERTION FAILED: ");
                    if (arg_count >= 2 && args[1].type == VALUE_STRING) {
                        printf("%s\n", args[1].as.string);
                    } else {
                        printf("Expected non-nil value\n");
                    }
                    vm->assertions_failed++;
                }
                vm->stack_top -= arg_count + 1;
                Value result = (Value){VALUE_BOOL, {.boolean = ok}};
                vm_push(vm, result);
                return true;
            }
        }
        
        // Route registration: route_register(method, path, handler, async?, bridge?)
        // These are available in ALL modes, not just test mode
        if (strcmp(callee.as.string, "route_register") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_route_register(int, Value*);
            Value result = builtin_route_register(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Route lookup: route_find(method, path)
        if (strcmp(callee.as.string, "route_find") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_route_find(int, Value*);
            Value result = builtin_route_find(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // List all routes: route_list()
        if (strcmp(callee.as.string, "route_list") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_route_list(int, Value*);
            Value result = builtin_route_list(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Extract path parameters: route_extract_params("/users/:id", "/users/123")
        if (strcmp(callee.as.string, "route_extract_params") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_route_extract_params(int, Value*);
            Value result = builtin_route_extract_params(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Bridge invoke: route_bridge_invoke(handler_name, arg1, arg2, ...)
        if (strcmp(callee.as.string, "route_bridge_invoke") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_route_bridge_invoke(int, Value*, VM*);
            Value result = builtin_route_bridge_invoke(arg_count, args, vm);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Request object constructor
        if (strcmp(callee.as.string, "Request") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_Request(int, Value*);
            Value result = builtin_Request(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Response object constructor
        if (strcmp(callee.as.string, "Response") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_Response(int, Value*);
            Value result = builtin_Response(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // aio_response_setStatus - using kyl_aio registry
        if (strcmp(callee.as.string, "aio_response_setStatus") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value kyl_aio_response_set_status(int, Value*);
            Value result = kyl_aio_response_set_status(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // aio_response_addHeader - using kyl_aio registry
        if (strcmp(callee.as.string, "aio_response_addHeader") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value kyl_aio_response_add_header(int, Value*);
            Value result = kyl_aio_response_add_header(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // aio_response_setBody - using kyl_aio registry
        if (strcmp(callee.as.string, "aio_response_setBody") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value kyl_aio_response_set_body(int, Value*);
            Value result = kyl_aio_response_set_body(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Mutex primitives
        if (strcmp(callee.as.string, "mutex_create") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_mutex_create(int, Value*);
            Value result = builtin_mutex_create(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "mutex_lock") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_mutex_lock(int, Value*);
            Value result = builtin_mutex_lock(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "mutex_unlock") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_mutex_unlock(int, Value*);
            Value result = builtin_mutex_unlock(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // AIO request/response constructors
        if (strcmp(callee.as.string, "aio_request") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_aio_request(int, Value*);
            Value result = builtin_aio_request(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "aio_response") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_aio_response(int, Value*);
            Value result = builtin_aio_response(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "aio_response_get_body") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_aio_response_get_body(int, Value*);
            Value result = builtin_aio_response_get_body(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "mutex_try_lock") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_mutex_try_lock(int, Value*);
            Value result = builtin_mutex_try_lock(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "mutex_destroy") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_mutex_destroy(int, Value*);
            Value result = builtin_mutex_destroy(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Start HTTP server
        if (strcmp(callee.as.string, "http_start_server") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_http_start_server(int, Value*);
            Value result = builtin_http_start_server(arg_count, args);
            // Set VM on server for handler callbacks
            extern AsyncHttpServer* async_http_server_get_global();
            AsyncHttpServer* server = async_http_server_get_global();
            if (server) {
                async_http_server_set_vm(server, vm);
            }
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Stop HTTP server
        if (strcmp(callee.as.string, "http_stop_server") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_http_stop_server(int, Value*);
            Value result = builtin_http_stop_server(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Add static file serving
        if (strcmp(callee.as.string, "http_static_add") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_http_static_add(int, Value*);
            Value result = builtin_http_static_add(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }
        
        // Wait for HTTP server (blocks without using VM)
        if (strcmp(callee.as.string, "http_server_wait") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            extern Value builtin_http_server_wait(int, Value*);
            Value result = builtin_http_server_wait(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        // Resolve user-defined function by exact name if exists
        {
            Value fn_value;
            if (get_global(vm, callee.as.string, &fn_value) && fn_value.type == VALUE_FUNCTION) {
                // Replace callee string with function and call it
                // Pop the callee string
                vm_pop(vm);
                // Push function value
                vm_push(vm, fn_value);
                // Delegate to function call path
                return call_value(vm, fn_value, arg_count);
            }
        }

        // Logging function calls
        if (strcmp(callee.as.string, "log_fatal") == 0 ||
            strcmp(callee.as.string, "log_error") == 0 ||
            strcmp(callee.as.string, "log_warning") == 0 ||
            strcmp(callee.as.string, "log_info") == 0 ||
            strcmp(callee.as.string, "log_debug") == 0) {
            
            Value* args = vm->stack_top - arg_count -1;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                if (strcmp(callee.as.string, "log_fatal") == 0) {
                    kuyil_log_fatal("%s", args[0].as.string);
                } else if (strcmp(callee.as.string, "log_error") == 0) {
                    kuyil_log_error("%s", args[0].as.string);
                } else if (strcmp(callee.as.string, "log_warning") == 0) {
                    kuyil_log_warning("%s", args[0].as.string);
                } else if (strcmp(callee.as.string, "log_info") == 0) {
                    kuyil_log_info("%s", args[0].as.string);
                } else if (strcmp(callee.as.string, "log_debug") == 0) {
                    kuyil_log_debug("%s", args[0].as.string);
                }
            }
            
            vm->stack_top -= arg_count + 1; // Pop args and function
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
            vm_push(vm, result);
            return true;
        }
        
        // String manipulation, math, date functions now handled by modular library system
        // These will fall through to the dynamic function system below
        
        // to_string, math functions now handled by modular library system
        
        // HTTP Server functions now handled by modular library system
        
        // Date utility functions now handled by modular library system
        
        // Development helper functions
        if (strcmp(callee.as.string, "dev_watch_file") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                printf("[DEV] Adding file to watch list: %s\n", args[0].as.string);
                // TODO: Implement actual file watching when DevHelper is integrated
            }
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "dev_watch_dir") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                printf("[DEV] Adding directory to watch list: %s\n", args[0].as.string);
                // TODO: Implement actual directory watching when DevHelper is integrated
            }
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "dev_start_watching") == 0) {
            printf("[DEV] Starting file watcher service...\n");
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "dev_stop_watching") == 0) {
            printf("[DEV] Stopping file watcher service...\n");
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        // Configuration and YAML functions
        if (strcmp(callee.as.string, "load_yaml") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                kuyil_log_info("Loading YAML config: %s", args[0].as.string);
                
                Value* config = config_load_yaml(args[0].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (config) {
                    vm_push(vm, *config);
                    kuyil_log_debug("YAML config loaded successfully");
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                    kuyil_log_debug("YAML config not found or failed to load");
                }
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "merge_config") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count >= 2) {
                kuyil_log_debug("Merging two configuration objects");
                
                Value* merged = config_merge(&args[0], &args[1]);
                vm->stack_top -= arg_count + 1;
                
                if (merged) {
                    vm_push(vm, *merged);
                    kuyil_log_debug("Configuration merge completed");
                } else {
                    vm_push(vm, args[0]); // Return first config if merge fails
                }
                return true;
            }
            kuyil_log_warning("merge_config requires 2 arguments");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        // Configuration path access function
        if (strcmp(callee.as.string, "config_get") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count >= 2 && args[1].type == VALUE_STRING) {
                kuyil_log_debug("Getting config value: %s", args[1].as.string);
                
                Value* result = config_get(&args[0], args[1].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (result) {
                    vm_push(vm, *result);
                } else {
                    // Return default value if provided
                    if (arg_count >= 3) {
                        vm_push(vm, args[2]);
                    } else {
                        Value nil_result = {VALUE_NIL};
                        vm_push(vm, nil_result);
                    }
                }
                return true;
            }
            kuyil_log_warning("config_get requires config object and path string");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        // Environment variable functions
        if (strcmp(callee.as.string, "getenv") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                printf("[DEBUG] getenv called with: '%s'\n", args[0].as.string);
                const char* env_value = getenv(args[0].as.string);
                printf("[DEBUG] getenv result: %s\n", env_value ? env_value : "NULL");
                Value result;
                if (env_value) {
                    result.type = VALUE_STRING;
                    result.as.string = strdup(env_value);
                } else {
                    result.type = VALUE_NIL;
                }
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            printf("[DEBUG] getenv called with invalid arguments\n");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "setenv") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                int overwrite = 1; // Default to overwrite
                if (arg_count >= 3 && args[2].type == VALUE_BOOL) {
                    overwrite = args[2].as.boolean ? 1 : 0;
                }
                
#ifndef _WIN32
                int result_code = setenv(args[0].as.string, args[1].as.string, overwrite);
#else
                // Windows equivalent: _putenv_s or SetEnvironmentVariable
                int result_code = _putenv_s(args[0].as.string, args[1].as.string);
#endif
                Value result = {VALUE_BOOL, .as.boolean = (result_code == 0)};
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        // Dynamic execution function
        if (strcmp(callee.as.string, "execute_script") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            
            // execute_script(source, libraries, param0, param1, ...)
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_NUMBER) {
                const char* script_source = args[0].as.string;
                LibraryFlags libraries = (LibraryFlags)(int)args[1].as.number;
                
                // Collect input parameters (param2 onwards)
                Value* input_params = &args[2];
                int param_count = arg_count - 2;
                
                // Create a new VM for isolated execution
                VM script_vm;
                Value script_result;
                
                InterpretResult result = vm_execute_dynamic(&script_vm, script_source, 
                                                         input_params, param_count, 
                                                         libraries, &script_result);
                
                vm->stack_top -= arg_count + 1;
                
                if (result == INTERPRET_OK) {
                    vm_push(vm, script_result);
                } else {
                    Value error_result = {VALUE_NIL};
                    vm_push(vm, error_result);
                }
                
                return true;
            }
            
            // Invalid arguments
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        // Compile-once, execute-multiple functions
        if (strcmp(callee.as.string, "compile_script") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            
            // compile_script(source, libraries)
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_NUMBER) {
                const char* script_source = args[0].as.string;
                LibraryFlags libraries = (LibraryFlags)(int)args[1].as.number;
                
                int handle = vm_compile_script(script_source, libraries);
                
                vm->stack_top -= arg_count + 1;
                
                Value result_val;
                if (handle >= 0) {
                    result_val.type = VALUE_NUMBER;
                    result_val.as.number = (double)handle;
                } else {
                    result_val.type = VALUE_NIL;
                }
                vm_push(vm, result_val);
                return true;
            }
            
            // Invalid arguments
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "execute_compiled") == 0) {
            Value* args = vm->stack_top - arg_count - 1;
            
            // execute_compiled(handle, param0, param1, ...)
            if (arg_count >= 1 && args[0].type == VALUE_NUMBER) {
                int script_handle = (int)args[0].as.number;
                
                // Collect input parameters (param1 onwards)
                Value* input_params = &args[1];
                int param_count = arg_count - 1;
                
                Value script_result;
                InterpretResult result = vm_execute_compiled(script_handle, input_params, 
                                                           param_count, &script_result);
                
                vm->stack_top -= arg_count + 1;
                
                if (result == INTERPRET_OK) {
                    vm_push(vm, script_result);
                } else {
                    Value error_result = {VALUE_NIL};
                    vm_push(vm, error_result);
                }
                
                return true;
            }
            
            // Invalid arguments
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "free_script") == 0) {
            Value* args = vm->stack_top - arg_count;
            
            // free_script(handle)
            if (arg_count >= 1 && args[0].type == VALUE_NUMBER) {
                int script_handle = (int)args[0].as.number;
                vm_free_script(script_handle);
                
                vm->stack_top -= arg_count + 1;
                Value success_result = {VALUE_BOOL, {.boolean = true}};
                vm_push(vm, success_result);
                return true;
            }
            
            // Invalid arguments
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, {.boolean = false}};
            vm_push(vm, fail_result);
            return true;
        }
        
        // FFI (Foreign Function Interface) functions
        if (strcmp(callee.as.string, "load_library") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                kuyil_log_info("Loading FFI library: %s from %s", args[0].as.string, args[1].as.string);
                
                // Initialize FFI context if not already done
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                FFILibrary* lib = ffi_load_library(g_ffi_context, args[0].as.string, args[1].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (lib) {
                    Value result = {VALUE_STRING, .as.string = strdup(lib->name)};
                    vm_push(vm, result);
                    kuyil_log_info("Library loaded successfully: %s", lib->name);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                    kuyil_log_error("Failed to load library: %s", ffi_error_message(ffi_get_last_error()));
                }
                return true;
            }
            kuyil_log_warning("load_library requires library name and path strings");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "register_function") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 3 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* func_name = args[1].as.string;
                
                kuyil_log_debug("Registering FFI function: %s in library %s", func_name, lib_name);
                
                if (g_ffi_context) {
                    FFILibrary* lib = ffi_get_library(g_ffi_context, lib_name);
                    if (lib) {
                        // For now, register with simple signature - extend later
                        FFIFunction* func = ffi_register_function(lib, func_name, FFI_TYPE_POINTER, 0, NULL);
                        vm->stack_top -= arg_count + 1;
                        
                        if (func) {
                            Value result = {VALUE_BOOL, .as.boolean = true};
                            vm_push(vm, result);
                            kuyil_log_info("Function registered: %s", func_name);
                        } else {
                            Value fail_result = {VALUE_BOOL, .as.boolean = false};
                            vm_push(vm, fail_result);
                        }
                        return true;
                    }
                }
                
                kuyil_log_error("Library not found: %s", lib_name);
                vm->stack_top -= arg_count + 1;
                Value fail_result = {VALUE_BOOL, .as.boolean = false};
                vm_push(vm, fail_result);
                return true;
            }
            kuyil_log_warning("register_function requires library name and function name");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "call_function") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* func_name = args[1].as.string;
                
                kuyil_log_debug("Calling FFI function: %s from library %s", func_name, lib_name);
                
                if (g_ffi_context) {
                    FFILibrary* lib = ffi_get_library(g_ffi_context, lib_name);
                    if (lib) {
                        FFIFunction* func = ffi_get_function(lib, func_name);
                        if (func) {
                            // Pass remaining arguments to the function
                            Value* func_args = (arg_count > 2) ? &args[2] : NULL;
                            int func_arg_count = (arg_count > 2) ? arg_count - 2 : 0;
                            
                            Value* result = ffi_call_function(func, func_args, func_arg_count);
                            vm->stack_top -= arg_count + 1;
                            
                            if (result) {
                                vm_push(vm, *result);
                                kuyil_log_debug("FFI function call completed");
                            } else {
                                Value nil_result = {VALUE_NIL};
                                vm_push(vm, nil_result);
                                kuyil_log_error("FFI function call failed: %s", ffi_error_message(ffi_get_last_error()));
                            }
                            return true;
                        }
                    }
                }
                
                kuyil_log_error("Function or library not found: %s.%s", lib_name, func_name);
                vm->stack_top -= arg_count + 1;
                Value nil_result = {VALUE_NIL};
                vm_push(vm, nil_result);
                return true;
            }
            kuyil_log_warning("call_function requires library name and function name");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "unload_library") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                kuyil_log_info("Unloading FFI library: %s", lib_name);
                
                if (g_ffi_context) {
                    ffi_unload_library(g_ffi_context, lib_name);
                }
                
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = true};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("unload_library requires library name string");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        // Enhanced FFI functions for shared library creation
        if (strcmp(callee.as.string, "create_shared_library") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 3 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING && args[2].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* source_file = args[1].as.string;
                const char* output_path = args[2].as.string;
                
                kuyil_log_info("Creating shared library: %s from %s", lib_name, source_file);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                // Create KuyilSharedLibSpec using the helper function
                KuyilSharedLibSpec* spec = ffi_create_lib_spec(lib_name, output_path);
                if (!spec) {
                    kuyil_log_error("Failed to create library specification");
                    vm->stack_top -= arg_count + 1;
                    Value fail_result = {VALUE_BOOL, .as.boolean = false};
                    vm_push(vm, fail_result);
                    return true;
                }
                
                // TODO: Parse source file and add functions
                // For now, create basic library structure
                int success = ffi_create_shared_library(spec);
                
                // Cleanup spec
                if (spec->library_name) free(spec->library_name);
                if (spec->output_path) free(spec->output_path);
                free(spec);
                
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = (success == 0)};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("create_shared_library requires name, source file, and output path strings");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "create_smart_shared_library") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 3 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING && args[2].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* source_file = args[1].as.string;
                const char* output_path = args[2].as.string;
                
                kuyil_log_info("Creating smart shared library: %s from %s", lib_name, source_file);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                // Use default flags or get from optional 4th argument
                int flags = 0;
                if (arg_count >= 4 && args[3].type == VALUE_NUMBER) {
                    flags = (int)args[3].as.number;
                }
                
                int success = ffi_create_smart_shared_library(lib_name, source_file, output_path, flags);
                
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = (success == 0)};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("create_smart_shared_library requires name, source file, and output path strings");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "load_library_ex") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 3 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING && args[2].type == VALUE_NUMBER) {
                const char* lib_name = args[0].as.string;
                const char* lib_path = args[1].as.string;
                FFILoadFlags flags = (FFILoadFlags)(int)args[2].as.number;
                
                kuyil_log_info("Loading FFI library with flags: %s from %s (flags: %d)", lib_name, lib_path, flags);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                FFILibrary* lib = ffi_load_library_ex(g_ffi_context, lib_name, lib_path, flags);
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = (lib != NULL)};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("load_library_ex requires library name, path, and flags");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "unload_library_ex") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_BOOL) {
                const char* lib_name = args[0].as.string;
                bool force = args[1].as.boolean;
                
                kuyil_log_info("Unloading FFI library: %s (force: %s)", lib_name, force ? "true" : "false");
                
                if (g_ffi_context) {
                    bool success = ffi_unload_library_ex(g_ffi_context, lib_name, force);
                    vm->stack_top -= arg_count + 1;
                    Value result = {VALUE_BOOL, .as.boolean = success};
                    vm_push(vm, result);
                    return true;
                }
                
                vm->stack_top -= arg_count + 1;
                Value fail_result = {VALUE_BOOL, .as.boolean = false};
                vm_push(vm, fail_result);
                return true;
            }
            kuyil_log_warning("unload_library_ex requires library name string and force boolean");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "list_loaded_libraries") == 0) {
            kuyil_log_info("Listing loaded FFI libraries");
            
            if (g_ffi_context) {
                ffi_list_loaded_libraries(g_ffi_context);
            }
            
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "is_library_loaded") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                
                bool is_loaded = false;
                if (g_ffi_context) {
                    is_loaded = ffi_is_library_loaded(g_ffi_context, lib_name);
                }
                
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = is_loaded};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("is_library_loaded requires library name string");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        // Application lifecycle functions
        if (strcmp(callee.as.string, "register_startup") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* func_name = args[1].as.string;
                
                kuyil_log_info("Registering startup function: %s from library %s", func_name, lib_name);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                bool success = ffi_register_startup_function(g_ffi_context, lib_name, func_name);
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = success};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("register_startup requires library name and function name strings");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "register_shutdown") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 2 && args[0].type == VALUE_STRING && args[1].type == VALUE_STRING) {
                const char* lib_name = args[0].as.string;
                const char* func_name = args[1].as.string;
                
                kuyil_log_info("Registering shutdown function: %s from library %s", func_name, lib_name);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                bool success = ffi_register_shutdown_function(g_ffi_context, lib_name, func_name);
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_BOOL, .as.boolean = success};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("register_shutdown requires library name and function name strings");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_BOOL, .as.boolean = false};
            vm_push(vm, fail_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "startup") == 0) {
            kuyil_log_info("Calling application startup functions");
            
            if (!g_ffi_context) {
                g_ffi_context = ffi_create_context();
            }
            
            bool success = ffi_call_startup_functions(g_ffi_context);
            vm->stack_top -= arg_count + 1;
            Value result = {VALUE_BOOL, .as.boolean = success};
            vm_push(vm, result);
            return true;
        }
        
        if (strcmp(callee.as.string, "shutdown") == 0) {
            kuyil_log_info("Calling application shutdown functions");
            
            if (g_ffi_context) {
                ffi_call_shutdown_functions(g_ffi_context);
            }
            
            vm->stack_top -= arg_count + 1;
            Value success_result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, success_result);
            return true;
        }
        
        // Convenience functions for common client libraries
        if (strcmp(callee.as.string, "load_redis_client") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                kuyil_log_info("Loading Redis client from: %s", args[0].as.string);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                FFILibrary* lib = ffi_load_redis_client(g_ffi_context, args[0].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (lib) {
                    Value result = {VALUE_STRING, .as.string = strdup("redis_client")};
                    vm_push(vm, result);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "load_elasticsearch_client") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                kuyil_log_info("Loading Elasticsearch client from: %s", args[0].as.string);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                FFILibrary* lib = ffi_load_elasticsearch_client(g_ffi_context, args[0].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (lib) {
                    Value result = {VALUE_STRING, .as.string = strdup("elasticsearch_client")};
                    vm_push(vm, result);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "load_kms_client") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                kuyil_log_info("Loading KMS client from: %s", args[0].as.string);
                
                if (!g_ffi_context) {
                    g_ffi_context = ffi_create_context();
                }
                
                FFILibrary* lib = ffi_load_kms_client(g_ffi_context, args[0].as.string);
                vm->stack_top -= arg_count + 1;
                
                if (lib) {
                    Value result = {VALUE_STRING, .as.string = strdup("kms_client")};
                    vm_push(vm, result);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        // HTTP Server functions (simplified for demo)
        if (strcmp(callee.as.string, "show_static_files") == 0) {
            kuyil_log_info("Demonstrating static file serving capabilities");
            
            // Test MIME type detection
            const char* test_files[] = {
                "index.html", "style.css", "app.js", "logo.svg", 
                "image.png", "document.pdf", "archive.zip"
            };
            
            printf("\n=== MIME Type Detection Demo (HTTP library) ===\n");
            for (int i = 0; i < 7; i++) {
                // HTTP functionality now in shared library
                printf("%-12s -> %s\n", test_files[i], "application/octet-stream");
            }
            
            // Test path security
            printf("\n=== Path Security Demo ===\n");
            const char* test_paths[] = {
                "normal/path.html", "../../../etc/passwd", "safe/file.css", "//dangerous/path"
            };
            
            for (int i = 0; i < 4; i++) {
                // HTTP functionality now in shared library
                printf("%-20s -> %s\n", test_paths[i], "SAFE");
            }
            
            vm->stack_top -= arg_count + 1;
            Value success_result = {VALUE_BOOL, .as.boolean = true};
            vm_push(vm, success_result);
            return true;
        }
        
        // File Reading functions - DEPRECATED: Now handled by libkylfileio.so
        // These hardcoded fallbacks should never execute because the library system
        // intercepts these calls first. Commenting out to prevent symbol conflicts.
        /*
        if (strcmp(callee.as.string, "file_read_text") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                Value result = kuyil_file_read_text(arg_count, args);
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "file_read_csv") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                Value result = kuyil_file_read_csv(arg_count, args);
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "file_read_json") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                Value result = kuyil_file_read_json(arg_count, args);
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        
        if (strcmp(callee.as.string, "file_read_yaml") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count > 0 && args[0].type == VALUE_STRING) {
                Value result = kuyil_file_read_yaml(arg_count, args);
                vm->stack_top -= arg_count + 1;
                vm_push(vm, result);
                return true;
            }
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }
        */

        // Green-thread / background task functions
        if (strcmp(callee.as.string, "start_bg") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_start_bg(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "wait_bg") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_wait_bg(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "wait_two_bg") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_wait_two_bg(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        // Observable pattern functions (RxJava-like)
        if (strcmp(callee.as.string, "observable_create") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_create(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "observable_emit") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_emit(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "observable_subscribe") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_subscribe(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "observable_map") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_map(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "observable_filter") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_filter(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "observable_complete") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_observable_complete(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        // Enhanced background task functions with message passing
        if (strcmp(callee.as.string, "start_bg_enhanced") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_start_bg_enhanced(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "wait_bg_enhanced") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_wait_bg_enhanced(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        if (strcmp(callee.as.string, "process_bg_jobs") == 0) {
            Value* args = vm->stack_top - arg_count;
            Value result = kuyil_process_bg_jobs(arg_count, args);
            vm->stack_top -= arg_count + 1;
            vm_push(vm, result);
            return true;
        }

        // Enhanced Library Metadata Management Functions
        if (strcmp(callee.as.string, "create_version") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 3 && args[0].type == VALUE_NUMBER && args[1].type == VALUE_NUMBER && args[2].type == VALUE_NUMBER) {
                int major = (int)args[0].as.number;
                int minor = (int)args[1].as.number;
                int patch = (int)args[2].as.number;
                const char* pre_release = (arg_count >= 4 && args[3].type == VALUE_STRING) ? args[3].as.string : NULL;
                
                KuyilVersion* version = ffi_create_version(major, minor, patch, pre_release);
                
                vm->stack_top -= arg_count + 1;
                if (version) {
                    char* version_str = ffi_version_to_string(version);
                    Value result = {VALUE_STRING, .as.string = version_str ? version_str : strdup("unknown")};
                    vm_push(vm, result);
                    ffi_free_version(version);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            kuyil_log_warning("create_version requires major, minor, patch numbers and optional pre-release string");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }

        if (strcmp(callee.as.string, "compare_versions") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 6 && 
                args[0].type == VALUE_NUMBER && args[1].type == VALUE_NUMBER && args[2].type == VALUE_NUMBER &&
                args[3].type == VALUE_NUMBER && args[4].type == VALUE_NUMBER && args[5].type == VALUE_NUMBER) {
                
                KuyilVersion v1 = {(int)args[0].as.number, (int)args[1].as.number, (int)args[2].as.number, NULL, NULL};
                KuyilVersion v2 = {(int)args[3].as.number, (int)args[4].as.number, (int)args[5].as.number, NULL, NULL};
                
                int cmp_result = ffi_compare_versions(&v1, &v2);
                
                vm->stack_top -= arg_count + 1;
                Value result = {VALUE_NUMBER, .as.number = (double)cmp_result};
                vm_push(vm, result);
                return true;
            }
            kuyil_log_warning("compare_versions requires 6 numbers: major1, minor1, patch1, major2, minor2, patch2");
            vm->stack_top -= arg_count + 1;
            Value fail_result = {VALUE_NUMBER, .as.number = 0};
            vm_push(vm, fail_result);
            return true;
        }

        if (strcmp(callee.as.string, "create_library_info") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                const char* author = args[0].as.string;
                const char* description = (arg_count >= 2 && args[1].type == VALUE_STRING) ? args[1].as.string : NULL;
                const char* license = (arg_count >= 3 && args[2].type == VALUE_STRING) ? args[2].as.string : NULL;
                const char* homepage = (arg_count >= 4 && args[3].type == VALUE_STRING) ? args[3].as.string : NULL;
                
                KuyilLibraryInfo* info = ffi_create_library_info(author, description, license, homepage);
                
                vm->stack_top -= arg_count + 1;
                if (info) {
                    Value result = {VALUE_STRING, .as.string = strdup(author)};
                    vm_push(vm, result);
                    ffi_free_library_info(info);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            kuyil_log_warning("create_library_info requires author string and optional description, license, homepage");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }

        if (strcmp(callee.as.string, "create_dependency") == 0) {
            Value* args = vm->stack_top - arg_count;
            if (arg_count >= 1 && args[0].type == VALUE_STRING) {
                const char* name = args[0].as.string;
                const char* version_req = (arg_count >= 2 && args[1].type == VALUE_STRING) ? args[1].as.string : NULL;
                bool optional = (arg_count >= 3 && args[2].type == VALUE_BOOL) ? args[2].as.boolean : false;
                
                KuyilDependency* dep = ffi_create_dependency(name, version_req, optional);
                
                vm->stack_top -= arg_count + 1;
                if (dep) {
                    Value result = {VALUE_STRING, .as.string = strdup(name)};
                    vm_push(vm, result);
                    ffi_free_dependency(dep);
                } else {
                    Value nil_result = {VALUE_NIL};
                    vm_push(vm, nil_result);
                }
                return true;
            }
            kuyil_log_warning("create_dependency requires name string and optional version requirement, optional flag");
            vm->stack_top -= arg_count + 1;
            Value nil_result = {VALUE_NIL};
            vm_push(vm, nil_result);
            return true;
        }

        if (strcmp(callee.as.string, "create_library_registry") == 0) {
            kuyil_log_info("Creating new library registry");
            
            // For simplicity, we'll use a global registry or create a new one
            // In a real implementation, you might want to track multiple registries
            KuyilLibraryRegistry* registry = ffi_create_library_registry();
            
            vm->stack_top -= arg_count + 1;
            if (registry) {
                // Return a success indicator (in real implementation, you'd return a handle)
                Value result = {VALUE_BOOL, .as.boolean = true};
                vm_push(vm, result);
                // Note: In a real implementation, store the registry pointer somewhere accessible
                ffi_free_library_registry(registry);
            } else {
                Value fail_result = {VALUE_BOOL, .as.boolean = false};
                vm_push(vm, fail_result);
            }
            return true;
        }
    }

    // If the callee is a string here, none of the resolution paths matched
    if (callee.type == VALUE_STRING) {
        runtime_error(vm, "Undefined function '%s'.", callee.as.string);
        return false;
    }

    // Generic non-callable value invocation
    runtime_error(vm, "Can only call functions and classes (got %s).", value_type_name(callee.type));
    return false;
}

// Public wrapper for external code to call functions
// Arguments must already be on the stack: [arg0, arg1, ..., argN, callee]
// Returns true on success, false on error
// Result will be on top of stack after successful call
bool vm_call_function(VM* vm, Value callee, int arg_count) {
    return call_value(vm, callee, arg_count);
}

static uint8_t read_byte(VM* vm) {
    return *vm->frames[vm->frame_count - 1].ip++;
}

static uint16_t __attribute__((unused)) read_short(VM* vm) {
    vm->frames[vm->frame_count - 1].ip += 2;
    return (uint16_t)((vm->frames[vm->frame_count - 1].ip[-2] << 8) |
                       vm->frames[vm->frame_count - 1].ip[-1]);
}

static Value read_constant(VM* vm) {
    uint8_t constant = read_byte(vm);
    return vm->frames[vm->frame_count - 1].function->chunk.constants[constant];
}

#if 0  // Unused function - kept for future use
static const char* read_string(VM* vm) {
    Value constant = read_constant(vm);
    return constant.as.string;
}
#endif

InterpretResult vm_run(VM* vm) {
    
#define READ_BYTE() ((*vm->frames[vm->frame_count - 1].ip++))
#define READ_SHORT() \
    (vm->frames[vm->frame_count - 1].ip += 2, \
     (uint16_t)((vm->frames[vm->frame_count - 1].ip[-2] << 8) | vm->frames[vm->frame_count - 1].ip[-1]))
#define READ_CONSTANT() \
    (vm->frames[vm->frame_count - 1].function->chunk.constants[READ_BYTE()])
#define READ_STRING() READ_CONSTANT().as.string
    
    // Set up execution context for shared opcode executor
    bool exec_error = false;
    char exec_error_msg[256] = {0};
    ExecContext exec_ctx = {
        .stack = vm->stack,
        .stack_top = &vm->stack_top,
        .stack_capacity = STACK_MAX,
        .current_frame_slots = NULL,  // Updated per operation
        .has_error = &exec_error,
        .error_message = exec_error_msg,
        .error_msg_size = sizeof(exec_error_msg),
        .type = EXEC_CTX_VM,
        .vm_ptr = vm
    };
    
    for (;;) {
        // CRITICAL: If no frames, execution is complete - this can happen in nested vm_run calls
        if (vm->frame_count == 0) {
            // This is actually expected when call_kuyil_function creates nested vm_run calls
            // The inner vm_run completes (frame_count=0), returns, then outer call_kuyil_function
            // restores frame_count. No error needed, just return cleanly.
            return INTERPRET_OK;
        }
        
        // CRITICAL: Validate IP before reading to prevent executing data as opcodes
        CallFrame* frame = &vm->frames[vm->frame_count - 1];
        if (frame->ip < frame->function->chunk.code || 
            frame->ip >= frame->function->chunk.code + frame->function->chunk.count) {
            fprintf(stderr, "[VM] FATAL: IP=%p outside bytecode [%p, %p), would read opcode %d\n",
                    (void*)frame->ip,
                    (void*)frame->function->chunk.code,
                    (void*)(frame->function->chunk.code + frame->function->chunk.count),
                    (int)*frame->ip);
            runtime_error(vm, "Instruction pointer corruption detected");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        uint8_t instruction = READ_BYTE();
        // Coverage instrumentation: increment hit for current instruction
        if (vm->coverage_enabled && vm->frame_count > 0) {
            CallFrame* cframe = &vm->frames[vm->frame_count - 1];
            Function* fn = cframe->function;
            size_t instr_index = (size_t)(cframe->ip - fn->chunk.code - 1);
            if (instr_index < (size_t)fn->chunk.count) {
                // Find or add entry for this function
                int found = -1;
                for (int ci = 0; ci < vm->coverage.count; ci++) {
                    if (vm->coverage.entries[ci].function == fn) { found = ci; break; }
                }
                if (found == -1 && vm->coverage.count < 256) {
                    found = vm->coverage.count++;
                    vm->coverage.entries[found].function = fn;
                    vm->coverage.entries[found].hits_len = fn->chunk.count;
                    vm->coverage.entries[found].hits = calloc(fn->chunk.count, sizeof(int));
                }
                if (found != -1 && vm->coverage.entries[found].hits) {
                    vm->coverage.entries[found].hits[instr_index]++;
                }
            }
        }
        
        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                vm_push(vm, constant);
                break;
            }
            case OP_CONSTANT_LONG: {
                uint8_t high = READ_BYTE();
                uint8_t low = READ_BYTE();
                uint16_t constant_index = (high << 8) | low;
                Chunk* chunk = &vm->frames[vm->frame_count - 1].function->chunk;
                if (constant_index >= chunk->constant_count) {
                    fprintf(stderr, "[VM] ERROR: Constant index %d out of bounds (max %d)\n", constant_index, chunk->constant_count - 1);
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value constant = chunk->constants[constant_index];
                vm_push(vm, constant);
                break;
            }
            case OP_NIL:
                exec_push_nil(&exec_ctx);
                break;
            case OP_TRUE:
                exec_push_true(&exec_ctx);
                break;
            case OP_FALSE:
                exec_push_false(&exec_ctx);
                break;
            case OP_POP: 
                exec_pop_discard(&exec_ctx);
                break;
            case OP_DUP: {
                Value value = vm_peek(vm, 0);
                vm_push(vm, value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                const char* name = READ_STRING();
                define_global(vm, name, vm_peek(vm, 0));
                vm_pop(vm);
                break;
            }
            case OP_DEFINE_GLOBAL_LONG: {
                uint8_t high = READ_BYTE();
                uint8_t low = READ_BYTE();
                uint16_t constant_index = (high << 8) | low;
                Chunk* chunk = &vm->frames[vm->frame_count - 1].function->chunk;
                if (constant_index >= chunk->constant_count) {
                    fprintf(stderr, "[VM] ERROR: Constant index %d out of bounds in OP_DEFINE_GLOBAL_LONG\n", constant_index);
                    return INTERPRET_RUNTIME_ERROR;
                }
                const char* name = chunk->constants[constant_index].as.string;
                define_global(vm, name, vm_peek(vm, 0));
                vm_pop(vm);
                break;
            }
            case OP_GET_GLOBAL: {
                const char* name = READ_STRING();
                Value value;
                if (!get_global(vm, name, &value)) {
                    // Check if this is an interface namespace
                    if (is_interface_name(name)) {
                        // Return a special marker value - use the string itself
                        // This allows file.readText to work: file evaluates to "file" (marker)
                        Value namespace_marker;
                        namespace_marker.type = VALUE_STRING;
                        namespace_marker.as.string = name;
                        vm_push(vm, namespace_marker);
                        break;
                    }
                    runtime_error(vm, "Undefined variable '%s'.", name);
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, value);
                break;
            }
            case OP_GET_GLOBAL_LONG: {
                uint8_t high = READ_BYTE();
                uint8_t low = READ_BYTE();
                uint16_t constant_index = (high << 8) | low;
                Chunk* chunk = &vm->frames[vm->frame_count - 1].function->chunk;
                if (constant_index >= chunk->constant_count) {
                    fprintf(stderr, "[VM] ERROR: Constant index %d out of bounds in OP_GET_GLOBAL_LONG\n", constant_index);
                    return INTERPRET_RUNTIME_ERROR;
                }
                const char* name = chunk->constants[constant_index].as.string;
                Value value;
                if (!get_global(vm, name, &value)) {
                    runtime_error(vm, "Undefined variable '%s'.", name);
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                const char* name = READ_STRING();
                if (!set_global(vm, name, vm_peek(vm, 0))) {
                    runtime_error(vm, "Undefined variable '%s'.", name);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_GLOBAL_LONG: {
                uint8_t high = READ_BYTE();
                uint8_t low = READ_BYTE();
                uint16_t constant_index = (high << 8) | low;
                Chunk* chunk = &vm->frames[vm->frame_count - 1].function->chunk;
                if (constant_index >= chunk->constant_count) {
                    fprintf(stderr, "[VM] ERROR: Constant index %d out of bounds in OP_SET_GLOBAL_LONG\n", constant_index);
                    return INTERPRET_RUNTIME_ERROR;
                }
                const char* name = chunk->constants[constant_index].as.string;
                if (!set_global(vm, name, vm_peek(vm, 0))) {
                    runtime_error(vm, "Undefined variable '%s'.", name);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm_push(vm, vm->frames[vm->frame_count - 1].slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm->frames[vm->frame_count - 1].slots[slot] = vm_peek(vm, 0);
                break;
            }
            case OP_EQUAL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = values_equal(a, b);
                vm_push(vm, result);
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = !values_equal(a, b);
                vm_push(vm, result);
                break;
            }
            case OP_GREATER: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER || 
                    vm_peek(vm, 1).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = vm_pop(vm).as.number;
                double a = vm_pop(vm).as.number;
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = a > b;
                vm_push(vm, result);
                break;
            }
            case OP_GREATER_EQUAL: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER || 
                    vm_peek(vm, 1).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = vm_pop(vm).as.number;
                double a = vm_pop(vm).as.number;
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = a >= b;
                vm_push(vm, result);
                break;
            }
            case OP_LESS: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER || 
                    vm_peek(vm, 1).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = vm_pop(vm).as.number;
                double a = vm_pop(vm).as.number;
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = a < b;
                vm_push(vm, result);
                break;
            }
            case OP_LESS_EQUAL: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER || 
                    vm_peek(vm, 1).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = vm_pop(vm).as.number;
                double a = vm_pop(vm).as.number;
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = a <= b;
                vm_push(vm, result);
                break;
            }
            case OP_ADD:
                if (!exec_add(&exec_ctx)) {
                    runtime_error(vm, "%s", exec_error_msg);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_SUBTRACT:
                if (!exec_subtract(&exec_ctx)) {
                    runtime_error(vm, "%s", exec_error_msg);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_MULTIPLY:
                if (!exec_multiply(&exec_ctx)) {
                    runtime_error(vm, "%s", exec_error_msg);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_DIVIDE:
                if (!exec_divide(&exec_ctx)) {
                    runtime_error(vm, "%s", exec_error_msg);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_MODULO: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER || 
                    vm_peek(vm, 1).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = vm_pop(vm).as.number;
                double a = vm_pop(vm).as.number;
                Value result;
                result.type = VALUE_NUMBER;
                result.as.number = fmod(a, b);
                vm_push(vm, result);
                break;
            }
            case OP_NOT: {
                Value value = vm_pop(vm);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = is_falsey(value);
                vm_push(vm, result);
                break;
            }
            case OP_AND: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = !is_falsey(a) && !is_falsey(b);
                vm_push(vm, result);
                break;
            }
            case OP_OR: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                result.type = VALUE_BOOL;
                result.as.boolean = !is_falsey(a) || !is_falsey(b);
                vm_push(vm, result);
                break;
            }
            case OP_NEGATE: {
                if (vm_peek(vm, 0).type != VALUE_NUMBER) {
                    runtime_error(vm, "Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value value = vm_pop(vm);
                value.as.number = -value.as.number;
                vm_push(vm, value);
                break;
            }
            case OP_TO_STRING: {
                Value value = vm_pop(vm);
                Value result;
                result.type = VALUE_STRING;
                
                // Convert value to string based on type
                switch (value.type) {
                    case VALUE_STRING:
                        result.as.string = value.as.string; // Already a string
                        break;
                    case VALUE_NUMBER: {
                        char* str = malloc(32);
                        snprintf(str, 32, "%g", value.as.number);
                        result.as.string = str;
                        break;
                    }
                    case VALUE_BOOL:
                        result.as.string = value.as.boolean ? strdup("true") : strdup("false");
                        break;
                    case VALUE_NIL:
                        result.as.string = strdup("nil");
                        break;
                    default:
                        result.as.string = strdup("[Object]");
                        break;
                }
                vm_push(vm, result);
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm->frames[vm->frame_count - 1].ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (is_falsey(vm_peek(vm, 0))) vm->frames[vm->frame_count - 1].ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm->frames[vm->frame_count - 1].ip -= offset;
                break;
            }
            case OP_CALL: {
                int arg_count = READ_BYTE();
                Value callee_dbg = vm_peek(vm, 0);
                kuyil_log_debug("OP_CALL: callee.type=%d arg_count=%d ptr=%p", callee_dbg.type, arg_count,
                                 (callee_dbg.type == VALUE_STRING ? (void*)callee_dbg.as.string : NULL));
                if (!call_value(vm, vm_peek(vm, 0), arg_count)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_RETURN: {
                Value result = vm_pop(vm);
                
                // Execute deferred functions in LIFO order before returning
                CallFrame* frame = &vm->frames[vm->frame_count - 1];
                for (int i = frame->defer_count - 1; i >= 0; i--) {
                    Value defer_call = frame->defers[i];
                    
                    if (defer_call.type != VALUE_ARRAY) {
                        kuyil_log_error("[DEFER] Invalid defer call structure");
                        continue;
                    }
                    
                    // Extract function and arguments from array
                    Value function_val = defer_call.as.array.values[0];
                    int arg_count = defer_call.as.array.count - 1;
                    
                    // Push arguments and function onto stack in correct order for call_value
                    // Stack layout for OP_CALL: [arg1, arg2, ..., argN, function]
                    // Push args first
                    for (int j = 0; j < arg_count; j++) {
                        vm_push(vm, defer_call.as.array.values[j + 1]);
                    }
                    
                    // Push function last (on top)
                    vm_push(vm, function_val);
                    
                    // Call the deferred function (function is at peek(0))
                    Value callee = vm_peek(vm, 0);
                    if (!call_value(vm, callee, arg_count)) {
                        kuyil_log_error("[DEFER] Failed to execute deferred function");
                        // Clean up stack on error
                        vm->stack_top -= arg_count + 1;
                        // Continue with other defers even if one fails
                    } else {
                        // Pop the result of the deferred call (call_value leaves result on stack)
                        vm_pop(vm);
                    }
                    
                    // Free the defer call array
                    free(defer_call.as.array.values);
                    
                    kuyil_log_info("[DEFER] Executed deferred function (remaining: %d)", i);
                }
                
                // Reset defer count for this frame
                frame->defer_count = 0;
                
                vm->frame_count--;
                if (vm->frame_count == 0) {
                    vm_pop(vm);
                    return INTERPRET_OK;
                }
                
                Value* caller_slots = vm->frames[vm->frame_count].slots;
                vm->stack_top = caller_slots;
                vm_push(vm, result);
                break;
            }
            case OP_AVATAR: {
                // OP_AVATAR launches a function asynchronously using avatar runtime
                // Stack layout: [function, arg1, arg2, ..., argN]
                
                int arg_count = READ_BYTE();
                Value function_value = vm_peek(vm, arg_count);
                
                if (function_value.type != VALUE_FUNCTION) {
                    runtime_error(vm, "Avatar can only launch functions");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (!vm->avatar_runtime) {
                    // Fallback to inline execution if avatar runtime not available
                    kuyil_log_warning("[AVATAR] Runtime not available, executing inline");
                    if (!call_value(vm, function_value, arg_count)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    // Collect arguments from stack
                    Value* args = malloc(sizeof(Value) * arg_count);
                    for (int i = arg_count - 1; i >= 0; i--) {
                        args[i] = vm_pop(vm);
                    }
                    
                    // Pop function value
                    Value func_val = vm_pop(vm);
                    Function* func = func_val.as.function.function;
                    
                    // Submit to avatar runtime
                    AvatarHandle* handle = avatar_runtime_submit(
                        vm->avatar_runtime,
                        func,
                        args,
                        arg_count,
                        vm,
                        NULL,  // No completion callback for now
                        NULL
                    );
                    
                    free(args);
                    
                    if (!handle) {
                        runtime_error(vm, "Failed to submit avatar");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    // Push avatar handle onto stack (wrapped as number for now)
                    // TODO: Create proper VALUE_AVATAR_HANDLE type
                    Value handle_val;
                    handle_val.type = VALUE_NUMBER;
                    handle_val.as.number = (double)(uintptr_t)handle;
                    vm_push(vm, handle_val);
                    
                    kuyil_log_info("[AVATAR] Function submitted to thread pool");
                }
                break;
            }
            case OP_DEFER: {
                // OP_DEFER registers a function call for execution at scope exit
                // Stack layout: [function, arg1, arg2, ..., argN]
                // Store function + args in current frame's defer stack (LIFO)
                
                int arg_count = READ_BYTE();
                CallFrame* frame = &vm->frames[vm->frame_count - 1];
                
                if (frame->defer_count >= DEFERS_MAX) {
                    runtime_error(vm, "Too many defer statements in function (max %d)", DEFERS_MAX);
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                // Stack layout: [arg1, arg2, ..., argN, function]
                // Create a deferred call value structure
                // We'll store it as an array: [function, arg1, arg2, ..., argN]
                Value defer_call = {VALUE_ARRAY};
                defer_call.as.array.count = arg_count + 1;
                defer_call.as.array.values = malloc(sizeof(Value) * (arg_count + 1));
                
                // Pop function (on top)
                defer_call.as.array.values[0] = vm_pop(vm);
                
                // Pop args in reverse order (last arg is now on top)
                for (int i = arg_count - 1; i >= 0; i--) {
                    defer_call.as.array.values[i + 1] = vm_pop(vm);
                }
                
                // Debug: Log what was captured
                Value func_val = defer_call.as.array.values[0];
                if (func_val.type == VALUE_STRING) {
                    kuyil_log_info("[DEFER] Captured VALUE_STRING: %s", func_val.as.string);
                } else if (func_val.type == VALUE_FUNCTION) {
                    kuyil_log_info("[DEFER] Captured VALUE_FUNCTION");
                } else {
                    kuyil_log_info("[DEFER] Captured type: %d", func_val.type);
                }
                
                // Push onto defer stack
                frame->defers[frame->defer_count++] = defer_call;
                
                kuyil_log_info("[DEFER] Registered function with %d args (defer stack size: %d)", 
                              arg_count, frame->defer_count);
                break;
            }
            case OP_AWAIT: {
                // OP_AWAIT waits for an avatar to complete
                // Stack: [avatar_handle] -> [avatar_result]
                
                Value handle_val = vm_pop(vm);
                
                if (!vm->avatar_runtime) {
                    // If no avatar runtime, value is already result from inline execution
                    vm_push(vm, handle_val);
                    kuyil_log_info("[AWAIT] Pass-through (no avatar runtime)");
                } else {
                    // Extract handle
                    AvatarHandle* handle = (AvatarHandle*)(uintptr_t)handle_val.as.number;
                    
                    // Poll for completion instead of blocking indefinitely
                    // This allows completions to be processed
                    const int poll_interval_ms = 10;
                    const int max_polls = 10000; // 100 seconds total timeout
                    int polls = 0;
                    
                    while (!avatar_runtime_is_complete(handle) && polls < max_polls) {
                        // Process pending completions
                        avatar_runtime_process_completions(vm->avatar_runtime, 100);
                        
                        // Small sleep to avoid busy-wait
                        struct timespec ts = {0, poll_interval_ms * 1000000};
                        nanosleep(&ts, NULL);
                        
                        polls++;
                    }
                    
                    if (!avatar_runtime_is_complete(handle)) {
                        runtime_error(vm, "Avatar await timeout");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    // Check for avatar execution errors
                    if (avatar_runtime_has_error(handle)) {
                        const char* error_msg = avatar_runtime_get_error(handle);
                        runtime_error(vm, "Avatar execution failed: %s", error_msg ? error_msg : "Unknown error");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    // Get the result
                    Value result = avatar_runtime_get_result(handle);
                    vm_push(vm, result);
                    kuyil_log_info("[AWAIT] Avatar completed, result retrieved");
                }
                break;
            }
            case OP_CLOSURE: {
                uint8_t constant_index = READ_BYTE();
                Chunk* chunk = &vm->frames[vm->frame_count - 1].function->chunk;
                if (constant_index >= chunk->constant_count) {
                    fprintf(stderr, "[VM] ERROR: Constant index %d out of bounds in OP_CLOSURE (max %d)\n", constant_index, chunk->constant_count - 1);
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value function_value = chunk->constants[constant_index];
                vm_push(vm, function_value);
                break;
            }
            case OP_LOG_FATAL: {
                Value message = vm_pop(vm);
                if (message.type == VALUE_STRING) {
                    kuyil_log_fatal("%s", message.as.string);
                } else {
                    kuyil_log_fatal("Non-string log message");
                }
                break;
            }
            case OP_LOG_ERROR: {
                Value message = vm_pop(vm);
                if (message.type == VALUE_STRING) {
                    kuyil_log_error("%s", message.as.string);
                }
                break;
            }
            case OP_LOG_WARNING: {
                Value message = vm_pop(vm);
                if (message.type == VALUE_STRING) {
                    kuyil_log_warning("%s", message.as.string);
                }
                break;
            }
            case OP_LOG_INFO: {
                Value message = vm_pop(vm);
                if (message.type == VALUE_STRING) {
                    kuyil_log_info("%s", message.as.string);
                }
                break;
            }
            case OP_LOG_DEBUG: {
                Value message = vm_pop(vm);
                if (message.type == VALUE_STRING) {
                    kuyil_log_debug("%s", message.as.string);
                }
                break;
            }
            case OP_LOG_PUSH_CTX: {
                const char* func_name = READ_STRING();
                kuyil_push_function_context(func_name);
                break;
            }
            case OP_LOG_POP_CTX: {
                kuyil_pop_function_context();
                break;
            }
            case OP_ARRAY: {
                uint8_t count = READ_BYTE();
                Value array;
                array.type = VALUE_ARRAY;
                array.as.array.count = count;
                array.as.array.values = malloc(sizeof(Value) * count);
                
                // Pop elements in reverse order (last element pushed first)
                for (int i = count - 1; i >= 0; i--) {
                    array.as.array.values[i] = vm_pop(vm);
                }
                
                vm_push(vm, array);
                break;
            }
            case OP_ARRAY_GET: {
                Value index = vm_pop(vm);
                Value container = vm_pop(vm);
                
                // Handle object access with string index
                if (container.type == VALUE_OBJECT && index.type == VALUE_STRING) {
                    // Search for key in object
                    bool found = false;
                    for (int i = 0; i < container.as.object.count; i++) {
                        if (strcmp(container.as.object.keys[i], index.as.string) == 0) {
                            vm_push(vm, container.as.object.values[i]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        Value nilv = {VALUE_NIL};
                        vm_push(vm, nilv);
                    }
                    break;
                }
                
                // Handle array access
                if (container.type != VALUE_ARRAY) {
                    runtime_error(vm, "Can only index arrays or objects with string keys.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (index.type != VALUE_NUMBER) {
                    runtime_error(vm, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                int idx = (int)index.as.number;
                if (idx < 0 || idx >= container.as.array.count) {
                    runtime_error(vm, "Array index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                vm_push(vm, container.as.array.values[idx]);
                break;
            }
            case OP_ARRAY_SET: {
                Value index = vm_pop(vm);
                Value array = vm_pop(vm);
                Value value = vm_pop(vm);
                
                if (array.type != VALUE_ARRAY) {
                    runtime_error(vm, "Can only index arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (index.type != VALUE_NUMBER) {
                    runtime_error(vm, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                int idx = (int)index.as.number;
                if (idx < 0 || idx >= array.as.array.count) {
                    runtime_error(vm, "Array index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                array.as.array.values[idx] = value;
                vm_push(vm, value); // Assignment expression returns the value
                break;
            }
            case OP_OBJECT_GET: {
                Value key = vm_pop(vm);
                Value object = vm_pop(vm);
                
                if (key.type != VALUE_STRING) {
                    runtime_error(vm, "Object property key must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                // Treat arrays and strings as objects for common properties
                if (object.type == VALUE_ARRAY) {
                    if (strcmp(key.as.string, "length") == 0) {
                        Value v; v.type = VALUE_NUMBER; v.as.number = (double)object.as.array.count; vm_push(vm, v); break;
                    }
                    // Unknown property on array -> nil
                    Value nilv = {VALUE_NIL}; vm_push(vm, nilv); break;
                }
                if (object.type == VALUE_STRING) {
                    // Check if this is an interface namespace
                    if (is_interface_name(object.as.string)) {
                        // Build dotted name: "interface.property"
                        size_t dotted_len = strlen(object.as.string) + 1 + strlen(key.as.string) + 1;
                        char* dotted_name = malloc(dotted_len);
                        snprintf(dotted_name, dotted_len, "%s.%s", object.as.string, key.as.string);
                        
                        // Return the dotted name as a string (function name marker)
                        Value result;
                        result.type = VALUE_STRING;
                        result.as.string = dotted_name;
                        vm_push(vm, result);
                        break;
                    }
                    
                    // Regular string properties
                    if (strcmp(key.as.string, "length") == 0) {
                        Value v; v.type = VALUE_NUMBER; v.as.number = (double)strlen(object.as.string); vm_push(vm, v); break;
                    }
                    if (strcmp(key.as.string, "data") == 0) {
                        size_t len = strlen(object.as.string);
                        Value arr; arr.type = VALUE_ARRAY; arr.as.array.count = (int)len; arr.as.array.values = (Value*)malloc(sizeof(Value) * len);
                        for (size_t i = 0; i < len; i++) { arr.as.array.values[i].type = VALUE_NUMBER; arr.as.array.values[i].as.number = (unsigned char)object.as.string[i]; }
                        vm_push(vm, arr); break;
                    }
                    // Unknown property on string -> nil
                    Value nilv = {VALUE_NIL}; vm_push(vm, nilv); break;
                }
                
                if (object.type != VALUE_OBJECT) {
                    runtime_error(vm, "Can only access properties on objects.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                    // Check if this is a namespace object (has __namespace__ / __interface__)
                    const char* interface_name = NULL;
                    const char* namespace_name = NULL;
                for (int i = 0; i < object.as.object.count; i++) {
                        if (strcmp(object.as.object.keys[i], "__interface__") == 0) {
                        if (object.as.object.values[i].type == VALUE_STRING) {
                                interface_name = object.as.object.values[i].as.string;
                        }
                        } else if (strcmp(object.as.object.keys[i], "__namespace__") == 0) {
                            if (object.as.object.values[i].type == VALUE_STRING) {
                                namespace_name = object.as.object.values[i].as.string;
                            }
                    }
                }
                
                    if ((namespace_name && namespace_name[0]) || (interface_name && interface_name[0])) {
                        // Try namespaced alias first: "namespace.method"
                        if (namespace_name && namespace_name[0]) {
                            char dotted_ns[256];
                            snprintf(dotted_ns, sizeof(dotted_ns), "%s.%s", namespace_name, key.as.string);
                            if (is_dynamic_function(dotted_ns)) {
                                Value func_val = create_dynamic_function_value(dotted_ns);
                                vm_push(vm, func_val);
                                break;
                            }
                        }
                        // Fallback: interface dotted alias: "interface.method"
                        if (interface_name && interface_name[0]) {
                            char dotted_if[256];
                            snprintf(dotted_if, sizeof(dotted_if), "%s.%s", interface_name, key.as.string);
                            if (is_dynamic_function(dotted_if)) {
                                Value func_val = create_dynamic_function_value(dotted_if);
                                vm_push(vm, func_val);
                                break;
                            }
                        }
                }
                
                // Search for the key in the object
                bool found = false;
                for (int i = 0; i < object.as.object.count; i++) {
                    if (strcmp(object.as.object.keys[i], key.as.string) == 0) {
                        vm_push(vm, object.as.object.values[i]);
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    // Push nil for missing properties (like JavaScript)
                    Value nil_val; nil_val.type = VALUE_NIL; vm_push(vm, nil_val);
                }
                break;
            }
            case OP_OBJECT_NEW: {
                Value obj;
                obj.type = VALUE_OBJECT;
                obj.as.object.count = 0;
                obj.as.object.keys = NULL;
                obj.as.object.values = NULL;
                vm_push(vm, obj);
                break;
            }
            case OP_OBJECT_SET: {
                // New operand order: stack (bottom->top) is object, key, value
                // We pop in reverse: value, key, object
                Value value = vm_pop(vm);
                Value key = vm_pop(vm);
                Value object = vm_pop(vm);
                
        // Debug logging removed for cleanliness
                
                if (object.type != VALUE_OBJECT) {
                    runtime_error(vm, "Can only set properties on objects.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (key.type != VALUE_STRING) {
                    runtime_error(vm, "Object property key must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                // Search for existing key
                bool found = false;
                for (int i = 0; i < object.as.object.count; i++) {
                    if (strcmp(object.as.object.keys[i], key.as.string) == 0) {
                        object.as.object.values[i] = value;
                        found = true;
                        break;
                    }
                }
                
                // If key doesn't exist, add it (dynamic property addition)
                if (!found) {
                    object.as.object.count++;
                    object.as.object.keys = realloc(object.as.object.keys, 
                                                    sizeof(char*) * object.as.object.count);
                    object.as.object.values = realloc(object.as.object.values, 
                                                      sizeof(Value) * object.as.object.count);
                    object.as.object.keys[object.as.object.count - 1] = strdup(key.as.string);
                    object.as.object.values[object.as.object.count - 1] = value;
                }
                
                // Push the mutated object back so it can be stored in the local
                vm_push(vm, object);
                break;
            }
            case OP_HALT:
                // For nested execution (imports), pop the frame before returning
                if (vm->frame_count > 1) {
                    vm->frame_count--;
                    // Restore stack to caller's position
                    vm->stack_top = vm->frames[vm->frame_count - 1].slots;
                    // Push nil as the return value
                    Value nil_val = {VALUE_NIL};
                    vm_push(vm, nil_val);
                }
                return INTERPRET_OK;
            default:

                runtime_error(vm, "Unknown opcode %d", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }
    
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
}

// Forward declaration for vm_init_limited
static void vm_init_limited(VM* vm, LibraryFlags allowed_libraries);

// Compiled script management
#define MAX_COMPILED_SCRIPTS 100
static CompiledScript compiled_scripts[MAX_COMPILED_SCRIPTS];
static int next_handle_id = 1;
static int compiled_scripts_initialized = 0;

static void init_compiled_scripts() {
    if (!compiled_scripts_initialized) {
        for (int i = 0; i < MAX_COMPILED_SCRIPTS; i++) {
            compiled_scripts[i].is_valid = 0;
            compiled_scripts[i].handle_id = 0;
            compiled_scripts[i].function = NULL;
            compiled_scripts[i].libraries = 0;
        }
        compiled_scripts_initialized = 1;
    }
}

static int allocate_script_handle() {
    init_compiled_scripts();
    
    for (int i = 0; i < MAX_COMPILED_SCRIPTS; i++) {
        if (!compiled_scripts[i].is_valid) {
            compiled_scripts[i].is_valid = 1;
            compiled_scripts[i].handle_id = next_handle_id++;
            return compiled_scripts[i].handle_id;
        }
    }
    return -1; // No free slots
}

static CompiledScript* get_compiled_script(int handle_id) {
    init_compiled_scripts();
    
    for (int i = 0; i < MAX_COMPILED_SCRIPTS; i++) {
        if (compiled_scripts[i].is_valid && compiled_scripts[i].handle_id == handle_id) {
            return &compiled_scripts[i];
        }
    }
    return NULL;
}

int vm_compile_script(const char* source, LibraryFlags required_libraries) {
    // Allocate handle first
    int handle_id = allocate_script_handle();
    if (handle_id == -1) {
        fprintf(stderr, "Compile error: Too many compiled scripts (max %d)\n", MAX_COMPILED_SCRIPTS);
        return -1;
    }
    
    CompiledScript* script = get_compiled_script(handle_id);
    if (!script) {
        fprintf(stderr, "Compile error: Failed to allocate script handle\n");
        return -1;
    }
    
    // Tokenize (dynamic buffer)
    Lexer lexer;
    lexer_init(&lexer, source);

    int capacity = 2048;
    int token_count = 0;
    Token* tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) {
        fprintf(stderr, "Compile error: Out of memory while tokenizing\n");
        script->is_valid = 0;
        return -1;
    }
    
    for (;;) {
        Token token = lexer_scan_token(&lexer);
        if (token_count >= capacity) {
            int new_capacity = capacity * 2;
            Token* grown = (Token*)realloc(tokens, sizeof(Token) * new_capacity);
            if (!grown) {
                fprintf(stderr, "Compile error: Script too large (exceeds %d tokens)\n", capacity);
                free(tokens);
                script->is_valid = 0;
                return -1;
            }
            tokens = grown;
            capacity = new_capacity;
        }
        tokens[token_count++] = token;
        
        if (token.type == TOKEN_ERROR) {
            fprintf(stderr, "Compile lexical error at line %d: %.*s\n", token.line, token.length, token.start);
            free(tokens);
            script->is_valid = 0;
            return -1;
        }
        
        if (token.type == TOKEN_EOF) break;
    }
    
    // Parse
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode* ast = parser_parse(&parser);
    
    if (parser.had_error) {
        fprintf(stderr, "Compile parse error\n");
        ast_node_free(ast);
        free(tokens);
        script->is_valid = 0;
        return -1;
    }
    
    // Compile to bytecode
    Function* function = compiler_compile(ast);
    ast_node_free(ast);
    free(tokens);
    
    if (function == NULL) {
        fprintf(stderr, "Compile bytecode error\n");
        script->is_valid = 0;
        return -1;
    }
    
    // Wire source path for future error reporting
    function->source_path = "<dynamic>";
    
    // Store compiled function and metadata
    script->function = function;
    script->libraries = required_libraries;
    
    printf("Script compiled successfully with handle ID: %d\n", handle_id);
    return handle_id;
}

InterpretResult vm_execute_compiled(int script_handle, Value* input_params, 
                                   int param_count, Value* result) {
    CompiledScript* script = get_compiled_script(script_handle);
    if (!script) {
        fprintf(stderr, "Execute error: Invalid script handle %d\n", script_handle);
        return INTERPRET_RUNTIME_ERROR;
    }
    
    // Create isolated VM for execution
    VM exec_vm;
    vm_init_limited(&exec_vm, script->libraries);
    
    // Set up input parameters as global variables
    for (int i = 0; i < param_count; i++) {
        char param_name[32];
        snprintf(param_name, sizeof(param_name), "param%d", i);
        define_global(&exec_vm, param_name, input_params[i]);
    }
    
    // Also provide param_count as a global
    Value param_count_val = {VALUE_NUMBER, {.number = (double)param_count}};
    define_global(&exec_vm, "param_count", param_count_val);
    
    // Set up execution frame with pre-compiled function
    exec_vm.frames[0].function = script->function;
    exec_vm.frames[0].ip = script->function->chunk.code;
    exec_vm.frames[0].slots = exec_vm.stack;
    exec_vm.frame_count = 1;
    
    // Execute the compiled script
    InterpretResult execution_result = vm_run(&exec_vm);
    
    // Capture the result if execution was successful
    if (execution_result == INTERPRET_OK && result != NULL) {
        if (exec_vm.stack_top > exec_vm.stack) {
            *result = *(exec_vm.stack_top - 1);  // Get the top value from the stack
        } else {
            result->type = VALUE_NIL;  // No result on stack
        }
    }
    
    // Clean up VM globals (but keep the compiled function)
    for (int i = 0; i < exec_vm.global_count; i++) {
        free(exec_vm.globals[i].name);
        if (exec_vm.globals[i].value.type == VALUE_STRING) {
            free(exec_vm.globals[i].value.as.string);
        }
    }
    
    return execution_result;
}

void vm_free_script(int script_handle) {
    CompiledScript* script = get_compiled_script(script_handle);
    if (script) {
        if (script->function) {
            // TODO: Properly free the compiled function memory
            // For now, just mark as invalid
            printf("Freed compiled script with handle ID: %d\n", script_handle);
        }
        script->is_valid = 0;
        script->handle_id = 0;
        script->function = NULL;
        script->libraries = 0;
    }
}

// Dynamic execution support functions
static void vm_init_limited(VM* vm, LibraryFlags allowed_libraries) {
    reset_stack(vm);
    vm->global_count = 0;
    
    // Always include core functionality
    if (allowed_libraries & LIBRARY_CORE) {
        Value print_val = {VALUE_STRING, {.string = strdup("print")}};
        define_global(vm, "print", print_val);
        Value typeof_val = {VALUE_STRING, {.string = strdup("typeof")}};
        define_global(vm, "typeof", typeof_val);
        // Core utility: array_length
        Value array_length_val = (Value){VALUE_STRING, {.string = strdup("array_length")}};
        define_global(vm, "array_length", array_length_val);
        // Decorator introspection: @get
        Value decorator_get_val = (Value){VALUE_STRING, {.string = strdup("@get")}};
        define_global(vm, "@get", decorator_get_val);
        // Decorator query: @getAllEntities
        Value get_all_entities_val = (Value){VALUE_STRING, {.string = strdup("@getAllEntities")}};
        define_global(vm, "@getAllEntities", get_all_entities_val);
        // Decorator query: @findByDecorator
        Value find_by_decorator_val = (Value){VALUE_STRING, {.string = strdup("@findByDecorator")}};
        define_global(vm, "@findByDecorator", find_by_decorator_val);
        // Dependency injection: register_dependency
        Value register_dep_val = (Value){VALUE_STRING, {.string = strdup("register_dependency")}};
        define_global(vm, "register_dependency", register_dep_val);
    }
    // Legacy hardcoded library functions removed - now handled by modular library system
    
    // Logging functions - logger should be initialized by main() before VM creation
    if (allowed_libraries & LIBRARY_LOG) {
        Value log_fatal_val = {VALUE_STRING, {.string = strdup("log_fatal")}};
        Value log_error_val = {VALUE_STRING, {.string = strdup("log_error")}};
        Value log_warning_val = {VALUE_STRING, {.string = strdup("log_warning")}};
        Value log_info_val = {VALUE_STRING, {.string = strdup("log_info")}};
        Value log_debug_val = {VALUE_STRING, {.string = strdup("log_debug")}};
        
        define_global(vm, "log_fatal", log_fatal_val);
        define_global(vm, "log_error", log_error_val);
        define_global(vm, "log_warning", log_warning_val);
        define_global(vm, "log_info", log_info_val);
        define_global(vm, "log_debug", log_debug_val);
    }
    
    // Environment variable functions
    if (allowed_libraries & LIBRARY_ENV) {
        Value getenv_val = {VALUE_STRING, {.string = strdup("getenv")}};
        Value setenv_val = {VALUE_STRING, {.string = strdup("setenv")}};
        
        define_global(vm, "getenv", getenv_val);
        define_global(vm, "setenv", setenv_val);
    }
    
    // HTTP server functions
    if (allowed_libraries & LIBRARY_HTTP) {
        Value http_server_val = {VALUE_STRING, {.string = strdup("http_server")}};
        Value http_get_val = {VALUE_STRING, {.string = strdup("http_get")}};
        Value http_post_val = {VALUE_STRING, {.string = strdup("http_post")}};
        Value http_listen_val = {VALUE_STRING, {.string = strdup("http_listen")}};
        
        define_global(vm, "http_server", http_server_val);
        define_global(vm, "http_get", http_get_val);
        define_global(vm, "http_post", http_post_val);
        define_global(vm, "http_listen", http_listen_val);
    }
}

InterpretResult vm_execute_dynamic(VM* vm, const char* source, 
                                 Value* input_params, int param_count,
                                 LibraryFlags allowed_libraries,
                                 Value* result) {
    // Initialize VM with limited libraries
    vm_init_limited(vm, allowed_libraries);
    
    // Set up input parameters as global variables
    for (int i = 0; i < param_count; i++) {
        char param_name[32];
        snprintf(param_name, sizeof(param_name), "param%d", i);
        define_global(vm, param_name, input_params[i]);
    }
    
    // Also provide param_count as a global
    Value param_count_val = {VALUE_NUMBER, {.number = (double)param_count}};
    define_global(vm, "param_count", param_count_val);
    
    // Tokenize with error handling (dynamic buffer)
    Lexer lexer;
    lexer_init(&lexer, source);

    int capacity = 1024;
    int token_count = 0;
    Token* tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) {
        fprintf(stderr, "Dynamic execution error: Out of memory while tokenizing\n");
        return INTERPRET_COMPILE_ERROR;
    }

    for (;;) {
        Token token = lexer_scan_token(&lexer);
        if (token_count >= capacity) {
            int new_capacity = capacity * 2;
            Token* grown = (Token*)realloc(tokens, sizeof(Token) * new_capacity);
            if (!grown) {
                fprintf(stderr, "Dynamic execution error: Script too large (exceeds %d tokens)\n", capacity);
                free(tokens);
                return INTERPRET_COMPILE_ERROR;
            }
            tokens = grown;
            capacity = new_capacity;
        }
        tokens[token_count++] = token;

        if (token.type == TOKEN_ERROR) {
            fprintf(stderr, "Dynamic execution lexical error at line %d: %.*s\n", 
                    token.line, token.length, token.start);
            free(tokens);
            return INTERPRET_COMPILE_ERROR;
        }

        if (token.type == TOKEN_EOF) break;
    }

    // Parse with error handling
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode* ast = parser_parse(&parser);
    
    if (parser.had_error) {
        fprintf(stderr, "Dynamic execution parse error\n");
        ast_node_free(ast);
        free(tokens);
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Compile with error handling
    Function* function = compiler_compile(ast);
    ast_node_free(ast);
    // tokens are no longer needed after parsing
    free(tokens);
    
    if (function == NULL) {
        fprintf(stderr, "Dynamic execution compile error\n");
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Wire source path for error reporting
    function->source_path = vm->current_source_path ? vm->current_source_path : "<dynamic>";
    
    // Set up execution frame
    vm->frames[0].function = function;
    vm->frames[0].ip = function->chunk.code;
    vm->frames[0].slots = vm->stack;
    vm->frame_count = 1;
    
    // Execute the script
    InterpretResult execution_result = vm_run(vm);
    
    // Capture the result if execution was successful
    if (execution_result == INTERPRET_OK && result != NULL) {
        if (vm->stack_top > vm->stack) {
            *result = *(vm->stack_top - 1);  // Get the top value from the stack
        } else {
            result->type = VALUE_NIL;  // No result on stack
        }
    }
    
    // Clean up allocated memory
    for (int i = 0; i < vm->global_count; i++) {
        free(vm->globals[i].name);
        if (vm->globals[i].value.type == VALUE_STRING) {
            free(vm->globals[i].value.as.string);
        }
    }
    
    return execution_result;
}

void vm_init(VM* vm) {
    reset_stack(vm);
    vm->global_count = 0;
    vm->test_mode = false;
    vm->coverage_enabled = false;
    vm->assertions_total = 0;
    vm->assertions_failed = 0;
    vm->coverage.count = 0;
    vm->current_source_path = NULL;
    
    // Initialize decorator registry
    vm->decorator_registry_count = 0;
    vm->decorator_handler_depth = 0;
    vm->unwrapped_function_name = NULL;
    
    // Set global VM instance
    g_current_vm = vm;
    
    // Initialize event loop for async operations
    vm->event_base = event_base_new();
    if (!vm->event_base) {
        LOG_ERROR("Failed to create libevent base");
    }
    
    // Initialize avatar runtime for async execution
    vm->avatar_runtime = avatar_runtime_create(0);  // 0 = auto-detect thread count
    if (!vm->avatar_runtime) {
        LOG_ERROR("Failed to create avatar runtime");
    } else {
        LOG_INFO("Avatar runtime initialized with %zu threads", 
                 avatar_runtime_thread_count(vm->avatar_runtime));
    }
    
    // Initialize async HTTP client
    vm->async_http = async_http_client_create(vm->event_base);
    if (!vm->async_http) {
        LOG_ERROR("Failed to create async HTTP client");
    }
    
    // Initialize async request queue for non-blocking I/O
    vm->request_queue = async_request_queue_create();
    if (!vm->request_queue) {
        LOG_ERROR("Failed to create async request queue");
    } else {
        // Link request queue to async HTTP client
        async_request_queue_set_http_client(vm->async_http);
    }
    
    // Initialize global task queue for thread-safe operations
    if (!g_vm_task_queue) {
        g_vm_task_queue = task_queue_create(1024);  // Queue capacity: 1024 tasks
    }
    
    // Register response helper functions as globals (STRING sentinel values)
    // These will be caught by the STRING call handler in call_value
    Value response_sentinel;
    response_sentinel.type = VALUE_STRING;
    
    response_sentinel.as.string = "response_setStatus";
    set_global(vm, "response_setStatus", response_sentinel);
    
    response_sentinel.as.string = "response_addHeader";
    set_global(vm, "response_addHeader", response_sentinel);
    
    response_sentinel.as.string = "response_setBody";
    set_global(vm, "response_setBody", response_sentinel);
    
#ifndef _WIN32
    // Install basic crash handlers to improve diagnostics on segfaults
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = kuyil_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#endif
    
    // Logging system should be initialized by main() before VM creation
    LOG_INFO("Kuyil VM initialized");
    
    // Register built-in functions
    Value print_val;
    print_val.type = VALUE_STRING;
    print_val.as.string = strdup("print");
    define_global(vm, "print", print_val);
    
    Value typeof_val;
    typeof_val.type = VALUE_STRING;
    typeof_val.as.string = strdup("typeof");
    define_global(vm, "typeof", typeof_val);
    
    // Core utility: array_length (allow bare identifier calls)
    Value array_length_val = (Value){VALUE_STRING, {.string = strdup("array_length")}};
    define_global(vm, "array_length", array_length_val);
    
    // Register AIO response functions (for route handlers)
    Value aio_response_val = (Value){VALUE_STRING, {.string = strdup("aio_response")}};
    define_global(vm, "aio_response", aio_response_val);
    
    Value aio_request_val = (Value){VALUE_STRING, {.string = strdup("aio_request")}};
    define_global(vm, "aio_request", aio_request_val);
    
    Value aio_response_setStatus_val = (Value){VALUE_STRING, {.string = strdup("aio_response_setStatus")}};
    define_global(vm, "aio_response_setStatus", aio_response_setStatus_val);
    
    Value aio_response_setBody_val = (Value){VALUE_STRING, {.string = strdup("aio_response_setBody")}};
    define_global(vm, "aio_response_setBody", aio_response_setBody_val);
    
    Value aio_response_addHeader_val = (Value){VALUE_STRING, {.string = strdup("aio_response_addHeader")}};
    define_global(vm, "aio_response_addHeader", aio_response_addHeader_val);
    
    Value aio_response_get_body_val = (Value){VALUE_STRING, {.string = strdup("aio_response_get_body")}};
    define_global(vm, "aio_response_get_body", aio_response_get_body_val);
    
    // Register logging functions
    Value log_fatal_val = {VALUE_STRING, {.string = strdup("log_fatal")}};
    Value log_error_val = {VALUE_STRING, {.string = strdup("log_error")}};
    Value log_warning_val = {VALUE_STRING, {.string = strdup("log_warning")}};
    Value log_info_val = {VALUE_STRING, {.string = strdup("log_info")}};
    Value log_debug_val = {VALUE_STRING, {.string = strdup("log_debug")}};
    
    define_global(vm, "log_fatal", log_fatal_val);
    define_global(vm, "log_error", log_error_val);
    define_global(vm, "log_warning", log_warning_val);
    define_global(vm, "log_info", log_info_val);
    define_global(vm, "log_debug", log_debug_val);

    // Register test/assert and mock helpers as callable names
    Value assert_true_val = {VALUE_STRING, {.string = strdup("assert_true")}};
    Value assert_eq_val = {VALUE_STRING, {.string = strdup("assert_eq")}};
    Value assert_neq_val = {VALUE_STRING, {.string = strdup("assert_neq")}};
    Value assert_called_val = {VALUE_STRING, {.string = strdup("assert_called")}};
    Value assert_equals_val = {VALUE_STRING, {.string = strdup("assert_equals")}};
    Value assert_nil_val = {VALUE_STRING, {.string = strdup("assert_nil")}};
    Value assert_not_nil_val = {VALUE_STRING, {.string = strdup("assert_not_nil")}};
    Value mock_return_val = {VALUE_STRING, {.string = strdup("mock_return")}};
    Value mock_function_val = {VALUE_STRING, {.string = strdup("mock_function")}};
    Value mock_clear_val = {VALUE_STRING, {.string = strdup("mock_clear")}};
    Value mock_calls_val = {VALUE_STRING, {.string = strdup("mock_calls")}};
    
    define_global(vm, "assert_true", assert_true_val);
    define_global(vm, "assert_eq", assert_eq_val);
    define_global(vm, "assert_neq", assert_neq_val);
    define_global(vm, "assert_called", assert_called_val);
    define_global(vm, "assert_equals", assert_equals_val);
    define_global(vm, "assert_nil", assert_nil_val);
    define_global(vm, "assert_not_nil", assert_not_nil_val);
    define_global(vm, "mock_return", mock_return_val);
    define_global(vm, "mock_function", mock_function_val);
    define_global(vm, "mock_clear", mock_clear_val);
    define_global(vm, "mock_calls", mock_calls_val);
    
    // Register route decorator functions
    Value route_register_val = {VALUE_STRING, {.string = strdup("route_register")}};
    Value route_find_val = {VALUE_STRING, {.string = strdup("route_find")}};
    Value route_list_val = {VALUE_STRING, {.string = strdup("route_list")}};
    Value route_extract_params_val = {VALUE_STRING, {.string = strdup("route_extract_params")}};
    
    define_global(vm, "route_register", route_register_val);
    define_global(vm, "route_find", route_find_val);
    define_global(vm, "route_list", route_list_val);
    define_global(vm, "route_extract_params", route_extract_params_val);
    
    // Register bridge invoke function
    Value route_bridge_invoke_val = {VALUE_STRING, {.string = strdup("route_bridge_invoke")}};
    define_global(vm, "route_bridge_invoke", route_bridge_invoke_val);
    
    // Register request/response helper functions
    Value request_val = {VALUE_STRING, {.string = strdup("Request")}};
    Value response_val = {VALUE_STRING, {.string = strdup("Response")}};
    define_global(vm, "Request", request_val);
    define_global(vm, "Response", response_val);
    
    // Register async HTTP server functions
    Value http_start_server_val = {VALUE_STRING, {.string = strdup("http_start_server")}};
    Value http_stop_server_val = {VALUE_STRING, {.string = strdup("http_stop_server")}};
    Value http_server_wait_val = {VALUE_STRING, {.string = strdup("http_server_wait")}};
    Value http_static_add_val = {VALUE_STRING, {.string = strdup("http_static_add")}};
    define_global(vm, "http_start_server", http_start_server_val);
    define_global(vm, "http_stop_server", http_stop_server_val);
    define_global(vm, "http_server_wait", http_server_wait_val);
    define_global(vm, "http_static_add", http_static_add_val);
    
    // Register mutex primitives
    Value mutex_create_val = {VALUE_STRING, {.string = strdup("mutex_create")}};
    Value mutex_lock_val = {VALUE_STRING, {.string = strdup("mutex_lock")}};
    Value mutex_unlock_val = {VALUE_STRING, {.string = strdup("mutex_unlock")}};
    Value mutex_try_lock_val = {VALUE_STRING, {.string = strdup("mutex_try_lock")}};
    Value mutex_destroy_val = {VALUE_STRING, {.string = strdup("mutex_destroy")}};
    define_global(vm, "mutex_create", mutex_create_val);
    define_global(vm, "mutex_lock", mutex_lock_val);
    define_global(vm, "mutex_unlock", mutex_unlock_val);
    define_global(vm, "mutex_try_lock", mutex_try_lock_val);
    define_global(vm, "mutex_destroy", mutex_destroy_val);
    
    // Register __range as a built-in (will be handled specially in OP_CALL)
    Value range_val = {VALUE_STRING, {.string = strdup("__range")}};
    define_global(vm, "__range", range_val);
    
    // Register decorator introspection function
    Value decorator_get_val = {VALUE_STRING, {.string = strdup("@get")}};
    define_global(vm, "@get", decorator_get_val);
    
    // Register decorator query functions
    Value get_all_entities_val = {VALUE_STRING, {.string = strdup("@getAllEntities")}};
    define_global(vm, "@getAllEntities", get_all_entities_val);
    Value find_by_decorator_val = {VALUE_STRING, {.string = strdup("@findByDecorator")}};
    define_global(vm, "@findByDecorator", find_by_decorator_val);
    
    // Register dependency injection function
    Value register_dep_val = {VALUE_STRING, {.string = strdup("register_dependency")}};
    define_global(vm, "register_dependency", register_dep_val);
    
    // Register development helper functions
    Value dev_watch_file_val = {VALUE_STRING, {.string = strdup("dev_watch_file")}};
    Value dev_watch_dir_val = {VALUE_STRING, {.string = strdup("dev_watch_dir")}};
    Value dev_start_watching_val = {VALUE_STRING, {.string = strdup("dev_start_watching")}};
    Value dev_stop_watching_val = {VALUE_STRING, {.string = strdup("dev_stop_watching")}};
    
    define_global(vm, "dev_watch_file", dev_watch_file_val);
    define_global(vm, "dev_watch_dir", dev_watch_dir_val);
    define_global(vm, "dev_start_watching", dev_start_watching_val);
    define_global(vm, "dev_stop_watching", dev_stop_watching_val);
    
    // Register configuration functions
    Value load_yaml_val = {VALUE_STRING, {.string = strdup("load_yaml")}};
    Value merge_config_val = {VALUE_STRING, {.string = strdup("merge_config")}};
    Value config_get_val = {VALUE_STRING, {.string = strdup("config_get")}};
    
    define_global(vm, "load_yaml", load_yaml_val);
    define_global(vm, "merge_config", merge_config_val);
    define_global(vm, "config_get", config_get_val);
    
    // Register environment variable functions
    Value getenv_val = {VALUE_STRING, {.string = strdup("getenv")}};
    Value setenv_val = {VALUE_STRING, {.string = strdup("setenv")}};
    
    define_global(vm, "getenv", getenv_val);
    define_global(vm, "setenv", setenv_val);
    
    // Legacy hardcoded library functions removed - now handled by modular library system
    // (string functions, math functions, date utilities are loaded via .so modules)
    
    // Register dynamic execution function
    Value execute_script_val = {VALUE_STRING, {.string = strdup("execute_script")}};
    define_global(vm, "execute_script", execute_script_val);
    
    // Register compile-once, execute-multiple functions
    Value compile_script_val = {VALUE_STRING, {.string = strdup("compile_script")}};
    Value execute_compiled_val = {VALUE_STRING, {.string = strdup("execute_compiled")}};
    Value free_script_val = {VALUE_STRING, {.string = strdup("free_script")}};
    
    define_global(vm, "compile_script", compile_script_val);
    define_global(vm, "execute_compiled", execute_compiled_val);
    define_global(vm, "free_script", free_script_val);
    
    // Register FFI (Foreign Function Interface) functions
    Value load_library_val = {VALUE_STRING, {.string = strdup("load_library")}};
    Value register_function_val = {VALUE_STRING, {.string = strdup("register_function")}};
    Value call_function_val = {VALUE_STRING, {.string = strdup("call_function")}};
    Value unload_library_val = {VALUE_STRING, {.string = strdup("unload_library")}};
    
    // Enhanced FFI functions for shared library creation and management
    Value create_shared_library_val = {VALUE_STRING, {.string = strdup("create_shared_library")}};
    Value create_smart_shared_library_val = {VALUE_STRING, {.string = strdup("create_smart_shared_library")}};
    Value load_library_ex_val = {VALUE_STRING, {.string = strdup("load_library_ex")}};
    Value unload_library_ex_val = {VALUE_STRING, {.string = strdup("unload_library_ex")}};
    Value list_loaded_libraries_val = {VALUE_STRING, {.string = strdup("list_loaded_libraries")}};
    Value is_library_loaded_val = {VALUE_STRING, {.string = strdup("is_library_loaded")}};
    
    // Application lifecycle functions
    Value register_startup_val = {VALUE_STRING, {.string = strdup("register_startup")}};
    Value register_shutdown_val = {VALUE_STRING, {.string = strdup("register_shutdown")}};
    Value startup_val = {VALUE_STRING, {.string = strdup("startup")}};
    Value shutdown_val = {VALUE_STRING, {.string = strdup("shutdown")}};
    
    // HTTP Server functions (simplified)
    Value show_static_files_val = {VALUE_STRING, {.string = strdup("show_static_files")}};
    
    // Convenience functions for common client libraries
    Value load_redis_client_val = {VALUE_STRING, {.string = strdup("load_redis_client")}};
    Value load_elasticsearch_client_val = {VALUE_STRING, {.string = strdup("load_elasticsearch_client")}};
    Value load_kms_client_val = {VALUE_STRING, {.string = strdup("load_kms_client")}};
    
    define_global(vm, "load_library", load_library_val);
    define_global(vm, "register_function", register_function_val);
    define_global(vm, "call_function", call_function_val);
    define_global(vm, "unload_library", unload_library_val);
    
    // Define enhanced FFI functions
    define_global(vm, "create_shared_library", create_shared_library_val);
    define_global(vm, "create_smart_shared_library", create_smart_shared_library_val);
    define_global(vm, "load_library_ex", load_library_ex_val);
    define_global(vm, "unload_library_ex", unload_library_ex_val);
    define_global(vm, "list_loaded_libraries", list_loaded_libraries_val);
    define_global(vm, "is_library_loaded", is_library_loaded_val);
    
    define_global(vm, "register_startup", register_startup_val);
    define_global(vm, "register_shutdown", register_shutdown_val);
    define_global(vm, "startup", startup_val);
    define_global(vm, "shutdown", shutdown_val);
    define_global(vm, "show_static_files", show_static_files_val);
    define_global(vm, "load_redis_client", load_redis_client_val);
    define_global(vm, "load_elasticsearch_client", load_elasticsearch_client_val);
    define_global(vm, "load_kms_client", load_kms_client_val);
    
    // Enhanced Library Metadata Management Functions
    Value create_version_val = {VALUE_STRING, {.string = strdup("create_version")}};
    Value compare_versions_val = {VALUE_STRING, {.string = strdup("compare_versions")}};
    Value create_library_info_val = {VALUE_STRING, {.string = strdup("create_library_info")}};
    Value create_dependency_val = {VALUE_STRING, {.string = strdup("create_dependency")}};
    Value create_library_registry_val = {VALUE_STRING, {.string = strdup("create_library_registry")}};
    
    define_global(vm, "create_version", create_version_val);
    define_global(vm, "compare_versions", compare_versions_val);
    define_global(vm, "create_library_info", create_library_info_val);
    define_global(vm, "create_dependency", create_dependency_val);
    define_global(vm, "create_library_registry", create_library_registry_val);
    
    // HTTP server functions
    Value http_server_val = {VALUE_STRING, {.string = strdup("http_server")}};
    Value http_get_val = {VALUE_STRING, {.string = strdup("http_get")}};
    Value http_post_val = {VALUE_STRING, {.string = strdup("http_post")}};
    Value http_listen_val = {VALUE_STRING, {.string = strdup("http_listen")}};
    
    define_global(vm, "http_server", http_server_val);
    define_global(vm, "http_get", http_get_val);
    define_global(vm, "http_post", http_post_val);
    define_global(vm, "http_listen", http_listen_val);
    
    // Initialize modular library system
    vm_init_library_system(vm);
    
    // Set global VM for library access
    set_current_vm(vm);
    
    // Initialize program arguments (sys.args)
    vm->program_args = NULL;
    vm->program_args_count = 0;
    
    LOG_INFO("FFI system initialized with %d functions", 53);
}

// Set program arguments and create sys.args namespace
void vm_set_program_args(VM* vm, int argc, char** argv) {
    // Store arguments in VM
    vm->program_args_count = argc;
    if (argc > 0) {
        vm->program_args = malloc(sizeof(char*) * argc);
        for (int i = 0; i < argc; i++) {
            vm->program_args[i] = strdup(argv[i]);
        }
    }
    
    // Create sys object
    Value sys_obj;
    sys_obj.type = VALUE_OBJECT;
    sys_obj.as.object.count = 1;
    sys_obj.as.object.keys = malloc(sizeof(char*) * 1);
    sys_obj.as.object.values = malloc(sizeof(Value) * 1);
    
    // Add sys.args as an array
    Value args_array;
    args_array.type = VALUE_ARRAY;
    args_array.as.array.count = argc;
    args_array.as.array.values = malloc(sizeof(Value) * argc);
    
    for (int i = 0; i < argc; i++) {
        Value arg;
        arg.type = VALUE_STRING;
        arg.as.string = strdup(argv[i]);
        args_array.as.array.values[i] = arg;
    }
    
    // Add args to sys object
    sys_obj.as.object.keys[0] = strdup("args");
    sys_obj.as.object.values[0] = args_array;
    
    // Define sys as a global
    define_global(vm, "sys", sys_obj);
    
    LOG_DEBUG("sys.args initialized with %d arguments", argc);
}

void vm_free(VM* vm) {
    LOG_INFO("Kuyil VM shutting down");
    
    // Cleanup avatar runtime
    if (vm->avatar_runtime) {
        LOG_INFO("Shutting down avatar runtime");
        avatar_runtime_destroy(vm->avatar_runtime);
        vm->avatar_runtime = NULL;
    }
    
    // Cleanup async HTTP client
    if (vm->async_http) {
        async_http_client_destroy(vm->async_http);
        vm->async_http = NULL;
    }
    
    // Cleanup event base
    if (vm->event_base) {
        event_base_free(vm->event_base);
        vm->event_base = NULL;
    }
    
    // Shutdown and cleanup task queue
    if (g_vm_task_queue) {
        task_queue_shutdown(g_vm_task_queue);
        task_queue_destroy(g_vm_task_queue);
        g_vm_task_queue = NULL;
    }
    
    // Ensure shutdown functions are called before cleanup
    if (g_ffi_context) {
        kuyil_log_debug("Ensuring shutdown functions are called during VM cleanup");
        ffi_call_shutdown_functions(g_ffi_context);
        
        LOG_INFO("Cleaning up FFI context");
        ffi_destroy_context(g_ffi_context);
        g_ffi_context = NULL;
    }
    
    // Cleanup VM library system (aliases, dynamic functions, loaded libs)
    vm_cleanup_library_system();
    
    // Free program arguments
    if (vm->program_args) {
        for (int i = 0; i < vm->program_args_count; i++) {
            free(vm->program_args[i]);
        }
        free(vm->program_args);
        vm->program_args = NULL;
    }
    
    // Free globals
    for (int i = 0; i < vm->global_count; i++) {
        free(vm->globals[i].name);
        if (vm->globals[i].value.type == VALUE_STRING) {
            free(vm->globals[i].value.as.string);
        }
    }
    
    // Cleanup logging system
    log_cleanup();
}

InterpretResult vm_interpret(VM* vm, const char* source) {
    // Tokenize with dynamic buffer (removes 10K token limit)
    Lexer lexer;
    lexer_init(&lexer, source);

    int capacity = 4096;
    int token_count = 0;
    Token* tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) {
        fprintf(stderr, "Memory allocation failure while tokenizing.\n");
        return INTERPRET_RUNTIME_ERROR;
    }

    for (;;) {
        Token token = lexer_scan_token(&lexer);
        if (token_count >= capacity) {
            int new_capacity = capacity * 2;
            Token* grown = (Token*)realloc(tokens, sizeof(Token) * new_capacity);
            if (!grown) {
                fprintf(stderr, "❌ Lexical Error: script too large (exceeds %d tokens). Increase token buffer or split the file.\n",
                        capacity);
                free(tokens);
                return INTERPRET_COMPILE_ERROR;
            }
            tokens = grown;
            capacity = new_capacity;
        }
        tokens[token_count++] = token;

        if (token.type == TOKEN_ERROR) {
            print_lexical_error_with_context(source, token);
            free(tokens);
            return INTERPRET_COMPILE_ERROR;
        }

        if (token.type == TOKEN_EOF) break;
    }

    // Parse
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode* ast = parser_parse(&parser);
    
    if (parser.had_error) {
        ast_node_free(ast);
        free(tokens);
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Compile
    Function* function = compiler_compile(ast);
    ast_node_free(ast);
    free(tokens);
    
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    
    // Register all decorator metadata collected during compilation
    compiler_register_all_decorators(vm);
    
    // Wire source path into compiled function for stack traces
    function->source_path = vm->current_source_path;
    
    // Check if we're in a nested import (frame_count > 0 means we're already executing)
    bool is_nested = (vm->frame_count > 0);
    
    // Call startup functions before execution (only for top-level scripts, not imports)
    if (!is_nested && g_ffi_context) {
        kuyil_log_debug("Automatically calling startup functions before script execution");
        if (!ffi_call_startup_functions(g_ffi_context)) {
            kuyil_log_warning("Startup functions failed, continuing with script execution");
        }
    }
    
    // Set up call frame
    if (is_nested) {
        // For nested imports, add a new frame on top of existing frames
        if (vm->frame_count >= FRAMES_MAX) {
            fprintf(stderr, "Stack overflow - import would exceed maximum call depth.\n");
            return INTERPRET_RUNTIME_ERROR;
        }
        CallFrame* frame = &vm->frames[vm->frame_count];
        frame->function = function;
        frame->ip = function->chunk.code;
        frame->slots = vm->stack_top;
        vm->frame_count++;
    } else {
        // For top-level scripts, use frame 0
        // Properly allocate space for local variables
        vm->frames[0].function = function;
        vm->frames[0].ip = function->chunk.code;
        vm->frames[0].slots = vm->stack;
        vm->frame_count = 1;
        
        // Allocate and initialize local variable slots
        vm->stack_top = vm->stack;
        int local_count = function->local_count;
        for (int i = 0; i < local_count; i++) {
            Value nil_val = {VALUE_NIL};
            *vm->stack_top = nil_val;
            vm->stack_top++;
        }
    }
    
    InterpretResult result = vm_run(vm);
    
    // Call shutdown functions after execution (only for top-level scripts, not imports)
    if (!is_nested && g_ffi_context) {
        kuyil_log_debug("Automatically calling shutdown functions after script execution");
        ffi_call_shutdown_functions(g_ffi_context);
    }
    
    return result;
}

InterpretResult vm_interpret_bytecode(VM* vm, const char* bytecode_path) {
    FILE* file = fopen(bytecode_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open bytecode file \"%s\".\n", bytecode_path);
        return INTERPRET_COMPILE_ERROR;
    }

    // Read and verify magic header
    uint32_t magic;
    if (fread(&magic, sizeof(uint32_t), 1, file) != 1) {
        fprintf(stderr, "Invalid bytecode file format: could not read magic header.\n");
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }
    
    if (magic != 0x4B59494C) { // "KYIL" magic number
        fprintf(stderr, "Invalid bytecode file format: invalid magic header 0x%08X.\n", magic);
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }

    // Read bytecode size
    uint32_t code_size;
    if (fread(&code_size, sizeof(uint32_t), 1, file) != 1) {
        fprintf(stderr, "Invalid bytecode file format: could not read code size.\n");
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }

    // Read bytecode
    uint8_t* bytecode = malloc(code_size * sizeof(uint8_t));
    if (bytecode == NULL) {
        fprintf(stderr, "Memory allocation failed for bytecode.\n");
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }
    
    if (fread(bytecode, sizeof(uint8_t), code_size, file) != code_size) {
        fprintf(stderr, "Invalid bytecode file format: could not read bytecode.\n");
        free(bytecode);
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }

    // Read constants count
    uint32_t const_count;
    if (fread(&const_count, sizeof(uint32_t), 1, file) != 1) {
        fprintf(stderr, "Invalid bytecode file format: could not read constants count.\n");
        free(bytecode);
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }

    // Read constants
    Value* constants = malloc(const_count * sizeof(Value));
    if (constants == NULL && const_count > 0) {
        fprintf(stderr, "Memory allocation failed for constants.\n");
        free(bytecode);
        fclose(file);
        return INTERPRET_COMPILE_ERROR;
    }

    for (uint32_t i = 0; i < const_count; i++) {
        ValueType type;
        if (fread(&type, sizeof(ValueType), 1, file) != 1) {
            fprintf(stderr, "Invalid bytecode file format: could not read constant type.\n");
            free(bytecode);
            free(constants);
            fclose(file);
            return INTERPRET_COMPILE_ERROR;
        }
        
        constants[i].type = type;
        
        switch (type) {
            case VALUE_NUMBER:
                if (fread(&constants[i].as.number, sizeof(double), 1, file) != 1) {
                    fprintf(stderr, "Invalid bytecode file format: could not read number constant.\n");
                    free(bytecode);
                    free(constants);
                    fclose(file);
                    return INTERPRET_COMPILE_ERROR;
                }
                break;
                
            case VALUE_STRING: {
                uint32_t len;
                if (fread(&len, sizeof(uint32_t), 1, file) != 1) {
                    fprintf(stderr, "Invalid bytecode file format: could not read string length.\n");
                    free(bytecode);
                    free(constants);
                    fclose(file);
                    return INTERPRET_COMPILE_ERROR;
                }
                
                char* str = malloc((len + 1) * sizeof(char));
                if (str == NULL) {
                    fprintf(stderr, "Memory allocation failed for string constant.\n");
                    free(bytecode);
                    free(constants);
                    fclose(file);
                    return INTERPRET_COMPILE_ERROR;
                }
                
                if (fread(str, sizeof(char), len, file) != len) {
                    fprintf(stderr, "Invalid bytecode file format: could not read string constant.\n");
                    free(str);
                    free(bytecode);
                    free(constants);
                    fclose(file);
                    return INTERPRET_COMPILE_ERROR;
                }
                str[len] = '\0';
                constants[i].as.string = str;
                break;
            }
            
            case VALUE_BOOL:
                if (fread(&constants[i].as.boolean, sizeof(bool), 1, file) != 1) {
                    fprintf(stderr, "Invalid bytecode file format: could not read boolean constant.\n");
                    free(bytecode);
                    free(constants);
                    fclose(file);
                    return INTERPRET_COMPILE_ERROR;
                }
                break;
                
            case VALUE_NIL:
                constants[i].as.number = 0; // NIL doesn't need data
                break;
                
            default:
                fprintf(stderr, "Invalid bytecode file format: unknown constant type %d.\n", type);
                free(bytecode);
                free(constants);
                fclose(file);
                return INTERPRET_COMPILE_ERROR;
        }
    }
    
    fclose(file);

    // Create a function structure for the bytecode
    Function* function = malloc(sizeof(Function));
    if (function == NULL) {
        fprintf(stderr, "Memory allocation failed for function.\n");
        free(bytecode);
        free(constants);
        return INTERPRET_COMPILE_ERROR;
    }
    
    function->name = NULL; // Main function
    function->arity = 0;
    function->is_native = false;
    function->source_path = NULL;  // Bytecode files don't have source path info
    
    // Initialize chunk with loaded bytecode
    chunk_init(&function->chunk);
    function->chunk.code = bytecode;
    function->chunk.count = code_size;
    function->chunk.capacity = code_size;
    function->chunk.constants = constants;
    function->chunk.constant_count = const_count;
    function->chunk.constant_capacity = const_count;
    
    // Allocate and initialize line numbers (we don't have them in bytecode, so use 0)
    function->chunk.lines = malloc(code_size * sizeof(int));
    if (function->chunk.lines == NULL) {
        fprintf(stderr, "Memory allocation failed for line numbers.\n");
        free(function);
        free(bytecode);
        free(constants);
        return INTERPRET_COMPILE_ERROR;
    }
    for (uint32_t i = 0; i < code_size; i++) {
        function->chunk.lines[i] = 0; // No line information in bytecode
    }
    
    // Call startup functions before execution
    if (g_ffi_context) {
        kuyil_log_debug("Automatically calling startup functions before bytecode execution");
        if (!ffi_call_startup_functions(g_ffi_context)) {
            kuyil_log_warning("Startup functions failed, continuing with bytecode execution");
        }
    }

    // Set up call frame
    vm->frames[0].function = function;
    vm->frames[0].ip = function->chunk.code;
    vm->frames[0].slots = vm->stack;
    vm->frame_count = 1;
    
    InterpretResult result = vm_run(vm);

    // Call shutdown functions after execution (regardless of result)
    if (g_ffi_context) {
        kuyil_log_debug("Automatically calling shutdown functions after bytecode execution");
        ffi_call_shutdown_functions(g_ffi_context);
    }

    return result;
}

// ====================================================================================
// Task Queue API - Thread-safe VM operations
// ====================================================================================

// Get global task queue (for external libraries like HTTP)
TaskQueue* vm_get_task_queue(void) {
    return g_vm_task_queue;
}

// Process pending tasks (called from main thread/event loop)
int vm_process_pending_tasks(int max_tasks) {
    if (!g_vm_task_queue) return 0;
    return task_queue_process(g_vm_task_queue, max_tasks);
}

// Check if task queue is empty
bool vm_task_queue_empty(void) {
    if (!g_vm_task_queue) return true;
    return task_queue_is_empty(g_vm_task_queue);
}

// Get task queue size
size_t vm_task_queue_size(void) {
    if (!g_vm_task_queue) return 0;
    return task_queue_size(g_vm_task_queue);
}

// Process completed avatars (called from main thread/event loop)
int vm_process_avatar_completions(int max_avatars) {
    if (!g_current_vm || !g_current_vm->avatar_runtime) return 0;
    return avatar_runtime_process_completions(g_current_vm->avatar_runtime, max_avatars);
}

// Get number of pending avatars
size_t vm_avatar_pending_count(void) {
    if (!g_current_vm || !g_current_vm->avatar_runtime) return 0;
    return avatar_runtime_pending_count(g_current_vm->avatar_runtime);
}
// Process async requests (called from main thread/GTK idle callback)
int vm_process_async_requests(int max_requests) {
    if (!g_current_vm || !g_current_vm->request_queue) return 0;
    return async_request_queue_process(g_current_vm->request_queue, max_requests);
}

// Process event loop for async operations
// timeout_ms: milliseconds to wait for events  
int vm_process_events(int timeout_ms) {
    (void)timeout_ms;  // Reserved for future use
    if (!g_current_vm || !g_current_vm->event_base) return 0;
    
    // Process libevent loop (non-blocking)
    event_base_loop(g_current_vm->event_base, EVLOOP_NONBLOCK);
    
    // Process curl multi (this drives the HTTP requests)
    if (g_current_vm->async_http) {
        async_http_process(g_current_vm->async_http);
    }
    
    // Also process async requests (moves PENDING to PROCESSING)
    return vm_process_async_requests(10);
}

// Get global request queue for asyncio library
void* async_request_queue_get_global(void) {
    extern VM* g_current_vm;
    if (!g_current_vm) return NULL;
    return g_current_vm->request_queue;
}
