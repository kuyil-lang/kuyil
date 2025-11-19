// Stack Implementation for Kuyil Data Structures Library
#include <stdlib.h>
#include <string.h>
#include "../../src/ast.h"

typedef struct StackNode {
    Value value;
    struct StackNode* next;
} StackNode;

typedef struct {
    StackNode* top;
    size_t size;
} Stack;

// Create a new stack
Value kyl_ds_stackCreate(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Stack* stack = malloc(sizeof(Stack));
    if (!stack) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    stack->top = NULL;
    stack->size = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)stack;
    return result;
}

// Destroy stack
Value kyl_ds_stackDestroy(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    // Free all nodes
    while (stack->top) {
        StackNode* node = stack->top;
        stack->top = node->next;
        free(node);
    }
    
    free(stack);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Push value onto stack
Value kyl_ds_stackPush(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    StackNode* node = malloc(sizeof(StackNode));
    if (!node) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    node->value = args[1];
    node->next = stack->top;
    stack->top = node;
    stack->size++;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Pop value from stack
Value kyl_ds_stackPop(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack || !stack->top) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    StackNode* node = stack->top;
    Value result = node->value;
    stack->top = node->next;
    stack->size--;
    free(node);
    
    return result;
}

// Peek at top value
Value kyl_ds_stackPeek(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack || !stack->top) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    return stack->top->value;
}

// Get stack size
Value kyl_ds_stackSize(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NUMBER};
        err.as.number = 0;
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack) {
        Value err = {VALUE_NUMBER};
        err.as.number = 0;
        return err;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)stack->size;
    return result;
}

// Check if empty
Value kyl_ds_stackIsEmpty(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_BOOL};
        err.as.boolean = true;
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack) {
        Value err = {VALUE_BOOL};
        err.as.boolean = true;
        return err;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (stack->size == 0);
    return result;
}

// Clear stack
Value kyl_ds_stackClear(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Stack* stack = (Stack*)(uintptr_t)args[0].as.number;
    if (!stack) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    while (stack->top) {
        StackNode* node = stack->top;
        stack->top = node->next;
        free(node);
    }
    
    stack->size = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
