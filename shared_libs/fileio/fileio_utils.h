#ifndef FILEIO_UTILS_H
#define FILEIO_UTILS_H

#include "../kuyil_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

// File operation result structure
typedef struct {
    char* content;
    size_t size;
    int error_code;
    char* error_message;
} FileResult;

// CSV parsing structure
typedef struct {
    char** headers;
    char*** rows;
    int header_count;
    int row_count;
    int error_code;
    char* error_message;
} CSVResult;

// JSON parsing structure (simplified)
typedef struct {
    char* json_string;
    int is_valid;
    int error_code;
    char* error_message;
} JSONResult;

// File security and validation
typedef struct {
    size_t max_file_size;
    char** allowed_extensions;
    int extension_count;
    int validate_paths;
    int sandbox_mode;
} FileConfig;

// Core file reading functions
FileResult* file_read_text(const char* filepath);
CSVResult* file_read_csv(const char* filepath);
JSONResult* file_read_json(const char* filepath);
FileResult* file_read_yaml(const char* filepath);

// File utility functions
int file_exists(const char* filepath);
size_t file_get_size(const char* filepath);
int file_is_readable(const char* filepath);
char* file_get_extension(const char* filepath);
int file_validate_path(const char* filepath);

// Memory management
void file_result_free(FileResult* result);
void csv_result_free(CSVResult* result);
void json_result_free(JSONResult* result);

// Error handling
const char* file_get_error_string(int error_code);

// Configuration
void file_set_config(FileConfig* config);
FileConfig* file_get_default_config(void);

// Kuyil VM interface functions
Value kuyil_file_read_text(int arg_count, Value* args);
Value kuyil_file_read_csv(int arg_count, Value* args);
Value kuyil_file_read_json(int arg_count, Value* args);
Value kuyil_file_read_yaml(int arg_count, Value* args);
Value kuyil_file_exists(int arg_count, Value* args);
Value kuyil_file_size(int arg_count, Value* args);
Value kuyil_file_validate(int arg_count, Value* args);
Value kuyil_file_write_text(int arg_count, Value* args);
Value kuyil_file_write_text(int arg_count, Value* args);

// Error codes
#define FILE_SUCCESS 0
#define FILE_ERROR_NOT_FOUND 1
#define FILE_ERROR_PERMISSION_DENIED 2
#define FILE_ERROR_TOO_LARGE 3
#define FILE_ERROR_INVALID_FORMAT 4
#define FILE_ERROR_MEMORY_ALLOCATION 5
#define FILE_ERROR_INVALID_PATH 6
#define FILE_ERROR_UNSUPPORTED_EXTENSION 7
#define FILE_ERROR_PARSE_ERROR 8

// File size limits
#define MAX_FILE_SIZE_DEFAULT (10 * 1024 * 1024)  // 10MB
#define MAX_LINE_LENGTH 4096
#define MAX_CSV_COLUMNS 1000
#define MAX_CSV_ROWS 100000

// Library initialization
int fileio_init(void);
void fileio_cleanup(void);

#endif // FILEIO_UTILS_H