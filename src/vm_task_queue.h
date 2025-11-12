// vm_task_queue.h - Thread-safe task queue for VM operations
// Allows worker threads to safely enqueue tasks for main thread execution

#ifndef VM_TASK_QUEUE_H
#define VM_TASK_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

// Forward declare Value type
#ifndef VALUE_H
typedef struct Value Value;
#endif

// Task callback signature
typedef void (*TaskCallback)(void* data);

// HTTP request context for async handler execution
typedef struct {
    int req_handle;
    int res_handle;
    char* handler_name;
    bool completed;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} HttpTaskContext;

// Generic task
typedef struct {
    TaskCallback callback;
    void* data;
} Task;

// Thread-safe task queue
typedef struct {
    Task* tasks;
    size_t capacity;
    size_t head;    // Read position
    size_t tail;    // Write position
    size_t count;   // Number of items
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool shutdown;
} TaskQueue;

// Initialize/cleanup
TaskQueue* task_queue_create(size_t capacity);
void task_queue_destroy(TaskQueue* queue);

// Thread-safe operations (can be called from any thread)
bool task_queue_enqueue(TaskQueue* queue, Task task);
bool task_queue_enqueue_timeout(TaskQueue* queue, Task task, int timeout_ms);

// Main thread operations (process tasks on VM thread)
int task_queue_process(TaskQueue* queue, int max_tasks);
bool task_queue_is_empty(TaskQueue* queue);
size_t task_queue_size(TaskQueue* queue);

// Shutdown
void task_queue_shutdown(TaskQueue* queue);

// HTTP task helpers
HttpTaskContext* http_task_context_create(int req_h, int res_h, const char* handler);
void http_task_context_destroy(HttpTaskContext* ctx);
void http_task_context_wait(HttpTaskContext* ctx, int timeout_ms);
void http_task_context_signal(HttpTaskContext* ctx);

#endif // VM_TASK_QUEUE_H
