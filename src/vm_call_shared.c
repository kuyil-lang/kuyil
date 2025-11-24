#include "vm_call_shared.h"
#include "vm_library_integration.h"
#include "logging.h"
#include "avatar_runtime.h"
#include "vm.h"
#include <string.h>
#include <stdlib.h>

// Avatar VM context for shared call operations (from avatar_runtime.c)
typedef struct {
    VM* avatar_vm;
    struct AvatarHandle* handle;
} AvatarCallContext;

// Shared implementation of OP_CALL for VALUE_STRING callees
CallResult vm_call_string_shared(CallContext* ctx, const char* callee_str, int arg_count) {
    kuyil_log_debug("vm_call_string_shared: callee='%s' argc=%d", callee_str ? callee_str : "<null>", arg_count);
    
    // Get arguments from stack
    Value* args = ctx->get_args(ctx->context, arg_count);
    if (!args) {
        ctx->report_error(ctx->context, "Failed to get arguments");
        return CALL_RESULT_ERROR;
    }
    
    // Check if receiver (first arg) is an interface namespace marker string
    // This handles cases like: file.readText() where file is a namespace, not a variable
    if (arg_count >= 1 && !is_dynamic_function(callee_str)) {
        // Check if receiver is a STRING and is a registered interface name
        if (args[0].type == VALUE_STRING && is_interface_name(args[0].as.string)) {
            // Build dotted name: "interfaceName.methodName"
            char dotted[256];
            snprintf(dotted, sizeof(dotted), "%s.%s", args[0].as.string, callee_str);
            
            // Try to call the qualified function (drop the receiver)
            if (is_dynamic_function(dotted)) {
                Value result = call_dynamic_function(dotted, arg_count - 1, args + 1);
                ctx->pop_n(ctx->context, arg_count + 1); // pop receiver, user args, and callee string
                ctx->push(ctx->context, result);
                return CALL_RESULT_OK;
            } else {
                ctx->report_error(ctx->context, "Undefined function");
                return CALL_RESULT_ERROR;
            }
        }
    }
    
    // Special-case namespace method calls: receiver is a namespace object
    if (arg_count >= 1 && args[0].type == VALUE_OBJECT) {
        const char* ns_name = NULL;
        const char* if_name = NULL;
        
        for (int i = 0; i < args[0].as.object.count; i++) {
            if (strcmp(args[0].as.object.keys[i], "__namespace__") == 0 && 
                args[0].as.object.values[i].type == VALUE_STRING) {
                ns_name = args[0].as.object.values[i].as.string;
            } else if (strcmp(args[0].as.object.keys[i], "__interface__") == 0 && 
                       args[0].as.object.values[i].type == VALUE_STRING) {
                if_name = args[0].as.object.values[i].as.string;
            }
        }
        
        if ((ns_name && ns_name[0]) || (if_name && if_name[0])) {
            char dotted[256];
            
            // Prefer namespaced alias first
            if (ns_name && ns_name[0]) {
                snprintf(dotted, sizeof(dotted), "%s.%s", ns_name, callee_str);
                if (is_dynamic_function(dotted)) {
                    kuyil_log_debug("Namespace dispatch: '%s' with %d args (dropping receiver)", dotted, arg_count - 1);
                    Value result = call_dynamic_function(dotted, arg_count - 1, args + 1);
                    ctx->pop_n(ctx->context, arg_count + 1);
                    ctx->push(ctx->context, result);
                    return CALL_RESULT_OK;
                }
            }
            
            // Fallback to interface dotted alias
            if (if_name && if_name[0]) {
                snprintf(dotted, sizeof(dotted), "%s.%s", if_name, callee_str);
                if (is_dynamic_function(dotted)) {
                    kuyil_log_debug("Interface dispatch: '%s' with %d args (dropping receiver)", dotted, arg_count - 1);
                    Value result = call_dynamic_function(dotted, arg_count - 1, args + 1);
                    ctx->pop_n(ctx->context, arg_count + 1);
                    ctx->push(ctx->context, result);
                    return CALL_RESULT_OK;
                }
            }
            
            // If neither dotted alias exists but unqualified exists, still drop receiver
            if (is_dynamic_function(callee_str)) {
                kuyil_log_debug("Unqualified dispatch in namespace context: '%s' with %d args (dropping receiver)", callee_str, arg_count - 1);
                Value result = call_dynamic_function(callee_str, arg_count - 1, args + 1);
                ctx->pop_n(ctx->context, arg_count + 1);
                ctx->push(ctx->context, result);
                return CALL_RESULT_OK;
            }
        }
    }
    
    // Check if it's a registered dynamic function
    if (is_dynamic_function(callee_str)) {
        kuyil_log_debug("vm_call_string_shared: dispatching dynamic function '%s'", callee_str);
        Value result = call_dynamic_function(callee_str, arg_count, args);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    // Built-in functions
    if (strcmp(callee_str, "print") == 0) {
        if (arg_count < 1) {
            ctx->report_error(ctx->context, "print expects at least 1 argument");
            return CALL_RESULT_ERROR;
        }
        
        // Print all arguments separated by spaces
        for (int i = 0; i < arg_count; i++) {
            Value arg = args[i];
            
            // Print the value
            switch (arg.type) {
                case VALUE_NIL:
                    printf("nil");
                    break;
                case VALUE_BOOL:
                    printf("%s", arg.as.boolean ? "true" : "false");
                    break;
                case VALUE_NUMBER:
                    printf("%g", arg.as.number);
                    break;
                case VALUE_STRING:
                    printf("%s", arg.as.string ? arg.as.string : "<null>");
                    break;
                case VALUE_ARRAY:
                    printf("[");
                    for (int j = 0; j < arg.as.array.count; j++) {
                        Value elem = arg.as.array.values[j];
                        if (elem.type == VALUE_STRING) {
                            printf("\"%s\"", elem.as.string);
                        } else if (elem.type == VALUE_NUMBER) {
                            printf("%g", elem.as.number);
                        } else if (elem.type == VALUE_OBJECT) {
                            printf("{");
                            for (int k = 0; k < elem.as.object.count; k++) {
                                printf("\"%s\": ", elem.as.object.keys[k]);
                                Value val = elem.as.object.values[k];
                                if (val.type == VALUE_STRING) {
                                    printf("\"%s\"", val.as.string);
                                } else if (val.type == VALUE_NUMBER) {
                                    printf("%g", val.as.number);
                                } else {
                                    printf("<value type=%d>", val.type);
                                }
                                if (k < elem.as.object.count - 1) printf(", ");
                            }
                            printf("}");
                        } else {
                            printf("<value type=%d>", elem.type);
                        }
                        if (j < arg.as.array.count - 1) printf(", ");
                    }
                    printf("]");
                    break;
                case VALUE_OBJECT:
                    printf("{");
                    for (int j = 0; j < arg.as.object.count; j++) {
                        printf("\"%s\": ", arg.as.object.keys[j]);
                        Value val = arg.as.object.values[j];
                        if (val.type == VALUE_STRING) {
                            printf("\"%s\"", val.as.string);
                        } else if (val.type == VALUE_NUMBER) {
                            printf("%g", val.as.number);
                        } else if (val.type == VALUE_OBJECT) {
                            // Nested object - print recursively
                            printf("{");
                            for (int k = 0; k < val.as.object.count; k++) {
                                printf("\"%s\": ", val.as.object.keys[k]);
                                Value nested = val.as.object.values[k];
                                if (nested.type == VALUE_STRING) {
                                    printf("\"%s\"", nested.as.string);
                                } else if (nested.type == VALUE_NUMBER) {
                                    printf("%g", nested.as.number);
                                } else {
                                    printf("<value type=%d>", nested.type);
                                }
                                if (k < val.as.object.count - 1) printf(", ");
                            }
                            printf("}");
                        } else {
                            printf("<value type=%d>", val.type);
                        }
                        if (j < arg.as.object.count - 1) printf(", ");
                    }
                    printf("}");
                    break;
                default:
                    printf("<value type=%d>", arg.type);
                    break;
            }
            
            // Print space between arguments (except after the last one)
            if (i < arg_count - 1) {
                printf(" ");
            }
        }
        
        // Print newline at the end
        printf("\n");
        fflush(stdout);
        
        ctx->pop_n(ctx->context, arg_count + 1);
        Value nilv = {VALUE_NIL};
        ctx->push(ctx->context, nilv);
        return CALL_RESULT_OK;
    }
    
    if (strcmp(callee_str, "typeof") == 0) {
        if (arg_count != 1) {
            ctx->report_error(ctx->context, "typeof expects 1 argument");
            return CALL_RESULT_ERROR;
        }
        
        Value arg = args[0];
        ctx->pop_n(ctx->context, arg_count + 1);
        
        const char* type_names[] = {"nil", "bool", "number", "string", "array", "object", "function"};
        Value result;
        result.type = VALUE_STRING;
        result.as.string = strdup(type_names[arg.type < 7 ? arg.type : 0]);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    // Bridge invoke: route_bridge_invoke(routePattern, requestJson)
    if (strcmp(callee_str, "route_bridge_invoke") == 0) {
        extern Value builtin_route_bridge_invoke_ctx(int, Value*, CallContext*);
        
        // Call with CallContext so avatar can execute handler directly on its own stack
        Value result = builtin_route_bridge_invoke_ctx(arg_count, args, ctx);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    // Response builder functions for async I/O
    if (strcmp(callee_str, "aio_response_setStatus") == 0) {
        extern Value kyl_aio_response_set_status(int, Value*);
        Value result = kyl_aio_response_set_status(arg_count, args);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    if (strcmp(callee_str, "aio_response_addHeader") == 0) {
        extern Value kyl_aio_response_add_header(int, Value*);
        Value result = kyl_aio_response_add_header(arg_count, args);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    if (strcmp(callee_str, "aio_response_setBody") == 0) {
        extern Value kyl_aio_response_set_body(int, Value*);
        Value result = kyl_aio_response_set_body(arg_count, args);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    if (strcmp(callee_str, "aio_response_get_body") == 0) {
        extern Value builtin_aio_response_get_body(int, Value*);
        Value result = builtin_aio_response_get_body(arg_count, args);
        ctx->pop_n(ctx->context, arg_count + 1);
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    if (strcmp(callee_str, "array_length") == 0) {
        double len = 0.0;
        if (arg_count >= 1 && args[0].type == VALUE_ARRAY) {
            len = (double)args[0].as.array.count;
        }
        ctx->pop_n(ctx->context, arg_count + 1);
        Value result = {VALUE_NUMBER, .as.number = len};
        ctx->push(ctx->context, result);
        return CALL_RESULT_OK;
    }
    
    // Not handled by shared code - let caller handle it (e.g., user-defined functions, test assertions)
    return CALL_RESULT_NOT_HANDLED;
}
