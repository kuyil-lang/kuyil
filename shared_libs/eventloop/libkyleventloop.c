// Event Loop Library - Process async tasks from main thread
#include "../kuyil_types.h"
#include <stdio.h>
#include <stdbool.h>

// Forward declarations from vm.c
extern int vm_process_pending_tasks(int max_tasks);
extern bool vm_task_queue_empty(void);
extern size_t vm_task_queue_size(void);
extern int vm_process_avatar_completions(int max_avatars);
extern size_t vm_avatar_pending_count(void);

// Export interface signatures for auto-binding
__attribute__((visibility("default")))
const char* kyl_interface_signature_text =
    "eventloop processPendingTasks(maxTasks: int32) -> int32\n"
    "eventloop tasksEmpty() -> bool\n"
    "eventloop taskCount() -> int32\n"
    "eventloop processAvatarCompletions(maxAvatars: int32) -> int32\n"
    "eventloop avatarPendingCount() -> int32\n";

// Process pending tasks (call from event loop)
__attribute__((visibility("default")))
Value kyl_eventloop_processPendingTasks(int arg_count, Value* args) {
    Value result = {VALUE_NUMBER};
    result.as.number = 0;
    
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        return result;
    }
    
    int max_tasks = (int)args[0].as.number;
    if (max_tasks <= 0) max_tasks = 100;  // Default
    
    int processed = vm_process_pending_tasks(max_tasks);
    result.as.number = (double)processed;
    
    return result;
}

// Check if task queue is empty
__attribute__((visibility("default")))
Value kyl_eventloop_tasksEmpty(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Value result = {VALUE_BOOL};
    result.as.boolean = vm_task_queue_empty();
    
    return result;
}

// Get task count
__attribute__((visibility("default")))
Value kyl_eventloop_taskCount(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Value result = {VALUE_NUMBER};
    result.as.number = (double)vm_task_queue_size();
    
    return result;
}

// Process avatar completions (call from event loop)
__attribute__((visibility("default")))
Value kyl_eventloop_processAvatarCompletions(int arg_count, Value* args) {
    Value result = {VALUE_NUMBER};
    result.as.number = 0;
    
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        return result;
    }
    
    int max_avatars = (int)args[0].as.number;
    if (max_avatars <= 0) max_avatars = 100;  // Default
    
    int processed = vm_process_avatar_completions(max_avatars);
    result.as.number = (double)processed;
    
    return result;
}

// Get pending avatar count
__attribute__((visibility("default")))
Value kyl_eventloop_avatarPendingCount(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Value result = {VALUE_NUMBER};
    result.as.number = (double)vm_avatar_pending_count();
    
    return result;
}
