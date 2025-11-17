// GNU extension for strdup - must be defined before includes
#define _GNU_SOURCE

// Standard headers required for file operations
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "fileio_utils.h"

// Kuyil interface signature metadata
__attribute__((visibility("default")))
const char* kyl_interface_signature_text = 
    "file readText(path: string) -> string\n"
    "file readCsv(path: string) -> array\n"
    "file readJson(path: string) -> object\n"
    "file readYaml(path: string) -> object\n"
    "file exists(path: string) -> bool\n"
    "file size(path: string) -> int32\n"
    "file validate(path: string) -> bool\n"
    "file writeText(path: string) -> bool\n"
    "file info(path: string) -> object\n";

// Optional YAML support - uncomment if libyaml is available
// #include <yaml.h>
#define YAML_SUPPORT 0

// Global configuration
static FileConfig* g_file_config = NULL;

// Error messages
static const char* error_messages[] = {
    "Success",
    "File not found",
    "Permission denied",
    "File too large",
    "Invalid file format",
    "Memory allocation failed",
    "Invalid file path",
    "Unsupported file extension",
    "Parse error"
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* file_get_error_string(int error_code) {
    if (error_code >= 0 && error_code < sizeof(error_messages) / sizeof(error_messages[0])) {
        return error_messages[error_code];
    }
    return "Unknown error";
}

int file_exists(const char* filepath) {
    if (!filepath) return 0;
    int ok = access(filepath, F_OK) == 0;
    return ok;
}

size_t file_get_size(const char* filepath) {
    if (!filepath) return 0;
    
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

int file_is_readable(const char* filepath) {
    if (!filepath) return 0;
    return access(filepath, R_OK) == 0;
}

char* file_get_extension(const char* filepath) {
    if (!filepath) return NULL;
    
    const char* dot = strrchr(filepath, '.');
    if (!dot || dot == filepath) return NULL;
    
    return strdup(dot + 1);
}

int file_validate_path(const char* filepath) {
    if (!filepath) return FILE_ERROR_INVALID_PATH;
    
    // Check for path traversal attacks
    if (strstr(filepath, "../") || strstr(filepath, "..\\") || 
        strstr(filepath, "/..") || strstr(filepath, "\\..")) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    // Check for absolute paths in sandbox mode
    if (g_file_config && g_file_config->sandbox_mode && filepath[0] == '/') {
        return FILE_ERROR_INVALID_PATH;
    }
    
    return FILE_SUCCESS;
}

// Ensure parent directory exists (naive, creates single-level if missing)
static void ensure_parent_dir(const char* filepath) {
    if (!filepath) return;
    const char* slash = strrchr(filepath, '/');
    if (!slash) return;
    size_t len = (size_t)(slash - filepath);
    if (len == 0) return;
    char* dir = (char*)malloc(len + 1);
    if (!dir) return;
    memcpy(dir, filepath, len);
    dir[len] = '\0';
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0755);
    }
    free(dir);
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void file_result_free(FileResult* result) {
    if (!result) return;
    
    if (result->content) free(result->content);
    if (result->error_message) free(result->error_message);
    free(result);
}

void csv_result_free(CSVResult* result) {
    if (!result) return;
    
    if (result->headers) {
        for (int i = 0; i < result->header_count; i++) {
            if (result->headers[i]) free(result->headers[i]);
        }
        free(result->headers);
    }
    
    if (result->rows) {
        for (int i = 0; i < result->row_count; i++) {
            if (result->rows[i]) {
                for (int j = 0; j < result->header_count; j++) {
                    if (result->rows[i][j]) free(result->rows[i][j]);
                }
                free(result->rows[i]);
            }
        }
        free(result->rows);
    }
    
    if (result->error_message) free(result->error_message);
    free(result);
}

void json_result_free(JSONResult* result) {
    if (!result) return;
    
    if (result->json_string) free(result->json_string);
    if (result->error_message) free(result->error_message);
    free(result);
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

FileConfig* file_get_default_config(void) {
    FileConfig* config = malloc(sizeof(FileConfig));
    if (!config) return NULL;
    
    config->max_file_size = MAX_FILE_SIZE_DEFAULT;
    config->allowed_extensions = NULL;
    config->extension_count = 0;
    config->validate_paths = 1;
    config->sandbox_mode = 0;
    
    return config;
}

void file_set_config(FileConfig* config) {
    if (g_file_config) {
        if (g_file_config->allowed_extensions) {
            for (int i = 0; i < g_file_config->extension_count; i++) {
                if (g_file_config->allowed_extensions[i]) {
                    free(g_file_config->allowed_extensions[i]);
                }
            }
            free(g_file_config->allowed_extensions);
        }
        free(g_file_config);
    }
    
    g_file_config = config;
}

// ============================================================================
// TEXT FILE READING
// ============================================================================

FileResult* file_read_text(const char* filepath) {
    FileResult* result = malloc(sizeof(FileResult));
    if (!result) return NULL;
    
    // Initialize result
    result->content = NULL;
    result->size = 0;
    result->error_code = FILE_SUCCESS;
    result->error_message = NULL;
    
    // Validate path
    int path_error = file_validate_path(filepath);
    if (path_error != FILE_SUCCESS) {
        result->error_code = path_error;
        result->error_message = strdup(file_get_error_string(path_error));
        return result;
    }
    
    // Check file existence
    if (!file_exists(filepath)) {
        result->error_code = FILE_ERROR_NOT_FOUND;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_NOT_FOUND));
        return result;
    }
    
    // Check readability
    if (!file_is_readable(filepath)) {
        result->error_code = FILE_ERROR_PERMISSION_DENIED;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_PERMISSION_DENIED));
        return result;
    }
    
    // Check file size
    size_t file_size = file_get_size(filepath);
    size_t max_size = g_file_config ? g_file_config->max_file_size : MAX_FILE_SIZE_DEFAULT;
    if (file_size > max_size) {
        result->error_code = FILE_ERROR_TOO_LARGE;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_TOO_LARGE));
        return result;
    }
    
    // Open and read file
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        result->error_code = FILE_ERROR_PERMISSION_DENIED;
        result->error_message = strdup(strerror(errno));
        return result;
    }
    
    // Allocate buffer
    result->content = malloc(file_size + 1);
    if (!result->content) {
        fclose(file);
        result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        return result;
    }
    
    // Read content
    size_t bytes_read = fread(result->content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        free(result->content);
        result->content = NULL;
        result->error_code = FILE_ERROR_INVALID_FORMAT;
        result->error_message = strdup("Failed to read complete file");
        return result;
    }
    
    // Null-terminate
    result->content[file_size] = '\0';
    result->size = file_size;
    
    return result;
}

// ============================================================================
// CSV FILE READING
// ============================================================================

static char** parse_csv_line(const char* line, int* field_count) {
    *field_count = 0;
    if (!line || strlen(line) == 0) return NULL;
    
    char** fields = NULL;
    int capacity = 10;
    fields = malloc(capacity * sizeof(char*));
    if (!fields) return NULL;
    
    const char* ptr = line;
    const char* field_start = ptr;
    int in_quotes = 0;
    int field_len = 0;
    
    while (*ptr) {
        if (*ptr == '"') {
            in_quotes = !in_quotes;
        } else if (*ptr == ',' && !in_quotes) {
            // End of field
            if (*field_count >= capacity) {
                capacity *= 2;
                fields = realloc(fields, capacity * sizeof(char*));
                if (!fields) return NULL;
            }
            
            fields[*field_count] = malloc(field_len + 1);
            strncpy(fields[*field_count], field_start, field_len);
            fields[*field_count][field_len] = '\0';
            
            // Trim quotes if present
            char* field = fields[*field_count];
            if (field[0] == '"' && field[strlen(field)-1] == '"') {
                memmove(field, field + 1, strlen(field) - 1);
                field[strlen(field) - 2] = '\0';
            }
            
            (*field_count)++;
            ptr++;
            field_start = ptr;
            field_len = 0;
            continue;
        }
        
        field_len++;
        ptr++;
    }
    
    // Handle last field
    if (*field_count >= capacity) {
        capacity++;
        fields = realloc(fields, capacity * sizeof(char*));
        if (!fields) return NULL;
    }
    
    fields[*field_count] = malloc(field_len + 1);
    strncpy(fields[*field_count], field_start, field_len);
    fields[*field_count][field_len] = '\0';
    
    // Trim quotes
    char* field = fields[*field_count];
    if (field_len > 1 && field[0] == '"' && field[field_len-1] == '"') {
        memmove(field, field + 1, field_len - 1);
        field[field_len - 2] = '\0';
    }
    
    (*field_count)++;
    return fields;
}

CSVResult* file_read_csv(const char* filepath) {
    CSVResult* result = malloc(sizeof(CSVResult));
    if (!result) return NULL;
    
    // Initialize result
    result->headers = NULL;
    result->rows = NULL;
    result->header_count = 0;
    result->row_count = 0;
    result->error_code = FILE_SUCCESS;
    result->error_message = NULL;
    
    // Read text content first
    FileResult* text_result = file_read_text(filepath);
    if (!text_result || text_result->error_code != FILE_SUCCESS) {
        if (text_result) {
            result->error_code = text_result->error_code;
            result->error_message = strdup(text_result->error_message ? text_result->error_message : "Unknown error");
            file_result_free(text_result);
        } else {
            result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
            result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        }
        return result;
    }
    
    // Parse CSV content
    char* content = strdup(text_result->content);
    file_result_free(text_result);
    
    if (!content) {
        result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        return result;
    }
    
    // Split into lines
    char* line = strtok(content, "\n\r");
    if (!line) {
        free(content);
        result->error_code = FILE_ERROR_INVALID_FORMAT;
        result->error_message = strdup("Empty CSV file");
        return result;
    }
    
    // Parse headers
    result->headers = parse_csv_line(line, &result->header_count);
    if (!result->headers || result->header_count == 0) {
        free(content);
        result->error_code = FILE_ERROR_PARSE_ERROR;
        result->error_message = strdup("Failed to parse CSV headers");
        return result;
    }
    
    // Allocate rows array
    int row_capacity = 100;
    result->rows = malloc(row_capacity * sizeof(char**));
    if (!result->rows) {
        free(content);
        result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
        result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        return result;
    }
    
    // Parse data rows
    while ((line = strtok(NULL, "\n\r")) != NULL) {
        if (result->row_count >= MAX_CSV_ROWS) break;
        
        if (result->row_count >= row_capacity) {
            row_capacity *= 2;
            result->rows = realloc(result->rows, row_capacity * sizeof(char**));
            if (!result->rows) {
                free(content);
                result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
                result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
                return result;
            }
        }
        
        int field_count;
        result->rows[result->row_count] = parse_csv_line(line, &field_count);
        
        if (result->rows[result->row_count] && field_count == result->header_count) {
            result->row_count++;
        } else {
            // Free invalid row
            if (result->rows[result->row_count]) {
                for (int i = 0; i < field_count; i++) {
                    free(result->rows[result->row_count][i]);
                }
                free(result->rows[result->row_count]);
            }
        }
    }
    
    free(content);
    return result;
}

// ============================================================================
// JSON FILE READING
// ============================================================================

JSONResult* file_read_json(const char* filepath) {
    JSONResult* result = malloc(sizeof(JSONResult));
    if (!result) return NULL;
    
    // Initialize result
    result->json_string = NULL;
    result->is_valid = 0;
    result->error_code = FILE_SUCCESS;
    result->error_message = NULL;
    
    // Read text content
    FileResult* text_result = file_read_text(filepath);
    if (!text_result || text_result->error_code != FILE_SUCCESS) {
        if (text_result) {
            result->error_code = text_result->error_code;
            result->error_message = strdup(text_result->error_message ? text_result->error_message : "Unknown error");
            file_result_free(text_result);
        } else {
            result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
            result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        }
        return result;
    }
    
    // Basic JSON validation (check for balanced braces)
    const char* content = text_result->content;
    int brace_count = 0;
    int bracket_count = 0;
    int in_string = 0;
    int escaped = 0;
    
    for (size_t i = 0; i < text_result->size; i++) {
        char c = content[i];
        
        if (escaped) {
            escaped = 0;
            continue;
        }
        
        if (c == '\\' && in_string) {
            escaped = 1;
            continue;
        }
        
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        
        if (!in_string) {
            if (c == '{') brace_count++;
            else if (c == '}') brace_count--;
            else if (c == '[') bracket_count++;
            else if (c == ']') bracket_count--;
        }
    }
    
    // Check if JSON is balanced
    if (brace_count == 0 && bracket_count == 0) {
        result->is_valid = 1;
        result->json_string = strdup(text_result->content);
    } else {
        result->error_code = FILE_ERROR_PARSE_ERROR;
        result->error_message = strdup("Invalid JSON format: unbalanced braces or brackets");
    }
    
    file_result_free(text_result);
    return result;
}

// ============================================================================
// YAML FILE READING
// ============================================================================

FileResult* file_read_yaml(const char* filepath) {
    FileResult* result = malloc(sizeof(FileResult));
    if (!result) return NULL;
    
    // Initialize result
    result->content = NULL;
    result->size = 0;
    result->error_code = FILE_SUCCESS;
    result->error_message = NULL;
    
    // Read text content first
    FileResult* text_result = file_read_text(filepath);
    if (!text_result || text_result->error_code != FILE_SUCCESS) {
        if (text_result) {
            result->error_code = text_result->error_code;
            result->error_message = strdup(text_result->error_message ? text_result->error_message : "Unknown error");
            file_result_free(text_result);
        } else {
            result->error_code = FILE_ERROR_MEMORY_ALLOCATION;
            result->error_message = strdup(file_get_error_string(FILE_ERROR_MEMORY_ALLOCATION));
        }
        return result;
    }
    
#if YAML_SUPPORT
    // Full YAML validation using libyaml (when available)
    yaml_parser_t parser;
    yaml_event_t event;
    
    if (!yaml_parser_initialize(&parser)) {
        file_result_free(text_result);
        result->error_code = FILE_ERROR_PARSE_ERROR;
        result->error_message = strdup("Failed to initialize YAML parser");
        return result;
    }
    
    yaml_parser_set_input_string(&parser, (const unsigned char*)text_result->content, text_result->size);
    
    int valid_yaml = 1;
    do {
        if (!yaml_parser_parse(&parser, &event)) {
            valid_yaml = 0;
            break;
        }
        yaml_event_delete(&event);
    } while (event.type != YAML_STREAM_END_EVENT);
    
    yaml_parser_delete(&parser);
    
    if (valid_yaml) {
        result->content = strdup(text_result->content);
        result->size = text_result->size;
    } else {
        result->error_code = FILE_ERROR_PARSE_ERROR;
        result->error_message = strdup("Invalid YAML format");
    }
#else
    // Basic YAML validation (check for basic structure)
    const char* content = text_result->content;
    int has_yaml_markers = 0;
    
    // Look for YAML indicators
    if (strstr(content, "---") || strchr(content, ':')) {
        has_yaml_markers = 1;
    }
    
    if (has_yaml_markers) {
        result->content = strdup(text_result->content);
        result->size = text_result->size;
    } else {
        result->error_code = FILE_ERROR_PARSE_ERROR;
        result->error_message = strdup("File does not appear to be valid YAML");
    }
#endif
    
    file_result_free(text_result);
    return result;
}

// ============================================================================
// KUYIL VM INTERFACE FUNCTIONS
// ============================================================================

Value kuyil_file_read_text(int arg_count, Value* args) {
    fprintf(stderr, "[fileio] === kuyil_file_read_text ENTRY ===\n"); fflush(stderr);
    Value result; memset(&result, 0, sizeof(Value)); result.type = VALUE_NIL;
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        fprintf(stderr, "[fileio] readText: invalid args (expected 1 string)\n"); fflush(stderr);
        return result;
    }
    const char* path = args[0].as.string;
    fprintf(stderr, "[fileio] readText: path='%s'\n", path ? path : "(null)"); fflush(stderr);
    if (!path) return result;
    int validation = file_validate_path(path);
    fprintf(stderr, "[fileio] readText: validation=%d\n", validation); fflush(stderr);
    if (validation != FILE_SUCCESS) return result;
    int existsFlag = file_exists(path);
    fprintf(stderr, "[fileio] readText: exists=%d\n", existsFlag); fflush(stderr);
    if (!existsFlag) return result;
    int readable = file_is_readable(path);
    fprintf(stderr, "[fileio] readText: readable=%d\n", readable); fflush(stderr);
    if (!readable) return result;
    size_t sz = file_get_size(path);
    fprintf(stderr, "[fileio] readText: size=%zu\n", sz); fflush(stderr);
    FileResult* file_result = file_read_text(path);
    if (!file_result) { fprintf(stderr, "[fileio] readText: file_read_text returned NULL\n"); fflush(stderr); return result; }
    if (file_result->error_code == FILE_SUCCESS && file_result->content) {
        result.type = VALUE_STRING;
        result.as.string = strdup(file_result->content);
        fprintf(stderr, "[fileio] readText: SUCCESS, returning string len=%zu\n", strlen(result.as.string)); fflush(stderr);
    } else {
        fprintf(stderr, "[fileio] readText: failure code=%d msg=%s\n", file_result->error_code, file_result->error_message ? file_result->error_message : "(none)"); fflush(stderr);
    }
    file_result_free(file_result);
    fprintf(stderr, "[fileio] === kuyil_file_read_text EXIT type=%d ===\n", result.type); fflush(stderr);
    return result;
}

/* Helper to append a row (Value array) into a result (Value array) */
static int append_row_to_result(Value* result, Value rowVal) {
    int newCount = result->as.array.count + 1;
    Value* newArr = (Value*)realloc(result->as.array.values, (size_t)newCount * sizeof(Value));
    if (!newArr) {
        return 0;
    }
    result->as.array.values = newArr;
    result->as.array.values[result->as.array.count] = rowVal;
    result->as.array.count = newCount;
    return 1;
}

Value kuyil_file_read_csv(int arg_count, Value* args) {
    Value result = (Value){ .type = VALUE_NIL };
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    const char* path = args[0].as.string;
    CSVResult* csv = file_read_csv(path);
    if (!csv || csv->error_code != FILE_SUCCESS) {
        if (csv) csv_result_free(csv);
        return result;
    }

    // Build array-of-arrays: first row = headers; subsequent rows = data rows
    result.type = VALUE_ARRAY;
    result.as.array.count = 0;
    result.as.array.values = NULL;

    // Create header row
    Value headerRow; headerRow.type = VALUE_ARRAY; headerRow.as.array.count = csv->header_count; 
    headerRow.as.array.values = (Value*)calloc(csv->header_count, sizeof(Value));
    if (!headerRow.as.array.values) { csv_result_free(csv); result.type = VALUE_NIL; return result; }
    for (int j = 0; j < csv->header_count; j++) {
        headerRow.as.array.values[j].type = VALUE_STRING;
        headerRow.as.array.values[j].as.string = strdup(csv->headers && csv->headers[j] ? csv->headers[j] : "");
    }
    if (!append_row_to_result(&result, headerRow)) { csv_result_free(csv); result.type = VALUE_NIL; return result; }

    // Data rows
    for (int i = 0; i < csv->row_count; i++) {
        Value rowVal; rowVal.type = VALUE_ARRAY; rowVal.as.array.count = csv->header_count;
        rowVal.as.array.values = (Value*)calloc(csv->header_count, sizeof(Value));
        if (!rowVal.as.array.values) { csv_result_free(csv); result.type = VALUE_NIL; return result; }
        for (int j = 0; j < csv->header_count; j++) {
            rowVal.as.array.values[j].type = VALUE_STRING;
            const char* cell = (csv->rows && csv->rows[i] && j < csv->header_count) ? csv->rows[i][j] : "";
            rowVal.as.array.values[j].as.string = strdup(cell ? cell : "");
        }
        if (!append_row_to_result(&result, rowVal)) { csv_result_free(csv); result.type = VALUE_NIL; return result; }
    }

    csv_result_free(csv);
    return result;
}

Value kuyil_file_read_json(int arg_count, Value* args) {
    Value result = {VALUE_NIL, .as = {.number = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    JSONResult* json_result = file_read_json(args[0].as.string);
    if (!json_result) {
        return result;
    }
    
    if (json_result->error_code == FILE_SUCCESS && json_result->json_string) {
        result.type = VALUE_STRING;
        result.as.string = strdup(json_result->json_string);
    }
    
    json_result_free(json_result);
    return result;
}

Value kuyil_file_read_yaml(int arg_count, Value* args) {
    Value result = {VALUE_NIL, .as = {.number = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    FileResult* yaml_result = file_read_yaml(args[0].as.string);
    if (!yaml_result) {
        return result;
    }
    
    if (yaml_result->error_code == FILE_SUCCESS && yaml_result->content) {
        result.type = VALUE_STRING;
        result.as.string = strdup(yaml_result->content);
    }
    
    file_result_free(yaml_result);
    return result;
}

Value kuyil_file_exists(int arg_count, Value* args) {
    Value result = {VALUE_BOOL, .as = {.boolean = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        fprintf(stderr, "[kuyil_file_exists] ERROR: invalid args\n"); fflush(stderr);
        return result;
    }
    
    const char* path = args[0].as.string;
    fprintf(stderr, "[kuyil_file_exists] checking path='%s'\n", path ? path : "(null)"); fflush(stderr);
    int exists = file_exists(path);
    fprintf(stderr, "[kuyil_file_exists] file_exists() returned %d\n", exists); fflush(stderr);
    result.as.boolean = exists;
    fprintf(stderr, "[kuyil_file_exists] returning bool=%d\n", result.as.boolean); fflush(stderr);
    return result;
}

Value kuyil_file_size(int arg_count, Value* args) {
    Value result = {VALUE_NUMBER, .as = {.number = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    result.as.number = (double)file_get_size(args[0].as.string);
    return result;
}

Value kuyil_file_validate(int arg_count, Value* args) {
    Value result = {VALUE_BOOL, .as = {.boolean = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    int validation_result = file_validate_path(args[0].as.string);
    result.as.boolean = (validation_result == FILE_SUCCESS);
    return result;
}

// ============================================================================
// LIBRARY INITIALIZATION
// ============================================================================

int fileio_init(void) {
    if (!g_file_config) {
        g_file_config = file_get_default_config();
        if (!g_file_config) return 0;
    }
    return 1;
}

void fileio_cleanup(void) {
    if (g_file_config) {
        file_set_config(NULL);
    }
}

// ============================================================================
// TEXT FILE WRITING (simple)
// ============================================================================

static int file_write_text_internal(const char* filepath, const char* content) {
    if (!filepath || !content) return 0;
    int path_error = file_validate_path(filepath);
    if (path_error != FILE_SUCCESS) return 0;
    ensure_parent_dir(filepath);
    FILE* f = fopen(filepath, "wb");
    if (!f) return 0;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}

Value kuyil_file_write_text(int arg_count, Value* args) {
    Value result = {VALUE_BOOL, .as = {.boolean = 0}};
    if (arg_count < 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        return result;
    }
    const char* path = args[0].as.string;
    const char* content = args[1].as.string;
    int ok = file_write_text_internal(path, content);
    result.as.boolean = ok ? 1 : 0;
    return result;
}

// Get MIME type from file extension
static const char* get_mime_type(const char* filepath) {
    const char* ext = file_get_extension(filepath);
    if (!ext) return "application/octet-stream";
    
    // Common text formats
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) return "text/html";
    if (strcmp(ext, "css") == 0) return "text/css";
    if (strcmp(ext, "js") == 0) return "application/javascript";
    if (strcmp(ext, "json") == 0) return "application/json";
    if (strcmp(ext, "xml") == 0) return "application/xml";
    if (strcmp(ext, "txt") == 0) return "text/plain";
    if (strcmp(ext, "csv") == 0) return "text/csv";
    if (strcmp(ext, "md") == 0) return "text/markdown";
    
    // Image formats
    if (strcmp(ext, "png") == 0) return "image/png";
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, "gif") == 0) return "image/gif";
    if (strcmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcmp(ext, "webp") == 0) return "image/webp";
    
    // Font formats
    if (strcmp(ext, "woff") == 0) return "font/woff";
    if (strcmp(ext, "woff2") == 0) return "font/woff2";
    if (strcmp(ext, "ttf") == 0) return "font/ttf";
    if (strcmp(ext, "otf") == 0) return "font/otf";
    
    // Archive formats
    if (strcmp(ext, "zip") == 0) return "application/zip";
    if (strcmp(ext, "tar") == 0) return "application/x-tar";
    if (strcmp(ext, "gz") == 0) return "application/gzip";
    
    // Binary formats
    if (strcmp(ext, "pdf") == 0) return "application/pdf";
    if (strcmp(ext, "wasm") == 0) return "application/wasm";
    
    free((char*)ext);
    return "application/octet-stream";
}

// Comprehensive file info function
// Returns: { path, size, contentType, modifiedTime, readable, writable, data }
Value kuyil_file_info(int arg_count, Value* args) {
    Value result = {.type = VALUE_NIL};
    
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    const char* path = args[0].as.string;
    
    // Validate path
    if (file_validate_path(path) != FILE_SUCCESS) {
        return result;
    }
    
    // Get file path (resolve if needed)
    char filepath[1024];
    if (path[0] == '/') {
        snprintf(filepath, sizeof(filepath), "%s", path);
    } else {
        snprintf(filepath, sizeof(filepath), "%s", path);
    }
    
    // Check if file exists
    if (!file_exists(filepath)) {
        return result;
    }
    
    // Get file stats
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return result;
    }
    
    // Read file content as bytes
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        return result;
    }
    
    size_t file_size = st.st_size;
    unsigned char* bytes = malloc(file_size);
    if (!bytes) {
        fclose(file);
        return result;
    }
    
    size_t bytes_read = fread(bytes, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        free(bytes);
        return result;
    }
    
    // Create byte array
    Value* byte_values = malloc(file_size * sizeof(Value));
    for (size_t i = 0; i < file_size; i++) {
        byte_values[i].type = VALUE_NUMBER;
        byte_values[i].as.number = (double)bytes[i];
    }
    free(bytes);
    
    // Create result object with 7 fields
    result.type = VALUE_OBJECT;
    result.as.object.count = 7;
    result.as.object.keys = malloc(7 * sizeof(char*));
    result.as.object.values = malloc(7 * sizeof(Value));
    
    // path field
    result.as.object.keys[0] = strdup("path");
    result.as.object.values[0].type = VALUE_STRING;
    result.as.object.values[0].as.string = strdup(filepath);
    
    // size field
    result.as.object.keys[1] = strdup("size");
    result.as.object.values[1].type = VALUE_NUMBER;
    result.as.object.values[1].as.number = (double)file_size;
    
    // contentType field
    result.as.object.keys[2] = strdup("contentType");
    result.as.object.values[2].type = VALUE_STRING;
    result.as.object.values[2].as.string = strdup(get_mime_type(filepath));
    
    // modifiedTime field (Unix timestamp)
    result.as.object.keys[3] = strdup("modifiedTime");
    result.as.object.values[3].type = VALUE_NUMBER;
    result.as.object.values[3].as.number = (double)st.st_mtime;
    
    // readable field
    result.as.object.keys[4] = strdup("readable");
    result.as.object.values[4].type = VALUE_BOOL;
    result.as.object.values[4].as.boolean = file_is_readable(filepath);
    
    // writable field
    result.as.object.keys[5] = strdup("writable");
    result.as.object.values[5].type = VALUE_BOOL;
    result.as.object.values[5].as.boolean = (access(filepath, W_OK) == 0);
    
    // data field (byte array)
    result.as.object.keys[6] = strdup("data");
    result.as.object.values[6].type = VALUE_ARRAY;
    result.as.object.values[6].as.array.count = file_size;
    result.as.object.values[6].as.array.values = byte_values;
    
    return result;
}

// LowerCamel aliases for cleaner interface
Value readText(int arg_count, Value* args) { 
    fprintf(stderr, "[WRAPPER readText] called with %d args\n", arg_count); fflush(stderr);
    Value result = kuyil_file_read_text(arg_count, args);
    fprintf(stderr, "[WRAPPER readText] returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value readCsv(int arg_count, Value* args) { return kuyil_file_read_csv(arg_count, args); }
Value readJson(int arg_count, Value* args) { return kuyil_file_read_json(arg_count, args); }
Value readYaml(int arg_count, Value* args) { return kuyil_file_read_yaml(arg_count, args); }
Value exists(int arg_count, Value* args) { 
    fprintf(stderr, "[WRAPPER exists] called with %d args\n", arg_count); fflush(stderr);
    Value result = kuyil_file_exists(arg_count, args);
    fprintf(stderr, "[WRAPPER exists] returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value size(int arg_count, Value* args) { 
    fprintf(stderr, "[WRAPPER size] called with %d args\n", arg_count); fflush(stderr);
    Value result = kuyil_file_size(arg_count, args);
    fprintf(stderr, "[WRAPPER size] returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value validate(int arg_count, Value* args) { return kuyil_file_validate(arg_count, args); }
Value writeText(int arg_count, Value* args) { return kuyil_file_write_text(arg_count, args); }
Value info(int arg_count, Value* args) { return kuyil_file_info(arg_count, args); }

// CamelCase aliases for interface compatibility
Value file_readText(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] *** file_readText wrapper at %p ***\n", (void*)file_readText); fflush(stderr);
    fprintf(stderr, "[fileio_wrapper] *** About to call kuyil_file_read_text at %p ***\n", (void*)kuyil_file_read_text); fflush(stderr);
    fprintf(stderr, "[fileio_wrapper] file_readText called with %d args\n", arg_count);
    if (arg_count > 0) {
        fprintf(stderr, "[fileio_wrapper]   arg[0] type=%d\n", args[0].type);
        if (args[0].type == VALUE_STRING) {
            fprintf(stderr, "[fileio_wrapper]   arg[0] string='%s'\n", args[0].as.string ? args[0].as.string : "(null)");
        }
    }
    Value result = kuyil_file_read_text(arg_count, args); 
    fprintf(stderr, "[fileio_wrapper] kuyil_file_read_text returned type=%d\n", result.type);
    if (result.type == VALUE_STRING) {
        fprintf(stderr, "[fileio_wrapper]   STRING value='%s'\n", result.as.string ? result.as.string : "(null)");
    }
    return result;
}
Value file_readCsv(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_readCsv called\n"); fflush(stderr);
    Value result = kuyil_file_read_csv(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_readCsv returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value file_readJson(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_readJson called\n"); fflush(stderr);
    Value result = kuyil_file_read_json(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_readJson returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value file_readYaml(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_readYaml called\n"); fflush(stderr);
    Value result = kuyil_file_read_yaml(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_readYaml returned type=%d\n", result.type); fflush(stderr);
    return result;
}
// kuyil_file_exists is the wrapper - no conflict with int file_exists() utility function
Value file_size(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_size called\n"); fflush(stderr);
    Value result = kuyil_file_size(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_size returned type=%d\n", result.type); fflush(stderr);
    if (result.type == VALUE_NUMBER) {
        fprintf(stderr, "[fileio_wrapper]   NUMBER value=%f\n", result.as.number); fflush(stderr);
    }
    return result;
}
Value file_validate(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_validate called\n"); fflush(stderr);
    Value result = kuyil_file_validate(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_validate returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value file_writeText(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_writeText called\n"); fflush(stderr);
    Value result = kuyil_file_write_text(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_writeText returned type=%d\n", result.type); fflush(stderr);
    return result;
}
Value file_info(int arg_count, Value* args) { 
    fprintf(stderr, "[fileio_wrapper] file_info called\n"); fflush(stderr);
    Value result = kuyil_file_info(arg_count, args);
    fprintf(stderr, "[fileio_wrapper] file_info returned type=%d\n", result.type); fflush(stderr);
    return result;
}

// Directory listing function - thread-safe, no popen()
#include <dirent.h>

Value kuyil_file_list_files(int arg_count, Value* args) {
    fprintf(stderr, "[DEBUG list_files] Function called with %d args\n", arg_count);
    fflush(stderr);
    
    // Handle namespace calling (argc=2) vs direct call (argc=1)
    Value path_val = (arg_count == 2) ? args[1] : args[0];
    Value pattern_val;
    if (arg_count == 3) {
        pattern_val = args[2];
    } else if (arg_count == 2 && args[1].type == VALUE_STRING) {
        pattern_val = args[1];
    } else {
        pattern_val.type = VALUE_NIL;
    }
    
    if (path_val.type != VALUE_STRING) {
        fprintf(stderr, "[DEBUG list_files] Error: path is not a string (type=%d)\n", path_val.type);
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    const char* dirpath = path_val.as.string;
    const char* pattern = (pattern_val.type == VALUE_STRING) ? pattern_val.as.string : "*.kyl";
    
    fprintf(stderr, "[DEBUG list_files] Listing directory: %s with pattern: %s\n", dirpath, pattern);
    fflush(stderr);
    
    // Open directory
    DIR* dir = opendir(dirpath);
    if (!dir) {
        fprintf(stderr, "[DEBUG list_files] Error: cannot open directory %s (errno=%d)\n", dirpath, errno);
        fflush(stderr);
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    fprintf(stderr, "[DEBUG list_files] Directory opened successfully\n");
    fflush(stderr);
    
    // Build result string with newline-separated file paths
    char* result = NULL;
    size_t result_len = 0;
    size_t result_cap = 1024;
    result = malloc(result_cap);
    if (!result) {
        closedir(dir);
        Value nil = {VALUE_NIL};
        return nil;
    }
    result[0] = '\0';
    
    int file_count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Check if it's a file (not directory)
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
            continue;
        }
        
        // Check if name matches pattern (.kyl files)
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".kyl") != 0) {
            continue;
        }
        
        // Build full path
        size_t path_len = strlen(dirpath) + strlen(entry->d_name) + 2;
        char* full_path = malloc(path_len);
        if (!full_path) continue;
        
        snprintf(full_path, path_len, "%s/%s", dirpath, entry->d_name);
        
        // Add to result
        size_t needed = result_len + strlen(full_path) + 2;
        if (needed > result_cap) {
            result_cap = needed * 2;
            char* new_result = realloc(result, result_cap);
            if (!new_result) {
                free(full_path);
                break;
            }
            result = new_result;
        }
        
        if (result_len > 0) {
            strcat(result, "\n");
            result_len++;
        }
        strcat(result, full_path);
        result_len += strlen(full_path);
        
        free(full_path);
        file_count++;
    }
    
    closedir(dir);
    
    fprintf(stderr, "[DEBUG list_files] Found %d files, result length: %zu\n", file_count, result_len);
    fflush(stderr);
    
    // Return result as string
    Value ret;
    ret.type = VALUE_STRING;
    ret.as.string = result;
    return ret;
}

// Wrapper for file.listFiles
Value file_listFiles(int arg_count, Value* args) { 
    return kuyil_file_list_files(arg_count, args);
}

// Snake-case no-underscore variants to satisfy interface binder fallback (e.g. file_readtext)
Value file_readtext(int arg_count, Value* args) { return kuyil_file_read_text(arg_count, args); }
Value file_readcsv(int arg_count, Value* args) { return kuyil_file_read_csv(arg_count, args); }
Value file_readjson(int arg_count, Value* args) { return kuyil_file_read_json(arg_count, args); }
Value file_readyaml(int arg_count, Value* args) { return kuyil_file_read_yaml(arg_count, args); }
Value file_listfiles(int arg_count, Value* args) { return kuyil_file_list_files(arg_count, args); }


// Library entry point for dynamic loading
__attribute__((constructor))
void fileio_constructor(void) {
    fileio_init();
}

__attribute__((destructor))
void fileio_destructor(void) {
    fileio_cleanup();
}