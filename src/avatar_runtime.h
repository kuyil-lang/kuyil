#ifndef AVATAR_RUNTIME_H
#define AVATAR_RUNTIME_H

#include <stddef.h>
#include <stdbool.h>
#include "bytecode.h"

// Forward declarations
typedef struct AvatarRuntime AvatarRuntime;
typedef struct AvatarHandle AvatarHandle;

// Get main VM context from avatar handle
void* avatar_handle_get_main_vm(AvatarHandle* handle);

// Completion callback - invoked on main thread when avatar completes
// handle: Avatar handle
// result: Return value from avatar function
// user_data: User-provided data
typedef void (*AvatarCompletionCallback)(AvatarHandle* handle, Value result, void* user_data);

// Create avatar runtime with specified number of worker threads
// num_threads: Number of worker threads (0 = auto-detect)
// Returns: AvatarRuntime instance or NULL on failure
AvatarRuntime* avatar_runtime_create(size_t num_threads);

// Destroy avatar runtime and wait for all avatars to complete
void avatar_runtime_destroy(AvatarRuntime* runtime);

// Submit avatar for execution
// runtime: AvatarRuntime instance
// function: Compiled function to execute
// args: Array of function arguments
// arg_count: Number of arguments
// vm_context: VM context for execution
// callback: Completion callback (can be NULL)
// user_data: User data passed to callback
// Returns: AvatarHandle or NULL on failure
AvatarHandle* avatar_runtime_submit(
    AvatarRuntime* runtime,
    Function* function,
    Value* args,
    int arg_count,
    void* vm_context,
    AvatarCompletionCallback callback,
    void* user_data
);

// Process completed avatars on main thread
// runtime: AvatarRuntime instance
// max_avatars: Maximum number of completions to process (0 = all)
// Returns: Number of completion callbacks invoked
int avatar_runtime_process_completions(AvatarRuntime* runtime, int max_avatars);

// Cancel a running avatar (best-effort)
void avatar_runtime_cancel(AvatarHandle* handle);

// Wait for specific avatar to complete
// handle: Avatar handle
// timeout_ms: Timeout in milliseconds (0 = no timeout)
// result: Output parameter for avatar result (can be NULL)
// Returns: true if avatar completed, false on timeout
bool avatar_runtime_await(AvatarHandle* handle, int timeout_ms, Value* result);

// Get number of pending avatars
size_t avatar_runtime_pending_count(AvatarRuntime* runtime);

// Get number of worker threads
size_t avatar_runtime_thread_count(AvatarRuntime* runtime);

// Check if avatar has completed
bool avatar_runtime_is_complete(AvatarHandle* handle);

// Check if avatar had an error
bool avatar_runtime_has_error(AvatarHandle* handle);

// Get avatar error message (returns NULL if no error)
const char* avatar_runtime_get_error(AvatarHandle* handle);

// Get avatar result (only valid if completed)
Value avatar_runtime_get_result(AvatarHandle* handle);

#endif // AVATAR_RUNTIME_H
