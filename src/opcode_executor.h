// opcode_executor.h - Shared opcode execution logic
#ifndef OPCODE_EXECUTOR_H
#define OPCODE_EXECUTOR_H

#include "ast.h"
#include "bytecode.h"
#include <stdbool.h>

// Execution context - abstraction over VM and Avatar VM
typedef struct {
    Value* stack;           // Stack pointer
    Value** stack_top;      // Pointer to stack top pointer
    size_t stack_capacity;  // Stack capacity
    
    // Frame information for local variables
    Value* current_frame_slots;  // Pointer to current frame's slots
    
    // Error handling
    bool* has_error;        // Pointer to error flag
    char* error_message;    // Error message buffer
    size_t error_msg_size;  // Size of error message buffer
    
    // Context type (for special handling)
    enum { EXEC_CTX_VM, EXEC_CTX_AVATAR } type;
    void* vm_ptr;           // Pointer to VM or AvatarHandle
} ExecContext;

// Stack operations
static inline Value exec_pop(ExecContext* ctx) {
    if (*ctx->stack_top <= ctx->stack) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow");
        Value nil = {VALUE_NIL};
        return nil;
    }
    Value result = *(--(*ctx->stack_top));
    int new_size = *ctx->stack_top - ctx->stack;
    if (new_size < 4) {
        printf("[EXEC_POP WARNING] Stack shrunk to %d (corrupting locals!)\n", new_size);
    }
    return result;
}

static inline void exec_push(ExecContext* ctx, Value value) {
    if (*ctx->stack_top >= ctx->stack + ctx->stack_capacity) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack overflow");
        return;
    }
    *(*ctx->stack_top)++ = value;
}

static inline Value exec_peek(ExecContext* ctx, int distance) {
    if (*ctx->stack_top - distance - 1 < ctx->stack) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in peek");
        Value nil = {VALUE_NIL};
        return nil;
    }
    return (*ctx->stack_top)[-1 - distance];
}

// Opcode execution functions
bool exec_add(ExecContext* ctx);
bool exec_subtract(ExecContext* ctx);
bool exec_multiply(ExecContext* ctx);
bool exec_divide(ExecContext* ctx);
bool exec_modulo(ExecContext* ctx);
bool exec_negate(ExecContext* ctx);
bool exec_not(ExecContext* ctx);
bool exec_and(ExecContext* ctx);
bool exec_or(ExecContext* ctx);
bool exec_equal(ExecContext* ctx);
bool exec_greater(ExecContext* ctx);
bool exec_less(ExecContext* ctx);

// Comparison helpers
bool exec_not_equal(ExecContext* ctx);
bool exec_greater_equal(ExecContext* ctx);
bool exec_less_equal(ExecContext* ctx);

// Array operations
bool exec_array_create(ExecContext* ctx, uint8_t element_count);
bool exec_array_get(ExecContext* ctx);
bool exec_array_set(ExecContext* ctx);

// Map/Object operations  
bool exec_object_new(ExecContext* ctx);
bool exec_object_get(ExecContext* ctx);
bool exec_object_set(ExecContext* ctx);

// Local variable operations
void exec_get_local(ExecContext* ctx, uint8_t slot);
void exec_set_local(ExecContext* ctx, uint8_t slot);

// Stack manipulation
void exec_pop_discard(ExecContext* ctx);
void exec_push_nil(ExecContext* ctx);
void exec_push_true(ExecContext* ctx);
void exec_push_false(ExecContext* ctx);

// Helper for value comparison
bool values_equal(Value a, Value b);

#endif // OPCODE_EXECUTOR_H
