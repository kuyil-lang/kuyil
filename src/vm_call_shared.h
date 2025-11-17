#ifndef kuyil_vm_call_shared_h
#define kuyil_vm_call_shared_h

#include "ast.h"
#include "vm.h"

// Shared call handling result
typedef enum {
    CALL_RESULT_OK,
    CALL_RESULT_ERROR,
    CALL_RESULT_NOT_HANDLED  // Means caller should handle this
} CallResult;

// Shared context for making calls (works for both main VM and avatar)
typedef struct {
    // Stack operations
    Value* (*peek)(void* context, int distance);
    Value (*pop)(void* context);
    void (*push)(void* context, Value value);
    Value* (*get_args)(void* context, int arg_count);  // Returns pointer to args on stack
    void (*pop_n)(void* context, int n);  // Pop n values
    
    // Error handling
    void (*report_error)(void* context, const char* message);
    
    // Frame operations (for user-defined functions)
    bool (*setup_frame)(void* context, Function* function, int arg_count);
    
    // Context pointer (VM* for main, avatar VM for avatar)
    void* context;
    
    // Globals access (may be NULL for avatar if not needed)
    bool (*get_global)(void* context, const char* name, Value* out);
    
    // Test mode flag
    bool test_mode;
} CallContext;

// Shared OP_CALL handler for VALUE_STRING callees (library functions, builtins)
// Returns CALL_RESULT_OK if handled successfully
// Returns CALL_RESULT_ERROR if error occurred
// Returns CALL_RESULT_NOT_HANDLED if this should fall through to other handlers
CallResult vm_call_string_shared(CallContext* ctx, const char* callee_str, int arg_count);

#endif
