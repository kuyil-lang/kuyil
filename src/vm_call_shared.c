#include "vm_call_shared.h"
#include "vm_library_integration.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>

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
        if (arg_count != 1) {
            ctx->report_error(ctx->context, "print expects 1 argument");
            return CALL_RESULT_ERROR;
        }
        
        Value arg = args[0];
        ctx->pop_n(ctx->context, arg_count + 1);
        
        // Print the value
        switch (arg.type) {
            case VALUE_NIL:
                printf("nil\n");
                break;
            case VALUE_BOOL:
                printf("%s\n", arg.as.boolean ? "true" : "false");
                break;
            case VALUE_NUMBER:
                printf("%g\n", arg.as.number);
                break;
            case VALUE_STRING:
                printf("%s\n", arg.as.string ? arg.as.string : "<null>");
                break;
            default:
                printf("<value type=%d>\n", arg.type);
                break;
        }
        
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
