// System utilities library for Kuyil
// Provides sleep, timestamp, and other system functions

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ast.h"

// Sleep for specified milliseconds
// Usage: sleep(1000) - sleep for 1 second
Value kyl_system_sleep(int argc, Value* argv) {
    // Handle both calling conventions:
    // - Direct call: argc=1, argv[0] = milliseconds
    // - Namespace call: argc=2, argv[0] = namespace object, argv[1] = milliseconds
    Value ms_val;
    if (argc == 2) {
        // Namespace call convention
        ms_val = argv[1];
    } else if (argc == 1) {
        // Direct call convention
        ms_val = argv[0];
    } else {
        // Unexpected argument count
        return (Value){.type = VALUE_NIL};
    }

    if (ms_val.type != VALUE_NUMBER) {
        return (Value){.type = VALUE_NIL};
    }

    int ms = (int)ms_val.as.number;
    usleep(ms * 1000);
    return (Value){.type = VALUE_NIL};
}

// Get current timestamp in milliseconds since epoch
Value kyl_system_timestamp(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Value result;
    result.type = VALUE_NUMBER;
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    // Convert to milliseconds
    long long ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    result.as.number = (double)ms;
    
    return result;
}

// Unescape a JSON string: convert \" to " and \\ to \
// Usage: unescape("{\"key\":\"value\"}") returns {"key":"value"}
Value kyl_system_unescape(int arg_count, Value* args) {
    Value result;
    result.type = VALUE_NIL;
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        fprintf(stderr, "Error: unescape() requires a string argument\n");
        return result;
    }
    
    const char* input = args[0].as.string;
    size_t len = strlen(input);
    
    // Allocate buffer (result will be same size or smaller)
    char* output = (char*)malloc(len + 1);
    if (!output) {
        fprintf(stderr, "Error: unescape() failed to allocate memory\n");
        return result;
    }
    
    size_t i = 0, j = 0;
    while (i < len) {
        if (input[i] == '\\' && i + 1 < len) {
            // Handle escape sequences
            char next = input[i + 1];
            if (next == '"') {
                output[j++] = '"';
                i += 2;
            } else if (next == '\\') {
                output[j++] = '\\';
                i += 2;
            } else if (next == 'n') {
                output[j++] = '\n';
                i += 2;
            } else if (next == 't') {
                output[j++] = '\t';
                i += 2;
            } else if (next == 'r') {
                output[j++] = '\r';
                i += 2;
            } else {
                // Unknown escape, keep backslash
                output[j++] = input[i++];
            }
        } else {
            output[j++] = input[i++];
        }
    }
    output[j] = '\0';
    
    result.type = VALUE_STRING;
    result.as.string = output;
    return result;
}

// Library initialization
const char* library_interface() {
    return 
        "library system\n"
        "  function sleep(milliseconds: any) -> any\n"
        "  function timestamp() -> any\n"
        "  function unescape(str: string) -> string\n";
}

// Function registration
typedef struct {
    const char* name;
    Value (*func)(int, Value*);
} FunctionEntry;

static FunctionEntry functions[] = {
    {"pause", kyl_system_sleep},
    {"timestamp", kyl_system_timestamp},
    {"unescape", kyl_system_unescape},
    {NULL, NULL}
};

Value call_library_function(const char* name, int arg_count, Value* args) {
    for (int i = 0; functions[i].name != NULL; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return functions[i].func(arg_count, args);
        }
    }
    
    Value error;
    error.type = VALUE_NIL;
    fprintf(stderr, "Error: Unknown function '%s' in system library\n", name);
    return error;
}
