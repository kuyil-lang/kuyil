// thread_pool.c - Thread pool for async task execution
#include "thread_pool.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

// Task in the work queue
struct ThreadPoolTask {
    ThreadPoolTaskFunc task_func;
    void* user_data;
    ThreadPoolCompletionFunc completion_func;
    void* completion_data;
    void* result;  // Set by worker thread
    struct ThreadPoolTask* next;
};

// Completion queue (completed tasks waiting for main thread processing)
typedef struct CompletionQueueNode {
    ThreadPoolTask* task;
    struct CompletionQueueNode* next;
} CompletionQueueNode;

// Thread pool structure
struct ThreadPool {
    pthread_t* threads;
    size_t num_threads;
    
    // Work queue
    ThreadPoolTask* work_queue_head;
    ThreadPoolTask* work_queue_tail;
    size_t pending_count;
    
    // Completion queue (thread-safe)
    CompletionQueueNode* completion_queue_head;
    CompletionQueueNode* completion_queue_tail;
    pthread_mutex_t completion_mutex;
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t work_done;
    
    bool shutdown;
};

// Worker thread function
static void* worker_thread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        // Wait for work or shutdown
        while (!pool->shutdown && pool->work_queue_head == NULL) {
            pthread_cond_wait(&pool->work_available, &pool->mutex);
        }
        
        if (pool->shutdown && pool->work_queue_head == NULL) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        // Dequeue task
        ThreadPoolTask* task = pool->work_queue_head;
        if (task) {
            pool->work_queue_head = task->next;
            if (pool->work_queue_head == NULL) {
                pool->work_queue_tail = NULL;
            }
            pool->pending_count--;
        }
        
        pthread_mutex_unlock(&pool->mutex);
        
        if (task) {
            // Execute task
            task->result = task->task_func(task->user_data);
            
            // Add to completion queue if there's a completion callback
            if (task->completion_func) {
                pthread_mutex_lock(&pool->completion_mutex);
                
                CompletionQueueNode* node = malloc(sizeof(CompletionQueueNode));
                node->task = task;
                node->next = NULL;
                
                if (pool->completion_queue_tail) {
                    pool->completion_queue_tail->next = node;
                } else {
                    pool->completion_queue_head = node;
                }
                pool->completion_queue_tail = node;
                
                pthread_mutex_unlock(&pool->completion_mutex);
            } else {
                // No completion callback, just free the task
                free(task);
            }
            
            pthread_cond_signal(&pool->work_done);
        }
    }
    
    return NULL;
}

ThreadPool* thread_pool_create(size_t num_threads) {
    if (num_threads == 0) {
        // Auto-detect CPU count
        long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        num_threads = (nprocs > 0) ? (size_t)nprocs : 4;
    }
    
    ThreadPool* pool = calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;
    
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }
    
    pool->num_threads = num_threads;
    pool->work_queue_head = NULL;
    pool->work_queue_tail = NULL;
    pool->pending_count = 0;
    pool->completion_queue_head = NULL;
    pool->completion_queue_tail = NULL;
    pool->shutdown = false;
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_mutex_init(&pool->completion_mutex, NULL);
    pthread_cond_init(&pool->work_available, NULL);
    pthread_cond_init(&pool->work_done, NULL);
    
    // Create worker threads
    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            // Cleanup on failure
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->work_available);
            for (size_t j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->mutex);
            pthread_mutex_destroy(&pool->completion_mutex);
            pthread_cond_destroy(&pool->work_available);
            pthread_cond_destroy(&pool->work_done);
            free(pool);
            return NULL;
        }
    }
    
    return pool;
}

void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;
    
    // Signal shutdown
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);
    
    // Wait for all threads to finish
    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Free remaining work queue
    while (pool->work_queue_head) {
        ThreadPoolTask* task = pool->work_queue_head;
        pool->work_queue_head = task->next;
        free(task);
    }
    
    // Free completion queue
    while (pool->completion_queue_head) {
        CompletionQueueNode* node = pool->completion_queue_head;
        pool->completion_queue_head = node->next;
        free(node->task);
        free(node);
    }
    
    free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_mutex_destroy(&pool->completion_mutex);
    pthread_cond_destroy(&pool->work_available);
    pthread_cond_destroy(&pool->work_done);
    free(pool);
}

bool thread_pool_submit(
    ThreadPool* pool,
    ThreadPoolTaskFunc task_func,
    void* user_data,
    ThreadPoolCompletionFunc completion_func,
    void* completion_data
) {
    if (!pool || !task_func) return false;
    
    ThreadPoolTask* task = malloc(sizeof(ThreadPoolTask));
    if (!task) return false;
    
    task->task_func = task_func;
    task->user_data = user_data;
    task->completion_func = completion_func;
    task->completion_data = completion_data;
    task->result = NULL;
    task->next = NULL;
    
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        free(task);
        return false;
    }
    
    // Add to work queue
    if (pool->work_queue_tail) {
        pool->work_queue_tail->next = task;
    } else {
        pool->work_queue_head = task;
    }
    pool->work_queue_tail = task;
    pool->pending_count++;
    
    pthread_cond_signal(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);
    
    return true;
}

int thread_pool_process_completions(ThreadPool* pool, int max_tasks) {
    if (!pool) return 0;
    
    int processed = 0;
    
    while (max_tasks == 0 || processed < max_tasks) {
        pthread_mutex_lock(&pool->completion_mutex);
        
        CompletionQueueNode* node = pool->completion_queue_head;
        if (!node) {
            pthread_mutex_unlock(&pool->completion_mutex);
            break;
        }
        
        // Dequeue
        pool->completion_queue_head = node->next;
        if (pool->completion_queue_head == NULL) {
            pool->completion_queue_tail = NULL;
        }
        
        pthread_mutex_unlock(&pool->completion_mutex);
        
        // Invoke completion callback on main thread
        ThreadPoolTask* task = node->task;
        if (task->completion_func) {
            task->completion_func(task->completion_data, task->result);
        }
        
        free(task);
        free(node);
        processed++;
    }
    
    return processed;
}

size_t thread_pool_pending_count(ThreadPool* pool) {
    if (!pool) return 0;
    
    pthread_mutex_lock(&pool->mutex);
    size_t count = pool->pending_count;
    pthread_mutex_unlock(&pool->mutex);
    
    return count;
}

size_t thread_pool_thread_count(ThreadPool* pool) {
    return pool ? pool->num_threads : 0;
}
