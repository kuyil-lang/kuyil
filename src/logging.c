#define _POSIX_C_SOURCE 200809L
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// Global logger state
LogConfig* global_logger = NULL;
LogContext* current_context = NULL;

// ANSI color codes
#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_GRAY    "\x1b[90m"

// Initialize the logging system
void log_init(LogLevel level) {
    if (global_logger != NULL) {
        log_cleanup();
    }
    
    global_logger = malloc(sizeof(LogConfig));
    global_logger->level = level;
    global_logger->show_timestamp = true;
    global_logger->show_context = true;
    global_logger->show_file_info = false;
    global_logger->colored_output = isatty(fileno(stderr));
    global_logger->trace_calls = true;
    global_logger->output_file = stderr;
    global_logger->log_format = strdup("[%s] %s %s%s%s\n");
    
    current_context = NULL;
}

// Cleanup logging system
void log_cleanup() {
    if (global_logger) {
        free(global_logger->log_format);
        free(global_logger);
        global_logger = NULL;
    }
    
    // Clean up context stack
    while (current_context) {
        log_pop_context();
    }
}

// Configuration functions
void log_set_level(LogLevel level) {
    if (global_logger) {
        global_logger->level = level;
    }
}

void log_set_output(FILE* file) {
    if (global_logger) {
        global_logger->output_file = file;
        global_logger->colored_output = isatty(fileno(file));
    }
}

void log_set_colored(bool enabled) {
    if (global_logger) {
        global_logger->colored_output = enabled;
    }
}

void log_set_trace_calls(bool enabled) {
    if (global_logger) {
        global_logger->trace_calls = enabled;
    }
}

// Utility functions
double get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

void format_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Add milliseconds
    char ms_buffer[32];
    snprintf(ms_buffer, sizeof(ms_buffer), ".%03d", (int)(tv.tv_usec / 1000));
    strncat(buffer, ms_buffer, size - strlen(buffer) - 1);
}

const char* log_level_string(LogLevel level) {
    switch (level) {
        case LOG_FATAL:   return "FATAL";
        case LOG_ERROR:   return "ERROR";
        case LOG_WARNING: return "WARN ";
        case LOG_INFO:    return "INFO ";
        case LOG_DEBUG:   return "DEBUG";
        default:          return "UNKNOWN";
    }
}

const char* log_level_color(LogLevel level) {
    if (!global_logger || !global_logger->colored_output) {
        return "";
    }
    
    switch (level) {
        case LOG_FATAL:   return COLOR_MAGENTA;
        case LOG_ERROR:   return COLOR_RED;
        case LOG_WARNING: return COLOR_YELLOW;
        case LOG_INFO:    return COLOR_GREEN;
        case LOG_DEBUG:   return COLOR_CYAN;
        default:          return COLOR_RESET;
    }
}

// Context management
LogContext* log_push_context(const char* function_name, const char* file, int line, bool is_kuyil) {
    LogContext* new_context = malloc(sizeof(LogContext));
    new_context->function_name = strdup(function_name);
    new_context->file_name = file ? strdup(file) : NULL;
    new_context->line_number = line;
    new_context->parent = current_context;
    new_context->depth = current_context ? current_context->depth + 1 : 0;
    new_context->start_time = get_current_time_ms();
    new_context->is_kuyil_function = is_kuyil;
    
    current_context = new_context;
    
    if (global_logger && global_logger->trace_calls) {
        char indent[256] = "";
        for (int i = 0; i < new_context->depth * 2 && i < 254; i++) {
            indent[i] = ' ';
            indent[i + 1] = '\0';
        }
        
        const char* color = log_level_color(LOG_DEBUG);
        const char* reset = global_logger->colored_output ? COLOR_RESET : "";
        const char* func_type = is_kuyil ? "[KUYIL]" : "[C]";
        
        fprintf(global_logger->output_file, "%s%s%s→ %s%s%s\n", 
                color, indent, func_type, function_name, reset, 
                file ? file : "");
    }
    
    return new_context;
}

void log_pop_context() {
    if (!current_context) return;
    
    double duration = get_current_time_ms() - current_context->start_time;
    
    if (global_logger && global_logger->trace_calls) {
        char indent[256] = "";
        for (int i = 0; i < current_context->depth * 2 && i < 254; i++) {
            indent[i] = ' ';
            indent[i + 1] = '\0';
        }
        
        const char* color = log_level_color(LOG_DEBUG);
        const char* reset = global_logger->colored_output ? COLOR_RESET : "";
        const char* func_type = current_context->is_kuyil_function ? "[KUYIL]" : "[C]";
        
        fprintf(global_logger->output_file, "%s%s%s← %s (%.2fms)%s\n", 
                color, indent, func_type, current_context->function_name, duration, reset);
    }
    
    LogContext* old_context = current_context;
    current_context = current_context->parent;
    
    free(old_context->function_name);
    if (old_context->file_name) {
        free(old_context->file_name);
    }
    free(old_context);
}

void log_print_call_stack() {
    if (!global_logger) return;
    
    fprintf(global_logger->output_file, "\n=== CALL STACK ===\n");
    
    LogContext* context = current_context;
    int depth = 0;
    
    while (context) {
        char indent[256] = "";
        for (int i = 0; i < depth * 2 && i < 254; i++) {
            indent[i] = ' ';
            indent[i + 1] = '\0';
        }
        
        const char* func_type = context->is_kuyil_function ? "[KUYIL]" : "[C]";
        double duration = get_current_time_ms() - context->start_time;
        
        fprintf(global_logger->output_file, "%s%s %s (%.2fms)\n", 
                indent, func_type, context->function_name, duration);
        
        if (context->file_name && global_logger->show_file_info) {
            fprintf(global_logger->output_file, "%s    at %s:%d\n", 
                    indent, context->file_name, context->line_number);
        }
        
        context = context->parent;
        depth++;
    }
    
    fprintf(global_logger->output_file, "================\n\n");
}

// Main logging function
void log_message(LogLevel level, const char* file, int line, const char* format, ...) {
    if (!global_logger || level > global_logger->level) {
        return;
    }
    
    char timestamp[64] = "";
    if (global_logger->show_timestamp) {
        format_timestamp(timestamp, sizeof(timestamp));
    }
    
    // Build context string
    char context_str[512] = "";
    if (global_logger->show_context && current_context) {
        snprintf(context_str, sizeof(context_str), "[%s] ", current_context->function_name);
    }
    
    // Build file info string
    char file_info[256] = "";
    if (global_logger->show_file_info && file) {
        const char* filename = strrchr(file, '/');
        filename = filename ? filename + 1 : file;
        snprintf(file_info, sizeof(file_info), " (%s:%d)", filename, line);
    }
    
    // Format the actual log message
    va_list args;
    va_start(args, format);
    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Output the formatted log message
    const char* color = log_level_color(level);
    const char* reset = global_logger->colored_output ? COLOR_RESET : "";
    const char* level_str = log_level_string(level);
    
    fprintf(global_logger->output_file, "[%s] %s%s%s %s%s%s%s\n", 
            timestamp, color, level_str, reset, context_str, message, file_info, reset);
    
    // Auto-flush for errors and fatal messages
    if (level <= LOG_ERROR) {
        fflush(global_logger->output_file);
    }
    
    // Print call stack for fatal errors
    if (level == LOG_FATAL) {
        log_print_call_stack();
        
        // Exit on fatal errors
        log_cleanup();
        exit(1);
    }
}

// Function tracing
void log_function_entry(const char* function_name, const char* file, int line) {
    log_push_context(function_name, file, line, false);
}

void log_function_exit(const char* function_name, double duration_ms) {
    (void)function_name;  // Unused parameter
    (void)duration_ms;    // Unused parameter
    log_pop_context();
}

// Kuyil specific logging functions
void kuyil_log_fatal(const char* format, ...) {
    if (!global_logger) return;
    
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_FATAL, "kuyil", 0, "%s", message);
}

void kuyil_log_error(const char* format, ...) {
    if (!global_logger) return;
    
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_ERROR, "kuyil", 0, "%s", message);
}

void kuyil_log_warning(const char* format, ...) {
    if (!global_logger) return;
    
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_WARNING, "kuyil", 0, "%s", message);
}

void kuyil_log_info(const char* format, ...) {
    if (!global_logger) return;
    
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_INFO, "kuyil", 0, "%s", message);
}

void kuyil_log_debug(const char* format, ...) {
    if (!global_logger) return;
    
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_DEBUG, "kuyil", 0, "%s", message);
}

// Kuyil function context tracking
void kuyil_push_function_context(const char* function_name) {
    log_push_context(function_name, "kuyil_script", 0, true);
}

void kuyil_pop_function_context() {
    log_pop_context();
}