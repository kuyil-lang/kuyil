// Mutex Primitives Implementation

#include "mutex_primitives.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Create a new mutex
Value builtin_mutex_create(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    KuyilMutex* mutex = malloc(sizeof(KuyilMutex));
    if (!mutex) {
        fprintf(stderr, "[MUTEX] Failed to allocate mutex\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    if (pthread_mutex_init(&mutex->mutex, NULL) != 0) {
        fprintf(stderr, "[MUTEX] Failed to initialize mutex\n");
        free(mutex);
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_NIL;
        return err;
    }
    
    mutex->initialized = true;
    mutex->lock_count = 0;
    
    // Return as opaque pointer wrapped in NUMBER (cast pointer to double)
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)mutex;
    
    return result;
}

// Lock mutex (blocking)
Value builtin_mutex_lock(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[MUTEX] mutex_lock requires mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    KuyilMutex* mutex = (KuyilMutex*)(uintptr_t)args[0].as.number;
    
    if (!mutex || !mutex->initialized) {
        fprintf(stderr, "[MUTEX] Invalid mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    int result = pthread_mutex_lock(&mutex->mutex);
    if (result == 0) {
        mutex->lock_count++;
    }
    
    Value ret;
    memset(&ret, 0, sizeof(Value));
    ret.type = VALUE_BOOL;
    ret.as.boolean = (result == 0);
    return ret;
}

// Unlock mutex
Value builtin_mutex_unlock(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[MUTEX] mutex_unlock requires mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    KuyilMutex* mutex = (KuyilMutex*)(uintptr_t)args[0].as.number;
    
    if (!mutex || !mutex->initialized) {
        fprintf(stderr, "[MUTEX] Invalid mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    int result = pthread_mutex_unlock(&mutex->mutex);
    
    Value ret;
    memset(&ret, 0, sizeof(Value));
    ret.type = VALUE_BOOL;
    ret.as.boolean = (result == 0);
    return ret;
}

// Try to lock mutex (non-blocking)
Value builtin_mutex_try_lock(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[MUTEX] mutex_try_lock requires mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    KuyilMutex* mutex = (KuyilMutex*)(uintptr_t)args[0].as.number;
    
    if (!mutex || !mutex->initialized) {
        fprintf(stderr, "[MUTEX] Invalid mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    int result = pthread_mutex_trylock(&mutex->mutex);
    if (result == 0) {
        mutex->lock_count++;
    }
    
    Value ret;
    memset(&ret, 0, sizeof(Value));
    ret.type = VALUE_BOOL;
    ret.as.boolean = (result == 0);
    return ret;
}

// Destroy mutex and free resources
Value builtin_mutex_destroy(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        fprintf(stderr, "[MUTEX] mutex_destroy requires mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    KuyilMutex* mutex = (KuyilMutex*)(uintptr_t)args[0].as.number;
    
    if (!mutex || !mutex->initialized) {
        fprintf(stderr, "[MUTEX] Invalid mutex handle\n");
        Value err;
        memset(&err, 0, sizeof(Value));
        err.type = VALUE_BOOL;
        err.as.boolean = false;
        return err;
    }
    
    mutex->initialized = false;
    pthread_mutex_destroy(&mutex->mutex);
    free(mutex);
    
    Value ret;
    memset(&ret, 0, sizeof(Value));
    ret.type = VALUE_BOOL;
    ret.as.boolean = true;
    return ret;
}
