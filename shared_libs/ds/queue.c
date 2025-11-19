// Queue Implementation (FIFO)
#include <stdlib.h>
#include "../../src/ast.h"

typedef struct QueueNode {
    Value value;
    struct QueueNode* next;
} QueueNode;

typedef struct {
    QueueNode* front;
    QueueNode* rear;
    size_t size;
} Queue;

Value kyl_ds_queueCreate(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Queue* queue = malloc(sizeof(Queue));
    if (!queue) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)queue;
    return result;
}

Value kyl_ds_queueDestroy(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    while (queue->front) {
        QueueNode* node = queue->front;
        queue->front = node->next;
        free(node);
    }
    
    free(queue);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ds_queueEnqueue(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    QueueNode* node = malloc(sizeof(QueueNode));
    if (!node) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    node->value = args[1];
    node->next = NULL;
    
    if (queue->rear) {
        queue->rear->next = node;
    } else {
        queue->front = node;
    }
    queue->rear = node;
    queue->size++;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ds_queueDequeue(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue || !queue->front) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    QueueNode* node = queue->front;
    Value result = node->value;
    queue->front = node->next;
    
    if (!queue->front) {
        queue->rear = NULL;
    }
    
    queue->size--;
    free(node);
    
    return result;
}

Value kyl_ds_queuePeek(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue || !queue->front) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    return queue->front->value;
}

Value kyl_ds_queueSize(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NUMBER};
        err.as.number = 0;
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue) {
        Value err = {VALUE_NUMBER};
        err.as.number = 0;
        return err;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)queue->size;
    return result;
}

Value kyl_ds_queueIsEmpty(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_BOOL};
        err.as.boolean = true;
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue) {
        Value err = {VALUE_BOOL};
        err.as.boolean = true;
        return err;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (queue->size == 0);
    return result;
}

Value kyl_ds_queueClear(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    Queue* queue = (Queue*)(uintptr_t)args[0].as.number;
    if (!queue) {
        Value err = {VALUE_NIL};
        return err;
    }
    
    while (queue->front) {
        QueueNode* node = queue->front;
        queue->front = node->next;
        free(node);
    }
    
    queue->rear = NULL;
    queue->size = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
