// Mutex Primitives for Thread-Safe Operations
// Provides pthread mutex wrappers for Kuyil avatar concurrency

#ifndef MUTEX_PRIMITIVES_H
#define MUTEX_PRIMITIVES_H

#include "bytecode.h"
#include <pthread.h>
#include <stdbool.h>

// Mutex handle stored as opaque pointer in Value
typedef struct {
    pthread_mutex_t mutex;
    bool initialized;
    int lock_count;  // For debugging/statistics
} KuyilMutex;

// Built-in functions for VM
Value builtin_mutex_create(int arg_count, Value* args);
Value builtin_mutex_lock(int arg_count, Value* args);
Value builtin_mutex_unlock(int arg_count, Value* args);
Value builtin_mutex_try_lock(int arg_count, Value* args);
Value builtin_mutex_destroy(int arg_count, Value* args);

#endif // MUTEX_PRIMITIVES_H
