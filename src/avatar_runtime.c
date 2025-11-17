// avatar_runtime.c - Green thread runtime using thread pool
#define _POSIX_C_SOURCE 200809L
#include "avatar_runtime.h"
#include "thread_pool.h"
#include "vm.h"
#include "vm_library_integration.h"
#include "vm_call_shared.h"
#include "bytecode.h"
#include "opcode_executor.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

// Forward declarations
struct AvatarHandle;
struct AvatarRuntime;

// Avatar VM context for shared call operations
typedef struct {
    VM* avatar_vm;
    struct AvatarHandle* handle;
} AvatarCallContext;

// Avatar handle structure
struct AvatarHandle {
    Function* function;
    Value* args;
    int arg_count;
    Value result;
    AvatarCompletionCallback callback;
    void* user_data;
    void* vm_context;
    bool completed;
    bool has_error;
    char error_message[256];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct AvatarRuntime* runtime;
    struct AvatarHandle* next;
};

// Avatar runtime structure
struct AvatarRuntime {
    ThreadPool* thread_pool;
    struct AvatarHandle* handles;  // Linked list of active handles
    pthread_mutex_t handles_mutex;
    size_t pending_count;
};

// Helper functions for CallContext
static Value* avatar_peek(void* context, int distance) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    if (ctx->avatar_vm->stack_top - distance - 1 < ctx->avatar_vm->stack) {
        return NULL;
    }
    return ctx->avatar_vm->stack_top - distance - 1;
}

static Value avatar_pop(void* context) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    if (ctx->avatar_vm->stack_top <= ctx->avatar_vm->stack) {
        Value nilv = {VALUE_NIL};
        return nilv;
    }
    return *--ctx->avatar_vm->stack_top;
}

static void avatar_push(void* context, Value value) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    if (ctx->avatar_vm->stack_top < ctx->avatar_vm->stack + STACK_MAX) {
        *ctx->avatar_vm->stack_top++ = value;
    }
}

static Value* avatar_get_args(void* context, int arg_count) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    // Stack layout: [...] [arg0, arg1, ..., argN-1, callee] <- stack_top
    return ctx->avatar_vm->stack_top - arg_count - 1;
}

static void avatar_pop_n(void* context, int n) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    ctx->avatar_vm->stack_top -= n;
    if (ctx->avatar_vm->stack_top < ctx->avatar_vm->stack) {
        ctx->avatar_vm->stack_top = ctx->avatar_vm->stack;
    }
}

static void avatar_report_error(void* context, const char* message) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    ctx->handle->has_error = true;
    snprintf(ctx->handle->error_message, sizeof(ctx->handle->error_message), "%s", message);
    fprintf(stderr, "[AVATAR RUNTIME ERROR] %s\n", message);
    fflush(stderr);
}

// Helper to set error with immediate reporting
static void avatar_set_error(AvatarHandle* handle, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(handle->error_message, sizeof(handle->error_message), format, args);
    va_end(args);
    
    handle->has_error = true;
    fprintf(stderr, "[AVATAR RUNTIME ERROR] %s\n", handle->error_message);
    fflush(stderr);
}

static bool avatar_setup_frame(void* context, Function* function, int arg_count) {
    AvatarCallContext* ctx = (AvatarCallContext*)context;
    CallFrame* frame = vm_setup_call_frame_ex(ctx->avatar_vm->frames, &ctx->avatar_vm->frame_count,
                                                &ctx->avatar_vm->stack_top, function, arg_count, true);
    return frame != NULL;
}

static bool avatar_get_global(void* context, const char* name, Value* out) {
    // Avatars don't have direct global access in the current design
    (void)context;
    (void)name;
    (void)out;
    return false;
}

// Task function executed on worker thread
static void* avatar_task_func(void* user_data) {
    AvatarHandle* handle = (AvatarHandle*)user_data;
    
    // Get main VM context for accessing globals
    VM* main_vm = (VM*)handle->vm_context;
    
    // Create isolated VM instance for this avatar
    VM avatar_vm;
    memset(&avatar_vm, 0, sizeof(VM));
    
    // Initialize minimal VM state with full call stack support
    avatar_vm.stack_top = avatar_vm.stack;
    avatar_vm.frame_count = 0;
    avatar_vm.test_mode = false;
    avatar_vm.avatar_runtime = NULL;  // Avatars don't spawn sub-avatars
    avatar_vm.async_http = NULL;
    avatar_vm.event_base = NULL;
    avatar_vm.global_count = 0;
    
    // Note: We'll access main_vm->globals directly when needed (read-only)
    
    // Set up initial call frame for function execution
    // IMPORTANT: This must match how call_value() sets up frames in vm.c!
    // The main difference from top-level execution is that we have arguments.
    if (avatar_vm.frame_count >= FRAMES_MAX) {
        avatar_set_error(handle, "Avatar stack overflow");
        handle->result.type = VALUE_NIL;
        return NULL;
    }
    
    // Push arguments onto stack first
    for (int i = 0; i < handle->arg_count; i++) {
        if (avatar_vm.stack_top >= avatar_vm.stack + STACK_MAX) {
            avatar_set_error(handle, "Avatar stack overflow (args)");
            handle->result.type = VALUE_NIL;
            return NULL;
        }
        *avatar_vm.stack_top++ = handle->args[i];
    }
    
    // Set up initial frame - must match vm_setup_call_frame_ex logic!
    // Arguments are already on stack, now set up frame properly
    avatar_vm.frames[0].function = handle->function;
    avatar_vm.frames[0].ip = handle->function->chunk.code;
    avatar_vm.frames[0].slots = avatar_vm.stack;  // Points to arg[0]
    avatar_vm.frame_count = 1;
    
    // CRITICAL: With proper SET_LOCAL/GET_LOCAL implementation:
    // - frame->slots[0..arg_count-1] = parameters (already on stack)
    // - frame->slots[arg_count..local_count-1] = local variables (need stack space!)
    // - Must allocate stack space for ALL locals, not just arguments
    // - Initialize local variable slots to NIL
    int local_count = handle->function->local_count;
    avatar_vm.stack_top = avatar_vm.stack + handle->arg_count;
    
    // printf("[AVATAR SETUP] Function has local_count=%d, arg_count=%d, allocating %d local slots\n",
    //        local_count, handle->arg_count, local_count - handle->arg_count);
    
    // Allocate and initialize local variable slots (beyond parameters)
    for (int i = handle->arg_count; i < local_count; i++) {
        if (avatar_vm.stack_top >= avatar_vm.stack + STACK_MAX) {
            snprintf(handle->error_message, sizeof(handle->error_message),
                    "Avatar stack overflow (locals)");
            handle->has_error = true;
            handle->result.type = VALUE_NIL;
            return NULL;
        }
        Value nil_val = {VALUE_NIL};
        *avatar_vm.stack_top++ = nil_val;
    }
    
    // printf("[AVATAR SETUP] Stack_top after locals = %ld (base=0, top=%ld)\n",
    //        (long)(avatar_vm.stack_top - avatar_vm.stack), (long)(avatar_vm.stack_top - avatar_vm.stack));
    
    // Now stack_top points after all locals - this is where expression evaluation starts
    // This matches vm_setup_call_frame_ex: *stack_top_ptr = frame->slots + arg_count;
    
    // With proper SET_LOCAL/GET_LOCAL implementation:
    // - Locals are stored in frame->slots[], indexed by slot number
    // - Expression stack is separate, starting at frame->slots + arg_count
    // - No need to track local initialization or protect from POP operations
    
    // Add temp stack reserve ONLY for nested calls, not the initial frame
    // Local variables need contiguous space starting from frame->slots + arg_count
    #define AVATAR_TEMP_STACK_RESERVE 16
    
    // Execute the function bytecode with full call stack support
    // This run loop now supports nested calls, recursion, and while loops
    bool running = true;
    int instruction_count = 0;
    const int MAX_INSTRUCTIONS = 100000;  // Increased limit for loops
    CallFrame* frame;  // Current executing frame
    
    // Set up execution context for shared opcode executor
    ExecContext exec_ctx = {
        .stack = avatar_vm.stack,
        .stack_top = &avatar_vm.stack_top,
        .stack_capacity = STACK_MAX,
        .has_error = &handle->has_error,
        .error_message = handle->error_message,
        .error_msg_size = sizeof(handle->error_message),
        .type = EXEC_CTX_AVATAR,
        .vm_ptr = handle
    };
    
    while (running && avatar_vm.frame_count > 0) {
        // Get current frame (it may change due to calls/returns)
        // Disabled: Frame count tracking
        /*
        static int prev_frame_count = 0;
        if (avatar_vm.frame_count != prev_frame_count) {
            printf("[AVATAR] Frame count changed from %d to %d\n", prev_frame_count, avatar_vm.frame_count);
            prev_frame_count = avatar_vm.frame_count;
        }
        */
        frame = &avatar_vm.frames[avatar_vm.frame_count - 1];
        
        // Update exec context with current frame slots
        exec_ctx.current_frame_slots = frame->slots;
        
        if (frame->ip >= frame->function->chunk.code + frame->function->chunk.count) {
            // Reached end of function without explicit return - return nil
            handle->result.type = VALUE_NIL;
            avatar_vm.frame_count--;
            if (avatar_vm.frame_count == 0) {
                running = false;
            }
            continue;
        }
        
        
        uint8_t instruction = *frame->ip++;
        instruction_count++;
        
        // Track execution flow with slot[0] state (disabled for performance)
        /*
        if (avatar_vm.frame_count == 2 || avatar_vm.frame_count == 3) {
            long ip_offset = (frame->ip - 1) - frame->function->chunk.code;
            printf("[AVATAR F%d] IP=%ld op=%d, slot[0]=%g, stack_delta=%ld\n",
                   avatar_vm.frame_count, ip_offset, instruction,
                   frame->slots[0].type == VALUE_NUMBER ? frame->slots[0].as.number : -999.0,
                   avatar_vm.stack_top - frame->slots);
        }
        */
        
        if (instruction_count > MAX_INSTRUCTIONS) {
            avatar_set_error(handle, "Avatar exceeded instruction limit (%d instructions)", MAX_INSTRUCTIONS);
            handle->result.type = VALUE_NIL;
            return NULL;
        }
        
        switch (instruction) {
            case OP_RETURN: {
                // printf("[AVATAR] Executing OP_RETURN\n");
                // Pop return value from stack
                Value result = exec_pop(&exec_ctx);
                if (*exec_ctx.has_error) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                // DEBUG: Log what we're returning
                // printf("[AVATAR OP_RETURN] Type: %d\n", result.type);
                // if (result.type == VALUE_STRING && result.as.string) {
                //     size_t len = strlen(result.as.string);
                //     printf("[AVATAR OP_RETURN] String ptr: %p, length: %zu\n", (void*)result.as.string, len);
                //     printf("[AVATAR OP_RETURN] String (first 50 chars): %.50s\n", result.as.string);
                // }
                
                // Decrement frame count
                avatar_vm.frame_count--;
                
                // Save slots pointer of returning frame for stack restoration
                Value* returning_frame_slots = frame->slots;
                
                if (avatar_vm.frame_count == 0) {
                    // Top-level return - set result and exit
                    handle->result = result;
                    // if (result.type == VALUE_STRING && result.as.string) {
                    //     size_t len = strlen(result.as.string);
                    //     printf("[AVATAR OP_RETURN] Saved to handle->result, string length: %zu\n", len);
                    // } else {
                    //     printf("[AVATAR OP_RETURN] Saved to handle->result, type: %d\n", result.type);
                    // }
                    running = false;
                } else {
                    // Returning from nested call - restore stack to where callee was
                    // This preserves values that were pushed before the CALL
                    avatar_vm.stack_top = returning_frame_slots;
                    *avatar_vm.stack_top++ = result;
                }
                break;
            }
            
            case OP_CONSTANT: {
                uint8_t constant_idx = *frame->ip++;
                if (constant_idx >= frame->function->chunk.constant_count) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Invalid constant index");
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                Value val = frame->function->chunk.constants[constant_idx];
                *avatar_vm.stack_top++ = val;
                break;
            }
            
            case OP_ADD:
                if (!exec_add(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_SUBTRACT:
                if (!exec_subtract(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_MULTIPLY:
                if (!exec_multiply(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_DIVIDE:
                if (!exec_divide(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_MODULO:
                if (!exec_modulo(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_NEGATE:
                if (!exec_negate(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;

            case OP_TO_STRING: {
                // Convert top-of-stack value to string (mirror vm.c implementation)
                if (avatar_vm.stack_top <= avatar_vm.stack) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                             "Stack underflow in TO_STRING");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                Value value = *(avatar_vm.stack_top - 1);
                // Pop original value
                avatar_vm.stack_top--;

                Value result;
                result.type = VALUE_STRING;
                switch (value.type) {
                    case VALUE_STRING:
                        result.as.string = value.as.string; // Already string
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
                // Push converted value
                *avatar_vm.stack_top++ = result;
                break;
            }
            
            case OP_NOT:
                if (!exec_not(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_AND:
                if (!exec_and(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_OR:
                if (!exec_or(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_EQUAL:
                if (!exec_equal(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_NOT_EQUAL:
                if (!exec_not_equal(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_GREATER:
                if (!exec_greater(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_GREATER_EQUAL:
                if (!exec_greater_equal(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_LESS:
                if (!exec_less(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_LESS_EQUAL:
                if (!exec_less_equal(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_POP:
                // CRITICAL: With frame->slots pointing to the stack:
                // - Locals occupy stack[0..local_count-1]
                // - Expression stack starts at stack[local_count]
                // - Expression stack can shrink to local_count, but NOT BELOW
                // - If we allow stack to shrink below local_count, next push overwrites locals!
                {
                    int current_stack_pos = avatar_vm.stack_top - avatar_vm.stack;
                    int min_stack_pos = handle->function->local_count;
                    
                    if (current_stack_pos <= min_stack_pos) {
                        // printf("[AVATAR OP_POP] BLOCKED: stack_pos=%d, cannot go below local_count=%d\n",
                        //        current_stack_pos, min_stack_pos);
                        // Don't pop - stack is already at minimum safe level
                        break;
                    }
                    
                    exec_pop_discard(&exec_ctx);
                }
                break;
            
            case OP_PRINT: {
                if (avatar_vm.stack_top < avatar_vm.stack + 1) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Stack underflow in PRINT");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                // Print from avatar (thread-safe with mutex or just allow interleaving)
                Value value = *(avatar_vm.stack_top - 1);
                avatar_vm.stack_top--;
                
                // Simple printing - may interleave with other output
                // printf("[AVATAR OUTPUT] ");
                switch (value.type) {
                    case VALUE_NIL:
                        printf("nil\n");
                        break;
                    case VALUE_BOOL:
                        printf("%s\n", value.as.boolean ? "true" : "false");
                        break;
                    case VALUE_NUMBER:
                        printf("%g\n", value.as.number);
                        break;
                    case VALUE_STRING:
                        printf("%s\n", value.as.string ? value.as.string : "<null>");
                        break;
                    default:
                        printf("<value type=%d>\n", value.type);
                        break;
                }
                break;
            }
            
            case OP_GET_LOCAL: {
                uint8_t slot = *frame->ip++;
                exec_ctx.current_frame_slots = frame->slots;
                // DEBUG: Log what we're loading
                // Value local_val = frame->slots[slot];
                // int stack_size = avatar_vm.stack_top - avatar_vm.stack;
                // printf("[AVATAR GET_LOCAL] Slot %d, type: %d, stack_size: %d\n", slot, local_val.type, stack_size);
                exec_get_local(&exec_ctx, slot);
                if (*exec_ctx.has_error) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            }
            
            case OP_SET_LOCAL: {
                uint8_t slot = *frame->ip++;
                exec_ctx.current_frame_slots = frame->slots;
                // DEBUG: Log what we're setting
                // Value top_val = avatar_vm.stack_top[-1];
                // int stack_pos = avatar_vm.stack_top - avatar_vm.stack;
                // printf("[AVATAR SET_LOCAL] Slot %d = type %d, stack_pos=%d, slot_addr=%p, stack_addr=%p\n", 
                //        slot, top_val.type, stack_pos, 
                //        (void*)&frame->slots[slot], (void*)(avatar_vm.stack_top - 1));
                exec_set_local(&exec_ctx, slot);
                if (*exec_ctx.has_error) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
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
            
            case OP_GET_GLOBAL: {
                // Get global variable from main VM's globals (read-only)
                // Read the constant pool index (which contains the variable name)
                uint8_t constant_idx = *frame->ip++;
                
                // Get the name from the constant pool
                if (constant_idx >= frame->function->chunk.constant_count) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Constant index out of bounds: %d", constant_idx);
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                Value name_value = frame->function->chunk.constants[constant_idx];
                if (name_value.type != VALUE_STRING) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Global name is not a string");
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                const char* name = name_value.as.string;
                
                // Use shared lookup logic (checks dynamic functions first, then globals)
                Value value = vm_lookup_function_shared(name, main_vm);
                
                if (value.type == VALUE_NIL) {
                    avatar_set_error(handle, "Undefined global variable: %s", name);
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                *avatar_vm.stack_top++ = value;
                break;
            }
            
            case OP_SET_GLOBAL: {
                // Set global variable (write to main VM's globals)
                uint8_t constant_idx = *frame->ip++;
                
                // Get the name from the constant pool
                if (constant_idx >= frame->function->chunk.constant_count) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Constant index out of bounds: %d", constant_idx);
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                Value name_value = frame->function->chunk.constants[constant_idx];
                if (name_value.type != VALUE_STRING) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Global name is not a string");
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                const char* name = name_value.as.string;
                
                // Pop the value to set
                if (avatar_vm.stack_top <= avatar_vm.stack) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Stack underflow in SET_GLOBAL");
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                Value value = *(--avatar_vm.stack_top);
                
                // Find the global in main VM and set it
                int index = -1;
                for (int i = 0; i < main_vm->global_count; i++) {
                    if (strcmp(main_vm->globals[i].name, name) == 0) {
                        index = i;
                        break;
                    }
                }
                
                if (index == -1) {
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Undefined variable '%s'", name);
                    handle->has_error = true;
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                // Set the global value
                main_vm->globals[index].value = value;
                
                // Push the value back (SET_GLOBAL returns the value)
                *avatar_vm.stack_top++ = value;
                break;
            }
            
            case OP_CALL: {
                // OP_CALL: call a function - now with full call stack support!
                uint8_t arg_count = *frame->ip++;
                
                // Get callee from top of stack (peek at position 0)
                // Stack layout: [... arg0, arg1, ..., argN, callee] <- stack_top
                if (avatar_vm.stack_top < avatar_vm.stack + arg_count + 1) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Stack underflow in CALL");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                Value callee = *(avatar_vm.stack_top - 1);  // Peek at top of stack
                
                // Handle VALUE_FUNCTION (user-defined functions)
                if (callee.type == VALUE_FUNCTION) {
                    Function* function = callee.as.function.function;
                    
                    // Check arity
                    if (arg_count != function->arity) {
                        handle->has_error = true;
                        snprintf(handle->error_message, sizeof(handle->error_message),
                                "Expected %d arguments but got %d", function->arity, arg_count);
                        handle->result.type = VALUE_NIL;
                        return NULL;
                    }
                    
                    // Check if we have room for another call frame
                    if (avatar_vm.frame_count >= FRAMES_MAX) {
                        handle->has_error = true;
                        snprintf(handle->error_message, sizeof(handle->error_message),
                                "Avatar call stack overflow (max depth: %d)", FRAMES_MAX);
                        handle->result.type = VALUE_NIL;
                        return NULL;
                    }
                    
                    // Create new call frame using shared logic with stack reserve for avatars
                    CallFrame* new_frame = vm_setup_call_frame_ex(avatar_vm.frames, &avatar_vm.frame_count,
                                                                    &avatar_vm.stack_top, function, arg_count, true);
                    if (!new_frame) {
                        handle->has_error = true;
                        snprintf(handle->error_message, sizeof(handle->error_message),
                                "Avatar call stack overflow");
                        handle->result.type = VALUE_NIL;
                        return NULL;
                    }
                    
                    // The frame pointer will be updated at the top of the loop
                    break;
                }
                // Handle VALUE_STRING (library functions and special functions)
                else if (callee.type == VALUE_STRING) {
                    // Set up call context for shared call handler
                    AvatarCallContext avatar_ctx = {
                        .avatar_vm = &avatar_vm,
                        .handle = handle
                    };
                    
                    CallContext ctx = {
                        .peek = avatar_peek,
                        .pop = avatar_pop,
                        .push = avatar_push,
                        .get_args = avatar_get_args,
                        .pop_n = avatar_pop_n,
                        .report_error = avatar_report_error,
                        .setup_frame = avatar_setup_frame,
                        .context = &avatar_ctx,
                        .get_global = avatar_get_global,
                        .test_mode = false
                    };
                    
                    
                    CallResult result = vm_call_string_shared(&ctx, callee.as.string, arg_count);
                    
                    if (result == CALL_RESULT_ERROR) {
                        // Error already set by shared handler (already prints via avatar_report_error)
                        handle->result.type = VALUE_NIL;
                        return NULL;
                    } else if (result == CALL_RESULT_OK) {
                        // Successfully handled
                        break;
                    }
                    // CALL_RESULT_NOT_HANDLED: fall through to error
                    
                    avatar_set_error(handle, "Unknown function in avatar: %s", callee.as.string);
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                else {
                    // Unsupported callee type
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Unsupported callee type: %d", callee.type);
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            }
            
            case OP_JUMP: {
                uint16_t offset = *frame->ip++ << 8;
                offset |= *frame->ip++;
                frame->ip += offset;
                // printf("[AVATAR] JUMP forward by %d\n", offset);
                break;
            }
            
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = *frame->ip++ << 8;
                offset |= *frame->ip++;
                
                // Peek at top of stack (don't pop - let subsequent OP_POP handle cleanup)
                if (avatar_vm.stack_top < avatar_vm.stack + 1) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Stack underflow in JUMP_IF_FALSE");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                
                Value condition = *(avatar_vm.stack_top - 1);  // Peek only
                
                // Check if falsy (nil, false, or 0)
                bool is_falsy = false;
                if (condition.type == VALUE_NIL) {
                    is_falsy = true;
                } else if (condition.type == VALUE_BOOL && !condition.as.boolean) {
                    is_falsy = true;
                } else if (condition.type == VALUE_NUMBER && condition.as.number == 0.0) {
                    is_falsy = true;
                }
                
                if (is_falsy) {
                    frame->ip += offset;
                    // When jumping, pop the condition value
                    avatar_vm.stack_top--;
                } 
                // When not jumping, leave value for OP_POP to clean
                break;
            }
            
            case OP_LOOP: {
                uint16_t offset = *frame->ip++ << 8;
                offset |= *frame->ip++;
                frame->ip -= offset;
                break;
            }
            
            case OP_ARRAY: {
                uint8_t element_count = *frame->ip++;
                if (!exec_array_create(&exec_ctx, element_count)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            }
            
            case OP_ARRAY_GET:
                if (!exec_array_get(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_ARRAY_SET:
                if (!exec_array_set(&exec_ctx)) {
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_OBJECT_NEW:
                // Create new empty map/object
                if (!exec_object_new(&exec_ctx)) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Error in OBJECT_NEW");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            case OP_OBJECT_GET:
                // Get property from map/object or namespace access
                if (!exec_object_get(&exec_ctx)) {
                    handle->has_error = true;
                    // printf("[AVATAR] OBJECT_GET FAILED: %s\n", exec_ctx.error_message);
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Error in OBJECT_GET: %s", exec_ctx.error_message);
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                // DEBUG: Check what's on stack after OBJECT_GET
                // if (avatar_vm.stack_top > avatar_vm.stack) {
                //     Value top = avatar_vm.stack_top[-1];
                //     printf("[AVATAR POST-OBJECT_GET] Stack top type: %d\n", top.type);
                //     if (top.type == VALUE_STRING && top.as.string) {
                //         printf("[AVATAR POST-OBJECT_GET] String length: %zu\n", strlen(top.as.string));
                //     }
                // }
                break;
            
            case OP_OBJECT_SET:
                // Set property in map/object
                if (!exec_object_set(&exec_ctx)) {
                    handle->has_error = true;
                    snprintf(handle->error_message, sizeof(handle->error_message),
                            "Error in OBJECT_SET");
                    handle->result.type = VALUE_NIL;
                    return NULL;
                }
                break;
            
            default:
                // Unsupported opcode
                snprintf(handle->error_message, sizeof(handle->error_message),
                        "Unsupported opcode in avatar: 0x%02x (%d)", instruction, instruction);
                handle->has_error = true;
                handle->result.type = VALUE_NIL;
                printf("[AVATAR] ERROR: Unsupported opcode: 0x%02x (%d)\n", instruction, instruction);
                running = false;  // Stop execution
                break;
        }
    }
    
    // If we exit the loop without explicit return, return nil
    if (running) {
        printf("[AVATAR] Loop exited with running=true, setting nil result\n");
        handle->result.type = VALUE_NIL;
    }
    
    return NULL;
}

// Completion callback executed on main thread
static void avatar_completion_func(void* completion_data, void* result) {
    AvatarHandle* handle = (AvatarHandle*)completion_data;
    
    pthread_mutex_lock(&handle->mutex);
    handle->completed = true;
    pthread_cond_broadcast(&handle->cond);
    pthread_mutex_unlock(&handle->mutex);
    
    // Invoke user callback if provided
    if (handle->callback) {
        handle->callback(handle, handle->result, handle->user_data);
    }
}

AvatarRuntime* avatar_runtime_create(size_t num_threads) {
    AvatarRuntime* runtime = calloc(1, sizeof(AvatarRuntime));
    if (!runtime) return NULL;
    
    runtime->thread_pool = thread_pool_create(num_threads);
    if (!runtime->thread_pool) {
        free(runtime);
        return NULL;
    }
    
    runtime->handles = NULL;
    runtime->pending_count = 0;
    pthread_mutex_init(&runtime->handles_mutex, NULL);
    
    return runtime;
}

void avatar_runtime_destroy(AvatarRuntime* runtime) {
    if (!runtime) return;
    
    // Wait for all avatars to complete
    thread_pool_destroy(runtime->thread_pool);
    
    // Free all handles
    pthread_mutex_lock(&runtime->handles_mutex);
    while (runtime->handles) {
        AvatarHandle* handle = runtime->handles;
        runtime->handles = handle->next;
        
        pthread_mutex_destroy(&handle->mutex);
        pthread_cond_destroy(&handle->cond);
        free(handle->args);
        free(handle);
    }
    pthread_mutex_unlock(&runtime->handles_mutex);
    
    pthread_mutex_destroy(&runtime->handles_mutex);
    free(runtime);
}

AvatarHandle* avatar_runtime_submit(
    AvatarRuntime* runtime,
    Function* function,
    Value* args,
    int arg_count,
    void* vm_context,
    AvatarCompletionCallback callback,
    void* user_data
) {
    if (!runtime || !function) return NULL;
    
    AvatarHandle* handle = calloc(1, sizeof(AvatarHandle));
    if (!handle) return NULL;
    
    handle->function = function;
    handle->arg_count = arg_count;
    handle->callback = callback;
    handle->user_data = user_data;
    handle->vm_context = vm_context;
    handle->completed = false;
    handle->runtime = runtime;
    handle->result.type = VALUE_NIL;
    
    // Copy arguments
    if (arg_count > 0) {
        handle->args = malloc(sizeof(Value) * arg_count);
        if (!handle->args) {
            free(handle);
            return NULL;
        }
        memcpy(handle->args, args, sizeof(Value) * arg_count);
    } else {
        handle->args = NULL;
    }
    
    pthread_mutex_init(&handle->mutex, NULL);
    pthread_cond_init(&handle->cond, NULL);
    
    // Add to handles list
    pthread_mutex_lock(&runtime->handles_mutex);
    handle->next = runtime->handles;
    runtime->handles = handle;
    runtime->pending_count++;
    pthread_mutex_unlock(&runtime->handles_mutex);
    
    // Submit to thread pool
    if (!thread_pool_submit(runtime->thread_pool, avatar_task_func, handle,
                           avatar_completion_func, handle)) {
        // Submission failed - cleanup
        pthread_mutex_lock(&runtime->handles_mutex);
        
        // Remove from list
        if (runtime->handles == handle) {
            runtime->handles = handle->next;
        } else {
            AvatarHandle* prev = runtime->handles;
            while (prev && prev->next != handle) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = handle->next;
            }
        }
        runtime->pending_count--;
        
        pthread_mutex_unlock(&runtime->handles_mutex);
        
        pthread_mutex_destroy(&handle->mutex);
        pthread_cond_destroy(&handle->cond);
        free(handle->args);
        free(handle);
        return NULL;
    }
    
    return handle;
}

int avatar_runtime_process_completions(AvatarRuntime* runtime, int max_avatars) {
    if (!runtime) return 0;
    
    // Process completions from thread pool
    return thread_pool_process_completions(runtime->thread_pool, max_avatars);
}

void avatar_runtime_cancel(AvatarHandle* handle) {
    if (!handle) return;
    
    // Mark as completed to unblock waiters
    pthread_mutex_lock(&handle->mutex);
    handle->completed = true;
    pthread_cond_broadcast(&handle->cond);
    pthread_mutex_unlock(&handle->mutex);
}

bool avatar_runtime_await(AvatarHandle* handle, int timeout_ms, Value* result) {
    if (!handle) return false;
    
    pthread_mutex_lock(&handle->mutex);
    
    if (timeout_ms == 0) {
        // Wait indefinitely
        while (!handle->completed) {
            pthread_cond_wait(&handle->cond, &handle->mutex);
        }
    } else {
        // Wait with timeout
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        while (!handle->completed) {
            if (pthread_cond_timedwait(&handle->cond, &handle->mutex, &ts) != 0) {
                // Timeout
                pthread_mutex_unlock(&handle->mutex);
                return false;
            }
        }
    }
    
    if (result) {
        *result = handle->result;
    }
    
    pthread_mutex_unlock(&handle->mutex);
    return true;
}

size_t avatar_runtime_pending_count(AvatarRuntime* runtime) {
    if (!runtime) return 0;
    
    pthread_mutex_lock(&runtime->handles_mutex);
    size_t count = runtime->pending_count;
    pthread_mutex_unlock(&runtime->handles_mutex);
    
    return count;
}

size_t avatar_runtime_thread_count(AvatarRuntime* runtime) {
    if (!runtime) return 0;
    return thread_pool_thread_count(runtime->thread_pool);
}

bool avatar_runtime_is_complete(AvatarHandle* handle) {
    if (!handle) return true;
    
    pthread_mutex_lock(&handle->mutex);
    bool completed = handle->completed;
    pthread_mutex_unlock(&handle->mutex);
    
    return completed;
}

bool avatar_runtime_has_error(AvatarHandle* handle) {
    if (!handle) return false;
    
    pthread_mutex_lock(&handle->mutex);
    bool has_error = handle->has_error;
    pthread_mutex_unlock(&handle->mutex);
    
    return has_error;
}

const char* avatar_runtime_get_error(AvatarHandle* handle) {
    if (!handle) return NULL;
    
    pthread_mutex_lock(&handle->mutex);
    const char* error = handle->has_error ? handle->error_message : NULL;
    pthread_mutex_unlock(&handle->mutex);
    
    return error;
}

Value avatar_runtime_get_result(AvatarHandle* handle) {
    Value nil = {VALUE_NIL};
    if (!handle) return nil;
    
    pthread_mutex_lock(&handle->mutex);
    
    // Check for errors
    if (handle->has_error) {
        printf("[AVATAR] ERROR: %s\n", handle->error_message);
    }
    
    Value result = handle->result;
    
    // DEBUG: Log result details
    printf("[AVATAR GET_RESULT] Type: %d\n", result.type);
    if (result.type == VALUE_STRING && result.as.string) {
        size_t len = strlen(result.as.string);
        printf("[AVATAR GET_RESULT] String ptr: %p, length: %zu\n", (void*)result.as.string, len);
        printf("[AVATAR GET_RESULT] String (first 50 chars): %.50s\n", result.as.string);
    }
    
    pthread_mutex_unlock(&handle->mutex);
    
    return result;
}
