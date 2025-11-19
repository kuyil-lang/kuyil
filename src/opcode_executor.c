// opcode_executor.c - Shared opcode execution implementation
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "opcode_executor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Helper: Check if value is falsy
static bool is_falsy(Value value) {
    return value.type == VALUE_NIL || 
           (value.type == VALUE_BOOL && !value.as.boolean);
}

// Helper: Value equality
bool values_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VALUE_NIL: return true;
        case VALUE_BOOL: return a.as.boolean == b.as.boolean;
        case VALUE_NUMBER: return a.as.number == b.as.number;
        case VALUE_STRING:
            return strcmp(a.as.string ? a.as.string : "", 
                         b.as.string ? b.as.string : "") == 0;
        default: return false; // Objects, functions compared by reference
    }
}

// OP_ADD: Number addition or string concatenation
bool exec_add(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in ADD");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type == VALUE_NUMBER && b.type == VALUE_NUMBER) {
        // Numeric addition
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
        result.as.number = a.as.number + b.as.number;
        exec_push(ctx, result);
        return true;
    } else if (a.type == VALUE_STRING || b.type == VALUE_STRING) {
        // String concatenation - convert numbers to strings if needed
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
        
        size_t len_a = strlen(str_a);
        size_t len_b = strlen(str_b);
        
        char* new_str = malloc(len_a + len_b + 1);
        if (!new_str) {
            *ctx->has_error = true;
            snprintf(ctx->error_message, ctx->error_msg_size, 
                    "Memory allocation failed in ADD");
            return false;
        }
        
        memcpy(new_str, str_a, len_a);
        memcpy(new_str + len_a, str_b, len_b + 1);
        
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = new_str;
        exec_push(ctx, result);
        return true;
    } else if (a.type == VALUE_ARRAY && b.type == VALUE_ARRAY) {
        // Array concatenation
        int new_count = a.as.array.count + b.as.array.count;
        Value* new_values = malloc(sizeof(Value) * new_count);
        if (!new_values) {
            *ctx->has_error = true;
            snprintf(ctx->error_message, ctx->error_msg_size, 
                    "Memory allocation failed in array ADD");
            return false;
        }
        
        // Copy elements from first array
        for (int i = 0; i < a.as.array.count; i++) {
            new_values[i] = a.as.array.values[i];
        }
        
        // Copy elements from second array
        for (int i = 0; i < b.as.array.count; i++) {
            new_values[a.as.array.count + i] = b.as.array.values[i];
        }
        
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
        result.as.array.count = new_count;
        result.as.array.values = new_values;
        exec_push(ctx, result);
        return true;
    } else {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be two numbers, two strings, or two arrays");
        return false;
    }
}

// OP_SUBTRACT: Number subtraction
bool exec_subtract(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in SUBTRACT");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = a.as.number - b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_MULTIPLY: Number multiplication
bool exec_multiply(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in MULTIPLY");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = a.as.number * b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_DIVIDE: Number division
bool exec_divide(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in DIVIDE");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    if (b.as.number == 0.0) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Division by zero");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = a.as.number / b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_MODULO: Number modulo
bool exec_modulo(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in MODULO");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    if (b.as.number == 0.0) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Modulo by zero");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    // Use fmod for floating point modulo
    result.as.number = fmod(a.as.number, b.as.number);
    exec_push(ctx, result);
    return true;
}

// OP_NEGATE: Numeric negation
bool exec_negate(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 1) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in NEGATE");
        return false;
    }
    
    Value val = exec_pop(ctx);
    
    if (val.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operand must be a number");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = -val.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_NOT: Logical negation
bool exec_not(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 1) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in NOT");
        return false;
    }
    
    Value val = exec_pop(ctx);
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = is_falsy(val);
    exec_push(ctx, result);
    return true;
}

// OP_AND: Logical AND
bool exec_and(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in AND");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    // Short-circuit evaluation: if a is falsy, return a; otherwise return b
    Value result;
    if (is_falsy(a)) {
        result = a;
    } else {
        result = b;
    }
    exec_push(ctx, result);
    return true;
}

// OP_OR: Logical OR
bool exec_or(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in OR");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    // Short-circuit evaluation: if a is truthy, return a; otherwise return b
    Value result;
    if (is_falsy(a)) {
        result = b;
    } else {
        result = a;
    }
    exec_push(ctx, result);
    return true;
}

// OP_EQUAL: Equality comparison
bool exec_equal(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in EQUAL");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = values_equal(a, b);
    exec_push(ctx, result);
    return true;
}

// OP_NOT_EQUAL: Inequality comparison
bool exec_not_equal(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in NOT_EQUAL");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = !values_equal(a, b);
    exec_push(ctx, result);
    return true;
}

// OP_GREATER: Greater than comparison
bool exec_greater(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in GREATER");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = a.as.number > b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_GREATER_EQUAL: Greater than or equal comparison
bool exec_greater_equal(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in GREATER_EQUAL");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = a.as.number >= b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_LESS: Less than comparison
bool exec_less(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in LESS");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = a.as.number < b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_LESS_EQUAL: Less than or equal comparison
bool exec_less_equal(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in LESS_EQUAL");
        return false;
    }
    
    Value b = exec_pop(ctx);
    Value a = exec_pop(ctx);
    
    if (a.type != VALUE_NUMBER || b.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Operands must be numbers");
        return false;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = a.as.number <= b.as.number;
    exec_push(ctx, result);
    return true;
}

// OP_ARRAY: Create array from N elements on stack
bool exec_array_create(ExecContext* ctx, uint8_t element_count) {
    if (*ctx->stack_top < ctx->stack + element_count) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Stack underflow in ARRAY (need %d elements)", element_count);
        return false;
    }
    
    // Allocate array structure
    ValueArray arr;
    arr.count = element_count;
    arr.values = malloc(sizeof(Value) * element_count);
    if (!arr.values && element_count > 0) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Memory allocation failed for array");
        return false;
    }
    
    // Pop elements from stack in reverse order (they were pushed left-to-right)
    for (int i = element_count - 1; i >= 0; i--) {
        arr.values[i] = exec_pop(ctx);
    }
    
    // Push array value
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array = arr;
    exec_push(ctx, result);
    return true;
}

// OP_ARRAY_GET: Get element from array by index
bool exec_array_get(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in ARRAY_GET");
        return false;
    }
    
    Value index = exec_pop(ctx);
    Value array = exec_pop(ctx);
    
    if (array.type != VALUE_ARRAY) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "ARRAY_GET requires array type, got %d", array.type);
        return false;
    }
    
    if (index.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Array index must be a number");
        return false;
    }
    
    int idx = (int)index.as.number;
    if (idx < 0 || idx >= array.as.array.count) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Array index out of bounds: %d (size: %d)", idx, array.as.array.count);
        return false;
    }
    
    Value result = array.as.array.values[idx];
    exec_push(ctx, result);
    return true;
}

// OP_ARRAY_SET: Set element in array by index
bool exec_array_set(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 3) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in ARRAY_SET");
        return false;
    }
    
    // Stack layout (top to bottom): index, array, value
    Value index = exec_pop(ctx);
    Value array = exec_pop(ctx);
    Value value = exec_pop(ctx);
    
    if (array.type != VALUE_ARRAY) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "ARRAY_SET requires array type");
        return false;
    }
    
    if (index.type != VALUE_NUMBER) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Array index must be a number");
        return false;
    }
    
    int idx = (int)index.as.number;
    if (idx < 0 || idx >= array.as.array.count) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Array index out of bounds: %d (size: %d)", idx, array.as.array.count);
        return false;
    }
    
    array.as.array.values[idx] = value;
    
    // Push the value back (assignment expression returns the value)
    exec_push(ctx, value);
    return true;
}

// OP_OBJECT_NEW: Create empty map/object
bool exec_object_new(ExecContext* ctx) {
    Value obj;
    memset(&obj, 0, sizeof(Value));
    obj.type = VALUE_OBJECT;
    obj.as.object.count = 0;
    obj.as.object.keys = NULL;
    obj.as.object.values = NULL;
    exec_push(ctx, obj);
    return true;
}

// OP_OBJECT_GET: Get value from map by key
bool exec_object_get(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 2) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in OBJECT_GET");
        return false;
    }
    
    Value key = exec_pop(ctx);
    Value object = exec_pop(ctx);
    
    if (key.type != VALUE_STRING) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Map key must be a string");
        return false;
    }
    
    // Handle array.length property
    if (object.type == VALUE_ARRAY) {
        if (strcmp(key.as.string, "length") == 0) {
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
            result.as.number = (double)object.as.array.count;
            exec_push(ctx, result);
            return true;
        }
        // Unknown property on array -> nil
        Value nilv = {VALUE_NIL};
        exec_push(ctx, nilv);
        return true;
    }
    
    // Handle string.length property
    if (object.type == VALUE_STRING) {
        if (strcmp(key.as.string, "length") == 0) {
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
            result.as.number = (double)strlen(object.as.string);
            exec_push(ctx, result);
            return true;
        }
        // Unknown property on string -> nil
        Value nilv = {VALUE_NIL};
        exec_push(ctx, nilv);
        return true;
    }
    
    if (object.type != VALUE_OBJECT) {
        // printf("[OBJECT_GET ERROR] Expected object but got type %d\n", object.type);
        // if (object.type == VALUE_STRING) {
        //     printf("[OBJECT_GET ERROR] String value: %.50s\n", object.as.string);
        // }
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Can only access properties on maps/objects");
        return false;
    }
    
    // DEBUG: Log object access in avatars
    // printf("[OBJECT_GET] Accessing key '%s' on object with %d properties\n", 
    //        key.as.string, object.as.object.count);
    
    // Search for the key in the map
    bool found = false;
    for (int i = 0; i < object.as.object.count; i++) {
        // printf("[OBJECT_GET] Checking key[%d] = '%s'\n", i, object.as.object.keys[i]);
        if (strcmp(object.as.object.keys[i], key.as.string) == 0) {
            Value val = object.as.object.values[i];
            // printf("[OBJECT_GET] FOUND! Type: %d\n", val.type);
            // if (val.type == VALUE_STRING) {
            //     size_t len = val.as.string ? strlen(val.as.string) : 0;
            //     printf("[OBJECT_GET] String ptr: %p, length: %zu\n", (void*)val.as.string, len);
            // }
            exec_push(ctx, val);
            found = true;
            break;
        }
    }
    
    if (!found) {
        // printf("[OBJECT_GET] Key '%s' NOT FOUND, returning nil\n", key.as.string);
        // Return nil for missing keys (like Go maps)
        Value nilv = {VALUE_NIL};
        exec_push(ctx, nilv);
    }
    
    return true;
}

// OP_OBJECT_SET: Set value in map by key
bool exec_object_set(ExecContext* ctx) {
    if (*ctx->stack_top < ctx->stack + 3) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, "Stack underflow in OBJECT_SET");
        return false;
    }
    
    Value value = exec_pop(ctx);
    Value key = exec_pop(ctx);
    Value object = exec_pop(ctx);
    
    if (object.type != VALUE_OBJECT) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Can only set properties on maps/objects");
        return false;
    }
    
    if (key.type != VALUE_STRING) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "Map key must be a string");
        return false;
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
    
    // If key doesn't exist, add it
    if (!found) {
        object.as.object.count++;
        object.as.object.keys = realloc(object.as.object.keys, 
                                        sizeof(char*) * object.as.object.count);
        object.as.object.values = realloc(object.as.object.values, 
                                          sizeof(Value) * object.as.object.count);
        if (!object.as.object.keys || !object.as.object.values) {
            *ctx->has_error = true;
            snprintf(ctx->error_message, ctx->error_msg_size, 
                    "Memory allocation failed in OBJECT_SET");
            return false;
        }
        object.as.object.keys[object.as.object.count - 1] = strdup(key.as.string);
        object.as.object.values[object.as.object.count - 1] = value;
    }
    
    // Push the modified object back
    exec_push(ctx, object);
    return true;
}

// Local variable operations
void exec_get_local(ExecContext* ctx, uint8_t slot) {
    if (!ctx->current_frame_slots) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "No frame slots available for GET_LOCAL");
        return;
    }
    
    Value val = ctx->current_frame_slots[slot];
    exec_push(ctx, val);
}

void exec_set_local(ExecContext* ctx, uint8_t slot) {
    if (!ctx->current_frame_slots) {
        *ctx->has_error = true;
        snprintf(ctx->error_message, ctx->error_msg_size, 
                "No frame slots available for SET_LOCAL");
        return;
    }
    
    // Peek value from stack (don't pop - matches VM behavior)
    Value val = exec_peek(ctx, 0);
    if (*ctx->has_error) return;
    
    ctx->current_frame_slots[slot] = val;
}

// OP_POP: Remove top value from stack
void exec_pop_discard(ExecContext* ctx) {
    if (*ctx->stack_top > ctx->stack) {
        (*ctx->stack_top)--;
    }
}

// OP_NIL: Push nil value
void exec_push_nil(ExecContext* ctx) {
    Value nil = {VALUE_NIL};
    exec_push(ctx, nil);
}

// OP_TRUE: Push true value
void exec_push_true(ExecContext* ctx) {
    Value val;
    memset(&val, 0, sizeof(Value));
    val.type = VALUE_BOOL;
    val.as.boolean = true;
    exec_push(ctx, val);
}

// OP_FALSE: Push false value
void exec_push_false(ExecContext* ctx) {
    Value val;
    memset(&val, 0, sizeof(Value));
    val.type = VALUE_BOOL;
    val.as.boolean = false;
    exec_push(ctx, val);
}
