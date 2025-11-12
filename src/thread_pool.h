#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

// Forward declarations
typedef struct ThreadPool ThreadPool;
typedef struct ThreadPoolTask ThreadPoolTask;

// Task function signature
// Takes user_data pointer, returns result pointer (can be NULL)
typedef void* (*ThreadPoolTaskFunc)(void* user_data);

// Completion callback signature  
// Called on main thread when task completes
typedef void (*ThreadPoolCompletionFunc)(void* user_data, void* result);

// Create thread pool with specified number of worker threads
// num_threads: Number of worker threads (0 = auto-detect CPU count)
// Returns: ThreadPool instance or NULL on failure
ThreadPool* thread_pool_create(size_t num_threads);

// Destroy thread pool and wait for all tasks to complete
void thread_pool_destroy(ThreadPool* pool);

// Submit task to thread pool
// pool: ThreadPool instance
// task_func: Function to execute on worker thread
// user_data: Pointer passed to task_func
// completion_func: Callback invoked on main thread when task completes (can be NULL)
// completion_data: Pointer passed to completion_func (can be NULL)
// Returns: true if task was queued, false if pool is shutting down
bool thread_pool_submit(
    ThreadPool* pool,
    ThreadPoolTaskFunc task_func,
    void* user_data,
    ThreadPoolCompletionFunc completion_func,
    void* completion_data
);

// Process completed tasks on main thread
// pool: ThreadPool instance
// max_tasks: Maximum number of completion callbacks to invoke (0 = all)
// Returns: Number of completion callbacks invoked
int thread_pool_process_completions(ThreadPool* pool, int max_tasks);

// Get number of pending tasks
size_t thread_pool_pending_count(ThreadPool* pool);

// Get number of worker threads
size_t thread_pool_thread_count(ThreadPool* pool);

#endif // THREAD_POOL_H
