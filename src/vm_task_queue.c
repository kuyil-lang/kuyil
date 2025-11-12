// vm_task_queue.c - Thread-safe task queue implementation
#define _POSIX_C_SOURCE 199309L
#include "vm_task_queue.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

TaskQueue* task_queue_create(size_t capacity) {
    TaskQueue* q = (TaskQueue*)malloc(sizeof(TaskQueue));
    if (!q) return NULL;
    
    q->tasks = (Task*)calloc(capacity, sizeof(Task));
    if (!q->tasks) {
        free(q);
        return NULL;
    }
    
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = false;
    
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    
    return q;
}

void task_queue_destroy(TaskQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    
    free(queue->tasks);
    free(queue);
}

bool task_queue_enqueue(TaskQueue* queue, Task task) {
    if (!queue) return false;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if full
    while (queue->count >= queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    
    // Add task
    queue->tasks[queue->tail] = task;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return true;
}

bool task_queue_enqueue_timeout(TaskQueue* queue, Task task, int timeout_ms) {
    if (!queue) return false;
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if full (with timeout)
    while (queue->count >= queue->capacity && !queue->shutdown) {
        int ret = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&queue->mutex);
            return false;
        }
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    
    // Add task
    queue->tasks[queue->tail] = task;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return true;
}

int task_queue_process(TaskQueue* queue, int max_tasks) {
    if (!queue) return 0;
    
    int processed = 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    while (processed < max_tasks && queue->count > 0) {
        // Get task
        Task task = queue->tasks[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        
        pthread_cond_signal(&queue->not_full);
        pthread_mutex_unlock(&queue->mutex);
        
        // Execute task outside the lock
        if (task.callback) {
            task.callback(task.data);
        }
        
        processed++;
        
        pthread_mutex_lock(&queue->mutex);
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    return processed;
}

bool task_queue_is_empty(TaskQueue* queue) {
    if (!queue) return true;
    
    pthread_mutex_lock(&queue->mutex);
    bool empty = (queue->count == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

size_t task_queue_size(TaskQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    size_t size = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

void task_queue_shutdown(TaskQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

// HTTP task context helpers
HttpTaskContext* http_task_context_create(int req_h, int res_h, const char* handler) {
    HttpTaskContext* ctx = (HttpTaskContext*)malloc(sizeof(HttpTaskContext));
    if (!ctx) return NULL;
    
    ctx->req_handle = req_h;
    ctx->res_handle = res_h;
    ctx->handler_name = handler ? strdup(handler) : NULL;
    ctx->completed = false;
    
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
    
    return ctx;
}

void http_task_context_destroy(HttpTaskContext* ctx) {
    if (!ctx) return;
    
    if (ctx->handler_name) free(ctx->handler_name);
    pthread_mutex_destroy(&ctx->mutex);
    pthread_cond_destroy(&ctx->cond);
    free(ctx);
}

void http_task_context_wait(HttpTaskContext* ctx, int timeout_ms) {
    if (!ctx) return;
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    while (!ctx->completed) {
        if (pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &ts) == ETIMEDOUT) {
            break;
        }
    }
    pthread_mutex_unlock(&ctx->mutex);
}

void http_task_context_signal(HttpTaskContext* ctx) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->mutex);
    ctx->completed = true;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
}
