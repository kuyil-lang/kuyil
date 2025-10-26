#ifndef KUYIL_LOGGING_H
#define KUYIL_LOGGING_H

#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>

// Log levels
typedef enum {
    LOG_FATAL = 0,
    LOG_ERROR = 1,
    LOG_WARNING = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4
} LogLevel;

// Log context for tracking function calls
typedef struct LogContext {
    char* function_name;
    char* file_name;
    int line_number;
    struct LogContext* parent;
    int depth;
    double start_time;
    bool is_kuyil_function;
} LogContext;

// Logger configuration
typedef struct {
    LogLevel level;
    bool show_timestamp;
    bool show_context;
    bool show_file_info;
    bool colored_output;
    bool trace_calls;
    FILE* output_file;
    char* log_format;
} LogConfig;

// Global logger instance
extern LogConfig* global_logger;
extern LogContext* current_context;

// Core logging functions
void log_init(LogLevel level);
void log_cleanup();
void log_set_level(LogLevel level);
void log_set_output(FILE* file);
void log_set_colored(bool enabled);
void log_set_trace_calls(bool enabled);

// Context management
LogContext* log_push_context(const char* function_name, const char* file, int line, bool is_kuyil);
void log_pop_context();
void log_print_call_stack();

// Main logging functions
void log_message(LogLevel level, const char* file, int line, const char* format, ...);
void log_function_entry(const char* function_name, const char* file, int line);
void log_function_exit(const char* function_name, double duration_ms);

// Convenience macros
#define LOG_FATAL(...) log_message(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) log_message(LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_message(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

// Function tracing macros
#define LOG_ENTER() log_function_entry(__func__, __FILE__, __LINE__)
#define LOG_EXIT() log_function_exit(__func__, 0.0)

// Kuyil specific logging (will be used by VM)
void kuyil_log_fatal(const char* format, ...);
void kuyil_log_error(const char* format, ...);
void kuyil_log_warning(const char* format, ...);
void kuyil_log_info(const char* format, ...);
void kuyil_log_debug(const char* format, ...);

// Context tracking for Kuyil functions
void kuyil_push_function_context(const char* function_name);
void kuyil_pop_function_context();

// Utility functions
const char* log_level_string(LogLevel level);
const char* log_level_color(LogLevel level);
double get_current_time_ms();
void format_timestamp(char* buffer, size_t size);

#endif