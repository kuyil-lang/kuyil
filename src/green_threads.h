#ifndef KUYIL_GREEN_THREADS_H
#define KUYIL_GREEN_THREADS_H

#include "vm.h"
#include <pthread.h>

// Opaque handle for background tasks
typedef struct BGTask {
    pthread_t thread;
    Value result;
    int completed;
    int id;
    // Simple storage of function/args - store as copied Values
    Value* args;
    int arg_count;
    Function* function;
} BGTask;

// API
BGTask* bgtask_create(Function* function, int arg_count, Value* args);
void bgtask_start(BGTask* task);
void bgtask_join(BGTask* task);
void bgtask_destroy(BGTask* task);

// Observable pattern structures (RxJava-like)
typedef struct Observer Observer;
typedef struct Observable Observable;
typedef struct Subscription Subscription;

// Observer callbacks
typedef void (*OnNextFunc)(Observer* observer, Value value);
typedef void (*OnErrorFunc)(Observer* observer, const char* error);
typedef void (*OnCompleteFunc)(Observer* observer);

typedef struct Observer {
    int id;
    OnNextFunc on_next;
    OnErrorFunc on_error;
    OnCompleteFunc on_complete;
    void* user_data;
} Observer;

// Observable operators
typedef enum {
    OP_NONE,
    OP_MAP,
    OP_FILTER,
    OP_TAKE,
    OP_SKIP
} OperatorType;

typedef struct Operator {
    OperatorType type;
    char* transform_func; // Function name for map/filter
    int count; // For take/skip
    struct Operator* next;
} Operator;

typedef struct Observable {
    int id;
    pthread_t producer_thread;
    Observer** observers;
    int observer_count;
    int observer_capacity;
    Operator* operators;
    Value* values;
    int value_count;
    int value_capacity;
    int completed;
    int error;
    char* error_message;
    pthread_mutex_t mutex;
} Observable;

typedef struct Subscription {
    int id;
    Observable* observable;
    Observer* observer;
    int active;
} Subscription;

// Observable API
Observable* observable_create(void);
void observable_destroy(Observable* obs);
Subscription* observable_subscribe(Observable* obs, Observer* observer);
void observable_emit(Observable* obs, Value value);
void observable_complete(Observable* obs);
void observable_error(Observable* obs, const char* error);

// Observable operators
Observable* observable_map(Observable* source, const char* transform_func);
Observable* observable_filter(Observable* source, const char* predicate_func);
Observable* observable_take(Observable* source, int count);
Observable* observable_skip(Observable* source, int count);

// Subscription management
void subscription_unsubscribe(Subscription* sub);

// VM native functions - Background tasks
Value kuyil_start_bg(int arg_count, Value* args);
Value kuyil_wait_bg(int arg_count, Value* args);
Value kuyil_wait_two_bg(int arg_count, Value* args);

// VM native functions - Observables
Value kuyil_observable_create(int arg_count, Value* args);
Value kuyil_observable_emit(int arg_count, Value* args);
Value kuyil_observable_subscribe(int arg_count, Value* args);
Value kuyil_observable_map(int arg_count, Value* args);
Value kuyil_observable_filter(int arg_count, Value* args);
Value kuyil_observable_complete(int arg_count, Value* args);

// ============================================================================
// MESSAGE PASSING SYSTEM - Enable real async execution
// ============================================================================

typedef struct VMMessage {
    int id;
    char* function_name;
    Value* args;
    int arg_count;
    Value result;
    int completed;
    int error;
    char* error_message;
    struct VMMessage* next;
} VMMessage;

typedef struct MessageQueue {
    VMMessage* head;
    VMMessage* tail;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int count;
    int message_counter;
} MessageQueue;

// Message queue API
MessageQueue* message_queue_create(void);
void message_queue_destroy(MessageQueue* queue);
int message_queue_enqueue(MessageQueue* queue, const char* func_name, Value* args, int arg_count);
VMMessage* message_queue_dequeue(MessageQueue* queue);
VMMessage* message_queue_get_completed(MessageQueue* queue, int message_id);
void message_queue_complete(MessageQueue* queue, int message_id, Value result);
void message_queue_error(MessageQueue* queue, int message_id, const char* error);

// Global message queue for VM communication
extern MessageQueue* g_vm_message_queue;

// VM integration functions
void vm_init_message_queue(void);
void vm_cleanup_message_queue(void);
int vm_process_background_jobs(VM* vm); // Returns number of jobs processed
Value vm_execute_queued_function(VM* vm, const char* func_name, Value* args, int arg_count);

// Enhanced background task with message passing
typedef struct EnhancedBGTask {
    BGTask base_task;
    int message_id;
    Observable* connected_observable;
} EnhancedBGTask;

// Enhanced VM native functions with real execution
Value kuyil_start_bg_enhanced(int arg_count, Value* args);
Value kuyil_wait_bg_enhanced(int arg_count, Value* args);
Value kuyil_process_bg_jobs(int arg_count, Value* args);

#endif // KUYIL_GREEN_THREADS_H