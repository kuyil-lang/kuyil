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

// Execute a shell command and capture its output
// Usage: capture("ls -la") returns the output as a string
Value kyl_system_capture(int arg_count, Value* args) {
    Value result;
    result.type = VALUE_NIL;
    
    // Handle both calling conventions:
    // - Direct call: argc=1, argv[0] = command
    // - Namespace call: argc=2, argv[0] = namespace object, argv[1] = command
    Value cmd_val;
    if (arg_count == 2) {
        // Namespace call convention
        cmd_val = args[1];
    } else if (arg_count == 1) {
        // Direct call convention
        cmd_val = args[0];
    } else {
        // Unexpected argument count
        fprintf(stderr, "Error: capture() requires exactly one string argument\n");
        return result;
    }
    
    if (cmd_val.type != VALUE_STRING) {
        fprintf(stderr, "Error: capture() requires a string argument\n");
        return result;
    }
    
    const char* command = cmd_val.as.string;
    
    fprintf(stderr, "[DEBUG capture] About to execute: %s\n", command);
    fflush(stderr);
    
    // Open pipe to command
    FILE* pipe = popen(command, "r");
    fprintf(stderr, "[DEBUG capture] popen returned: %p\n", (void*)pipe);
    fflush(stderr);
    
    if (!pipe) {
        fprintf(stderr, "Error: capture() failed to execute command\n");
        return result;
    }
    
    // Read output into buffer
    char* output = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output = (char*)malloc(output_capacity);
    if (!output) {
        pclose(pipe);
        fprintf(stderr, "Error: capture() failed to allocate memory\n");
        return result;
    }
    
    fprintf(stderr, "[DEBUG capture] Starting to read output...\n");
    fflush(stderr);
    
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        fprintf(stderr, "[DEBUG capture] Read line: %s", buffer);
        fflush(stderr);
        
        size_t len = strlen(buffer);
        if (output_size + len + 1 > output_capacity) {
            output_capacity *= 2;
            char* new_output = (char*)realloc(output, output_capacity);
            if (!new_output) {
                free(output);
                pclose(pipe);
                fprintf(stderr, "Error: capture() failed to reallocate memory\n");
                return result;
            }
            output = new_output;
        }
        strcpy(output + output_size, buffer);
        output_size += len;
    }
    
    fprintf(stderr, "[DEBUG capture] Finished reading, closing pipe...\n");
    fflush(stderr);
    
    pclose(pipe);
    
    fprintf(stderr, "[DEBUG capture] Pipe closed, returning result\n");
    fflush(stderr);
    
    // Return the output as a string
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
        "  function unescape(str: string) -> string\n"
        "  function capture(command: string) -> string\n";
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
    {"capture", kyl_system_capture},
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
