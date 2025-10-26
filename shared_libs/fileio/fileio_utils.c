// GNU extension for strdup - must be defined before includes
#define _GNU_SOURCE

#include "fileio_utils.h"

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
    return access(filepath, F_OK) == 0;
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
    Value result = {VALUE_NIL, .as = {.number = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    FileResult* file_result = file_read_text(args[0].as.string);
    if (!file_result) {
        return result;
    }
    
    if (file_result->error_code == FILE_SUCCESS && file_result->content) {
        result.type = VALUE_STRING;
        result.as.string = strdup(file_result->content);
    }
    
    file_result_free(file_result);
    return result;
}

Value kuyil_file_read_csv(int arg_count, Value* args) {
    Value result = {VALUE_NIL, .as = {.number = 0}};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    CSVResult* csv_result = file_read_csv(args[0].as.string);
    if (!csv_result) {
        return result;
    }
    
    // For now, return a simple string representation
    // In a full implementation, this would return a structured object
    if (csv_result->error_code == FILE_SUCCESS) {
        char info[1024];
        snprintf(info, sizeof(info), "CSV: %d headers, %d rows", 
                csv_result->header_count, csv_result->row_count);
        result.type = VALUE_STRING;
        result.as.string = strdup(info);
    }
    
    csv_result_free(csv_result);
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
        return result;
    }
    
    result.as.boolean = file_exists(args[0].as.string);
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

// Library entry point for dynamic loading
__attribute__((constructor))
void fileio_constructor(void) {
    fileio_init();
}

__attribute__((destructor))
void fileio_destructor(void) {
    fileio_cleanup();
}