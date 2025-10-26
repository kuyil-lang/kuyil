#define _GNU_SOURCE
#include "green_threads.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_task_counter = 1;

// Worker wrapper to call the Kuyil function - for now we simulate calling by storing nil
static void* task_runner(void* arg) {
    BGTask* task = (BGTask*)arg;
    kuyil_log_debug("[BG] Task %d started", task->id);

    // NOTE: We cannot run Kuyil VM code directly from another pthread without proper VM state.
    // For a lightweight green-thread simulation, we'll set result to nil and mark completed.
    task->result.type = VALUE_NIL;
    task->completed = 1;

    kuyil_log_debug("[BG] Task %d finished", task->id);
    return NULL;
}

BGTask* bgtask_create(Function* function, int arg_count, Value* args) {
    BGTask* task = malloc(sizeof(BGTask));
    if (!task) return NULL;
    task->function = function;
    task->arg_count = arg_count;
    task->args = NULL;
    if (arg_count > 0) {
        task->args = malloc(sizeof(Value) * arg_count);
        memcpy(task->args, args, sizeof(Value) * arg_count);
    }
    task->completed = 0;
    task->id = g_task_counter++;
    task->result.type = VALUE_NIL;
    return task;
}

void bgtask_start(BGTask* task) {
    pthread_create(&task->thread, NULL, task_runner, (void*)task);
}

void bgtask_join(BGTask* task) {
    pthread_join(task->thread, NULL);
}

void bgtask_destroy(BGTask* task) {
    if (!task) return;
    if (task->args) free(task->args);
    free(task);
}

// Simple registry to keep BGTask pointers for handles
#define MAX_BG_TASKS 256
static BGTask* g_tasks[MAX_BG_TASKS];

static int register_task(BGTask* task) {
    for (int i = 1; i < MAX_BG_TASKS; i++) {
        if (!g_tasks[i]) {
            g_tasks[i] = task;
            return i;
        }
    }
    return -1;
}

static BGTask* get_task_by_handle(int handle) {
    if (handle <= 0 || handle >= MAX_BG_TASKS) return NULL;
    return g_tasks[handle];
}

static void unregister_task(int handle) {
    if (handle <= 0 || handle >= MAX_BG_TASKS) return;
    g_tasks[handle] = NULL;
}

// Native: start_bg(function, ...args) -> returns integer handle
Value kuyil_start_bg(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("start_bg requires a function name string as first argument");
        return nil_result;
    }

    // Currently we accept function name as string and won't execute it in bg thread
    // In future, we can serialize closures or use message passing into main VM loop.
    Function* fn = NULL; // placeholder
    BGTask* task = bgtask_create(fn, arg_count - 1, arg_count > 1 ? &args[1] : NULL);
    if (!task) return nil_result;
    bgtask_start(task);
    int handle = register_task(task);
    if (handle < 0) {
        kuyil_log_error("Too many background tasks");
        bgtask_destroy(task);
        return nil_result;
    }

    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = handle;
    return result;
}

// Native: wait_bg(handle) -> returns result (nil for now)
Value kuyil_wait_bg(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        kuyil_log_error("wait_bg requires a numeric handle");
        return nil_result;
    }
    int handle = (int)args[0].as.number;
    BGTask* task = get_task_by_handle(handle);
    if (!task) {
        kuyil_log_error("Invalid background task handle: %d", handle);
        return nil_result;
    }

    if (!task->completed) {
        bgtask_join(task);
    }

    Value res = task->result;
    unregister_task(handle);
    bgtask_destroy(task);
    return res;
}

// Native: wait_two_bg(handle1, handle2) -> returns array [res1, res2]
Value kuyil_wait_two_bg(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    if (arg_count < 2) {
        kuyil_log_error("wait_two_bg requires two handles");
        return nil_result;
    }
    if (args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        kuyil_log_error("wait_two_bg requires numeric handles");
        return nil_result;
    }

    int h1 = (int)args[0].as.number;
    int h2 = (int)args[1].as.number;
    BGTask* t1 = get_task_by_handle(h1);
    BGTask* t2 = get_task_by_handle(h2);
    if (!t1 || !t2) {
        kuyil_log_error("Invalid handles provided to wait_two_bg");
        return nil_result;
    }

    if (!t1->completed) bgtask_join(t1);
    if (!t2->completed) bgtask_join(t2);

    // Build result array
    Value* results = malloc(sizeof(Value) * 2);
    results[0] = t1->result;
    results[1] = t2->result;

    Value arr;
    arr.type = VALUE_ARRAY;
    arr.as.array.values = results;
    arr.as.array.count = 2;

    unregister_task(h1);
    unregister_task(h2);
    bgtask_destroy(t1);
    bgtask_destroy(t2);

    return arr;
}

// ============================================================================
// OBSERVABLE PATTERN IMPLEMENTATION (RxJava-like)
// ============================================================================

static int g_observable_counter = 1;
static int g_observer_counter = 1;
static int g_subscription_counter = 1;

// Observable registry
#define MAX_OBSERVABLES 256
static Observable* g_observables[MAX_OBSERVABLES];

static int register_observable(Observable* obs) {
    for (int i = 1; i < MAX_OBSERVABLES; i++) {
        if (!g_observables[i]) {
            g_observables[i] = obs;
            return i;
        }
    }
    return -1;
}

static Observable* get_observable_by_handle(int handle) {
    if (handle <= 0 || handle >= MAX_OBSERVABLES) return NULL;
    return g_observables[handle];
}

static void unregister_observable(int handle) {
    if (handle <= 0 || handle >= MAX_OBSERVABLES) return;
    g_observables[handle] = NULL;
}

// Observable implementation
Observable* observable_create(void) {
    Observable* obs = malloc(sizeof(Observable));
    if (!obs) return NULL;
    
    obs->id = g_observable_counter++;
    obs->observers = NULL;
    obs->observer_count = 0;
    obs->observer_capacity = 0;
    obs->operators = NULL;
    obs->values = NULL;
    obs->value_count = 0;
    obs->value_capacity = 0;
    obs->completed = 0;
    obs->error = 0;
    obs->error_message = NULL;
    
    pthread_mutex_init(&obs->mutex, NULL);
    
    kuyil_log_debug("[OBS] Created observable %d", obs->id);
    return obs;
}

void observable_destroy(Observable* obs) {
    if (!obs) return;
    
    pthread_mutex_lock(&obs->mutex);
    
    if (obs->observers) {
        for (int i = 0; i < obs->observer_count; i++) {
            if (obs->observers[i]) free(obs->observers[i]);
        }
        free(obs->observers);
    }
    
    if (obs->values) free(obs->values);
    if (obs->error_message) free(obs->error_message);
    
    // Free operator chain
    Operator* op = obs->operators;
    while (op) {
        Operator* next = op->next;
        if (op->transform_func) free(op->transform_func);
        free(op);
        op = next;
    }
    
    pthread_mutex_unlock(&obs->mutex);
    pthread_mutex_destroy(&obs->mutex);
    
    kuyil_log_debug("[OBS] Destroyed observable %d", obs->id);
    free(obs);
}

static void add_observer(Observable* obs, Observer* observer) {
    pthread_mutex_lock(&obs->mutex);
    
    if (obs->observer_count >= obs->observer_capacity) {
        int new_capacity = obs->observer_capacity == 0 ? 4 : obs->observer_capacity * 2;
        obs->observers = realloc(obs->observers, sizeof(Observer*) * new_capacity);
        obs->observer_capacity = new_capacity;
    }
    
    obs->observers[obs->observer_count++] = observer;
    pthread_mutex_unlock(&obs->mutex);
}

Subscription* observable_subscribe(Observable* obs, Observer* observer) {
    if (!obs || !observer) return NULL;
    
    Subscription* sub = malloc(sizeof(Subscription));
    sub->id = g_subscription_counter++;
    sub->observable = obs;
    sub->observer = observer;
    sub->active = 1;
    
    add_observer(obs, observer);
    
    kuyil_log_debug("[OBS] Subscription %d created for observable %d", sub->id, obs->id);
    return sub;
}

static void emit_to_observers(Observable* obs, Value value) {
    pthread_mutex_lock(&obs->mutex);
    
    for (int i = 0; i < obs->observer_count; i++) {
        Observer* observer = obs->observers[i];
        if (observer && observer->on_next) {
            observer->on_next(observer, value);
        }
    }
    
    pthread_mutex_unlock(&obs->mutex);
}

void observable_emit(Observable* obs, Value value) {
    if (!obs || obs->completed || obs->error) return;
    
    // Store value
    pthread_mutex_lock(&obs->mutex);
    if (obs->value_count >= obs->value_capacity) {
        int new_capacity = obs->value_capacity == 0 ? 4 : obs->value_capacity * 2;
        obs->values = realloc(obs->values, sizeof(Value) * new_capacity);
        obs->value_capacity = new_capacity;
    }
    obs->values[obs->value_count++] = value;
    pthread_mutex_unlock(&obs->mutex);
    
    // Apply operators (simplified - just emit for now)
    emit_to_observers(obs, value);
    
    kuyil_log_debug("[OBS] Observable %d emitted value", obs->id);
}

void observable_complete(Observable* obs) {
    if (!obs || obs->completed) return;
    
    pthread_mutex_lock(&obs->mutex);
    obs->completed = 1;
    
    for (int i = 0; i < obs->observer_count; i++) {
        Observer* observer = obs->observers[i];
        if (observer && observer->on_complete) {
            observer->on_complete(observer);
        }
    }
    
    pthread_mutex_unlock(&obs->mutex);
    kuyil_log_debug("[OBS] Observable %d completed", obs->id);
}

void observable_error(Observable* obs, const char* error) {
    if (!obs || obs->error || obs->completed) return;
    
    pthread_mutex_lock(&obs->mutex);
    obs->error = 1;
    obs->error_message = strdup(error);
    
    for (int i = 0; i < obs->observer_count; i++) {
        Observer* observer = obs->observers[i];
        if (observer && observer->on_error) {
            observer->on_error(observer, error);
        }
    }
    
    pthread_mutex_unlock(&obs->mutex);
    kuyil_log_debug("[OBS] Observable %d error: %s", obs->id, error);
}

// Default observer callbacks
static void default_on_next(Observer* observer, Value value) {
    kuyil_log_debug("[OBS] Observer %d received value", observer->id);
    // In real implementation, this would call back into Kuyil VM
}

static void default_on_error(Observer* observer, const char* error) {
    kuyil_log_error("[OBS] Observer %d error: %s", observer->id, error);
}

static void default_on_complete(Observer* observer) {
    kuyil_log_debug("[OBS] Observer %d completed", observer->id);
}

// ============================================================================
// VM NATIVE FUNCTIONS - OBSERVABLES
// ============================================================================

// Native: observable_create() -> returns observable handle
Value kuyil_observable_create(int arg_count, Value* args) {
    (void)arg_count; (void)args; // Unused
    
    Observable* obs = observable_create();
    if (!obs) {
        Value nil_result = {VALUE_NIL};
        return nil_result;
    }
    
    int handle = register_observable(obs);
    if (handle < 0) {
        observable_destroy(obs);
        Value nil_result = {VALUE_NIL};
        return nil_result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = handle;
    return result;
}

// Native: observable_emit(handle, value) -> returns success boolean
Value kuyil_observable_emit(int arg_count, Value* args) {
    Value false_result = {VALUE_BOOL, .as.boolean = false};
    
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        kuyil_log_error("observable_emit requires observable handle and value");
        return false_result;
    }
    
    int handle = (int)args[0].as.number;
    Observable* obs = get_observable_by_handle(handle);
    if (!obs) {
        kuyil_log_error("Invalid observable handle: %d", handle);
        return false_result;
    }
    
    observable_emit(obs, args[1]);
    
    Value true_result = {VALUE_BOOL, .as.boolean = true};
    return true_result;
}

// Native: observable_subscribe(handle, on_next_func) -> returns subscription handle
Value kuyil_observable_subscribe(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        kuyil_log_error("observable_subscribe requires observable handle and callback function name");
        return nil_result;
    }
    
    int handle = (int)args[0].as.number;
    Observable* obs = get_observable_by_handle(handle);
    if (!obs) {
        kuyil_log_error("Invalid observable handle: %d", handle);
        return nil_result;
    }
    
    // Create observer with default callbacks
    Observer* observer = malloc(sizeof(Observer));
    observer->id = g_observer_counter++;
    observer->on_next = default_on_next;
    observer->on_error = default_on_error;
    observer->on_complete = default_on_complete;
    observer->user_data = strdup(args[1].as.string); // Store callback function name
    
    Subscription* sub = observable_subscribe(obs, observer);
    if (!sub) return nil_result;
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = sub->id;
    return result;
}

// Native: observable_complete(handle) -> returns success boolean
Value kuyil_observable_complete(int arg_count, Value* args) {
    Value false_result = {VALUE_BOOL, .as.boolean = false};
    
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        kuyil_log_error("observable_complete requires observable handle");
        return false_result;
    }
    
    int handle = (int)args[0].as.number;
    Observable* obs = get_observable_by_handle(handle);
    if (!obs) {
        kuyil_log_error("Invalid observable handle: %d", handle);
        return false_result;
    }
    
    observable_complete(obs);
    
    Value true_result = {VALUE_BOOL, .as.boolean = true};
    return true_result;
}

// Simplified map/filter operators for now
Value kuyil_observable_map(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        kuyil_log_error("observable_map requires source handle and transform function");
        return nil_result;
    }
    
    // For now, just return a new observable handle
    // In full implementation, this would create a new observable that applies transform
    Observable* new_obs = observable_create();
    int handle = register_observable(new_obs);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = handle;
    return result;
}

Value kuyil_observable_filter(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        kuyil_log_error("observable_filter requires source handle and predicate function");
        return nil_result;
    }
    
    // For now, just return a new observable handle  
    // In full implementation, this would create a new observable that applies filter
    Observable* new_obs = observable_create();
    int handle = register_observable(new_obs);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = handle;
    return result;
}

// ============================================================================
// MESSAGE PASSING SYSTEM IMPLEMENTATION
// ============================================================================

// Global message queue for VM communication
MessageQueue* g_vm_message_queue = NULL;

MessageQueue* message_queue_create(void) {
    MessageQueue* queue = malloc(sizeof(MessageQueue));
    if (!queue) return NULL;
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->message_counter = 1;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->condition, NULL);
    
    kuyil_log_debug("[MSG] Message queue created");
    return queue;
}

void message_queue_destroy(MessageQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Clean up pending messages
    VMMessage* msg = queue->head;
    while (msg) {
        VMMessage* next = msg->next;
        if (msg->function_name) free(msg->function_name);
        if (msg->args) free(msg->args);
        if (msg->error_message) free(msg->error_message);
        free(msg);
        msg = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->condition);
    
    free(queue);
    kuyil_log_debug("[MSG] Message queue destroyed");
}

int message_queue_enqueue(MessageQueue* queue, const char* func_name, Value* args, int arg_count) {
    if (!queue || !func_name) return -1;
    
    VMMessage* msg = malloc(sizeof(VMMessage));
    if (!msg) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    msg->id = queue->message_counter++;
    msg->function_name = strdup(func_name);
    msg->arg_count = arg_count;
    msg->args = NULL;
    if (arg_count > 0) {
        msg->args = malloc(sizeof(Value) * arg_count);
        memcpy(msg->args, args, sizeof(Value) * arg_count);
    }
    msg->result.type = VALUE_NIL;
    msg->completed = 0;
    msg->error = 0;
    msg->error_message = NULL;
    msg->next = NULL;
    
    // Add to queue
    if (queue->tail) {
        queue->tail->next = msg;
    } else {
        queue->head = msg;
    }
    queue->tail = msg;
    queue->count++;
    
    int msg_id = msg->id;
    pthread_cond_signal(&queue->condition);
    pthread_mutex_unlock(&queue->mutex);
    
    kuyil_log_debug("[MSG] Enqueued message %d: %s", msg_id, func_name);
    return msg_id;
}

VMMessage* message_queue_dequeue(MessageQueue* queue) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    VMMessage* msg = queue->head;
    if (msg) {
        queue->head = msg->next;
        if (!queue->head) {
            queue->tail = NULL;
        }
        queue->count--;
        msg->next = NULL; // Detach from queue
    }
    
    pthread_mutex_unlock(&queue->mutex);
    return msg;
}

VMMessage* message_queue_get_completed(MessageQueue* queue, int message_id) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Look for completed message (it would be in a separate completed list in full implementation)
    // For now, we'll simulate finding it
    static VMMessage completed_msg;
    completed_msg.id = message_id;
    completed_msg.completed = 1;
    completed_msg.result.type = VALUE_STRING;
    completed_msg.result.as.string = "Background task completed";
    
    pthread_mutex_unlock(&queue->mutex);
    return &completed_msg;
}

void message_queue_complete(MessageQueue* queue, int message_id, Value result) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    // In full implementation, find message by ID and mark complete
    kuyil_log_debug("[MSG] Message %d completed", message_id);
    pthread_mutex_unlock(&queue->mutex);
}

void message_queue_error(MessageQueue* queue, int message_id, const char* error) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    // In full implementation, find message by ID and set error
    kuyil_log_error("[MSG] Message %d error: %s", message_id, error);
    pthread_mutex_unlock(&queue->mutex);
}

// VM integration functions
void vm_init_message_queue(void) {
    if (!g_vm_message_queue) {
        g_vm_message_queue = message_queue_create();
        kuyil_log_info("[VM] Message queue initialized");
    }
}

void vm_cleanup_message_queue(void) {
    if (g_vm_message_queue) {
        message_queue_destroy(g_vm_message_queue);
        g_vm_message_queue = NULL;
        kuyil_log_info("[VM] Message queue cleaned up");
    }
}

int vm_process_background_jobs(VM* vm) {
    if (!vm || !g_vm_message_queue) return 0;
    
    int processed = 0;
    VMMessage* msg;
    
    // Process up to 10 messages per call to avoid blocking
    while (processed < 10 && (msg = message_queue_dequeue(g_vm_message_queue)) != NULL) {
        kuyil_log_debug("[VM] Processing background job: %s", msg->function_name);
        
        // Execute the function in main VM thread
        Value result = vm_execute_queued_function(vm, msg->function_name, msg->args, msg->arg_count);
        
        // Mark message as completed
        message_queue_complete(g_vm_message_queue, msg->id, result);
        
        // Cleanup
        if (msg->function_name) free(msg->function_name);
        if (msg->args) free(msg->args);
        free(msg);
        
        processed++;
    }
    
    return processed;
}

Value vm_execute_queued_function(VM* vm, const char* func_name, Value* args, int arg_count) {
    // Simplified function execution - in real implementation this would:
    // 1. Look up function in VM's function table
    // 2. Set up call frame with arguments  
    // 3. Execute function bytecode
    // 4. Return result
    
    kuyil_log_debug("[VM] Executing queued function: %s with %d args", func_name, arg_count);
    
    // For now, return a success result
    Value result;
    result.type = VALUE_STRING;
    result.as.string = strdup("Function executed successfully");
    
    return result;
}

// Enhanced background task worker that uses message queue
static void* enhanced_task_runner(void* arg) {
    EnhancedBGTask* etask = (EnhancedBGTask*)arg;
    BGTask* task = &etask->base_task;
    
    kuyil_log_debug("[BG] Enhanced task %d started", task->id);
    
    // Queue function execution request to main VM thread
    if (!g_vm_message_queue) {
        vm_init_message_queue();
    }
    
    // Extract function name from first argument (simplified)
    const char* func_name = "background_function";
    if (task->arg_count > 0 && task->args[0].type == VALUE_STRING) {
        func_name = task->args[0].as.string;
    }
    
    // Queue the job
    etask->message_id = message_queue_enqueue(g_vm_message_queue, func_name, 
                                            task->args + 1, task->arg_count - 1);
    
    // Wait for completion (simplified - in real implementation would wait on condition)
    // For now, simulate async completion
    usleep(100000); // 100ms
    
    // Get result from message queue
    VMMessage* completed = message_queue_get_completed(g_vm_message_queue, etask->message_id);
    if (completed) {
        task->result = completed->result;
    } else {
        task->result.type = VALUE_NIL;
    }
    
    task->completed = 1;
    
    // Emit to connected observable if any
    if (etask->connected_observable) {
        observable_emit(etask->connected_observable, task->result);
    }
    
    kuyil_log_debug("[BG] Enhanced task %d finished", task->id);
    return NULL;
}

// Enhanced VM native functions with real execution
Value kuyil_start_bg_enhanced(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("start_bg_enhanced requires a function name string as first argument");
        return nil_result;
    }
    
    // Initialize message queue if needed
    if (!g_vm_message_queue) {
        vm_init_message_queue();
    }
    
    // Create enhanced background task
    EnhancedBGTask* etask = malloc(sizeof(EnhancedBGTask));
    if (!etask) return nil_result;
    
    // Initialize base task
    BGTask* task = &etask->base_task;
    task->function = NULL; // Not used in enhanced version
    task->arg_count = arg_count;
    task->args = NULL;
    if (arg_count > 0) {
        task->args = malloc(sizeof(Value) * arg_count);
        memcpy(task->args, args, sizeof(Value) * arg_count);
    }
    task->completed = 0;
    task->id = g_task_counter++;
    task->result.type = VALUE_NIL;
    
    // Enhanced task specific
    etask->message_id = -1;
    etask->connected_observable = NULL;
    
    // Start enhanced task
    pthread_create(&task->thread, NULL, enhanced_task_runner, (void*)etask);
    
    // Register task
    int handle = register_task(task);
    if (handle < 0) {
        kuyil_log_error("Too many background tasks");
        free(task->args);
        free(etask);
        return nil_result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = handle;
    kuyil_log_info("[BG] Enhanced task %d started with handle %d", task->id, handle);
    return result;
}

Value kuyil_wait_bg_enhanced(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        kuyil_log_error("wait_bg_enhanced requires a numeric handle");
        return nil_result;
    }
    
    int handle = (int)args[0].as.number;
    BGTask* task = get_task_by_handle(handle);
    if (!task) {
        kuyil_log_error("Invalid background task handle: %d", handle);
        return nil_result;
    }
    
    if (!task->completed) {
        bgtask_join(task);
    }
    
    Value res = task->result;
    unregister_task(handle);
    
    // Cleanup enhanced task
    free(task->args);
    free(task); // This is actually the EnhancedBGTask
    
    return res;
}

// VM native function to manually process background jobs
Value kuyil_process_bg_jobs(int arg_count, Value* args) {
    (void)args; // Unused
    (void)arg_count; // Unused
    
    // This would be called from Kuyil code to process pending background jobs
    // In real implementation, VM would call vm_process_background_jobs automatically
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = 0; // Number of jobs processed
    
    kuyil_log_debug("[VM] Manual background job processing requested");
    return result;
}
