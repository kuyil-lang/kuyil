#define _GNU_SOURCE
#include "file_reader.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Create a new file reader
FileReader* file_reader_create(const char* filepath, FileType type) {
    kuyil_log_debug("Creating file reader for: %s", filepath);
    
    FileReader* reader = malloc(sizeof(FileReader));
    if (!reader) {
        kuyil_log_error("Failed to allocate memory for FileReader");
        return NULL;
    }
    
    reader->file = fopen(filepath, "r");
    if (!reader->file) {
        kuyil_log_error("Failed to open file: %s", filepath);
        free(reader);
        return NULL;
    }
    
    reader->type = type;
    reader->line_buffer_size = 1024;
    reader->current_line = malloc(reader->line_buffer_size);
    reader->line_number = 0;
    reader->eof_reached = false;
    
    // Set default CSV configuration
    reader->csv_config.delimiter = ',';
    reader->csv_config.has_header = true;
    reader->csv_config.skip_empty_lines = true;
    
    if (!reader->current_line) {
        kuyil_log_error("Failed to allocate line buffer");
        fclose(reader->file);
        free(reader);
        return NULL;
    }
    
    kuyil_log_info("File reader created successfully for %s", filepath);
    return reader;
}

// Destroy file reader
void file_reader_destroy(FileReader* reader) {
    if (!reader) return;
    
    if (reader->file) {
        fclose(reader->file);
    }
    
    if (reader->current_line) {
        free(reader->current_line);
    }
    
    free(reader);
    kuyil_log_debug("File reader destroyed");
}

// Check if there are more lines to read
bool file_reader_has_next(FileReader* reader) {
    if (!reader || reader->eof_reached) {
        return false;
    }
    
    return !feof(reader->file);
}

// Read a single line (text file)
Value* file_reader_read_line(FileReader* reader) {
    if (!reader || reader->eof_reached) {
        return NULL;
    }
    
    if (!fgets(reader->current_line, reader->line_buffer_size, reader->file)) {
        reader->eof_reached = true;
        return NULL;
    }
    
    reader->line_number++;
    
    // Remove trailing newline
    size_t len = strlen(reader->current_line);
    if (len > 0 && reader->current_line[len - 1] == '\n') {
        reader->current_line[len - 1] = '\0';
    }
    
    Value* result = malloc(sizeof(Value));
    result->type = VALUE_STRING;
    result->as.string = strdup(reader->current_line);
    
    kuyil_log_debug("Read line %d: %s", reader->line_number, reader->current_line);
    return result;
}

// Parse CSV line into array of values
Value* file_reader_parse_csv_line(const char* line, const CSVConfig* config) {
    if (!line || !config) return NULL;
    
    Value* row = malloc(sizeof(Value));
    row->type = VALUE_ARRAY;
    row->as.array.count = 0;
    row->as.array.values = NULL;
    
    char* line_copy = strdup(line);
    char* token = strtok(line_copy, &config->delimiter);
    
    while (token) {
        // Trim whitespace
        while (isspace(*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) *end-- = '\0';
        
        // Add to array
        row->as.array.count++;
        row->as.array.values = realloc(row->as.array.values, 
                                      row->as.array.count * sizeof(Value));
        
        Value* cell = &row->as.array.values[row->as.array.count - 1];
        cell->type = VALUE_STRING;
        cell->as.string = strdup(token);
        
        token = strtok(NULL, &config->delimiter);
    }
    
    free(line_copy);
    return row;
}

// Read CSV row
Value* file_reader_read_csv_row(FileReader* reader) {
    if (!reader || reader->type != FILE_TYPE_CSV) {
        return NULL;
    }
    
    Value* line = file_reader_read_line(reader);
    if (!line) return NULL;
    
    Value* row = file_reader_parse_csv_line(line->as.string, &reader->csv_config);
    
    // Free the line value
    free(line->as.string);
    free(line);
    
    return row;
}

// Simple JSON object parser (basic implementation)
Value* file_reader_parse_json_line(const char* line) {
    if (!line) return NULL;
    
    // This is a simplified JSON parser
    // For production, you would use a proper JSON library like cJSON
    
    Value* obj = malloc(sizeof(Value));
    obj->type = VALUE_OBJECT;
    obj->as.object.count = 0;
    obj->as.object.keys = NULL;
    obj->as.object.values = NULL;
    
    // Skip whitespace and check for opening brace
    const char* ptr = line;
    while (isspace(*ptr)) ptr++;
    
    if (*ptr != '{') {
        // Not a JSON object, treat as string
        obj->type = VALUE_STRING;
        obj->as.string = strdup(line);
        return obj;
    }
    
    kuyil_log_debug("Parsing JSON line: %s", line);
    
    // For now, return the line as a string
    // TODO: Implement proper JSON parsing
    obj->type = VALUE_STRING;
    obj->as.string = strdup(line);
    
    return obj;
}

// Read JSON object
Value* file_reader_read_json_object(FileReader* reader) {
    if (!reader || reader->type != FILE_TYPE_JSON) {
        return NULL;
    }
    
    Value* line = file_reader_read_line(reader);
    if (!line) return NULL;
    
    Value* obj = file_reader_parse_json_line(line->as.string);
    
    // Free the line value
    free(line->as.string);
    free(line);
    
    return obj;
}

// Simple YAML parser (basic implementation)
Value* file_reader_parse_yaml_line(const char* line) {
    if (!line) return NULL;
    
    // This is a simplified YAML parser
    // For production, you would use a proper YAML library like libyaml
    
    Value* obj = malloc(sizeof(Value));
    
    // Check if it's a key-value pair
    char* colon = strchr(line, ':');
    if (colon) {
        obj->type = VALUE_OBJECT;
        obj->as.object.count = 1;
        obj->as.object.keys = malloc(sizeof(char*));
        obj->as.object.values = malloc(sizeof(Value));
        
        // Extract key
        *colon = '\0';
        char* key = strdup(line);
        // Trim key
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && isspace(*key_end)) *key_end-- = '\0';
        
        // Extract value
        char* value = colon + 1;
        while (isspace(*value)) value++;
        
        obj->as.object.keys[0] = key;
        obj->as.object.values[0].type = VALUE_STRING;
        obj->as.object.values[0].as.string = strdup(value);
        
        kuyil_log_debug("Parsed YAML key-value: %s = %s", key, value);
    } else {
        // Treat as string
        obj->type = VALUE_STRING;
        obj->as.string = strdup(line);
    }
    
    return obj;
}

// Read YAML object
Value* file_reader_read_yaml_object(FileReader* reader) {
    if (!reader || reader->type != FILE_TYPE_YAML) {
        return NULL;
    }
    
    Value* line = file_reader_read_line(reader);
    if (!line) return NULL;
    
    Value* obj = file_reader_parse_yaml_line(line->as.string);
    
    // Free the line value
    free(line->as.string);
    free(line);
    
    return obj;
}

// Read all content into an array
Value* file_reader_read_all(FileReader* reader) {
    if (!reader) return NULL;
    
    Value* result = malloc(sizeof(Value));
    result->type = VALUE_ARRAY;
    result->as.array.count = 0;
    result->as.array.values = NULL;
    
    Value* item;
    while (file_reader_has_next(reader)) {
        switch (reader->type) {
            case FILE_TYPE_TEXT:
                item = file_reader_read_line(reader);
                break;
            case FILE_TYPE_CSV:
                item = file_reader_read_csv_row(reader);
                break;
            case FILE_TYPE_JSON:
                item = file_reader_read_json_object(reader);
                break;
            case FILE_TYPE_YAML:
                item = file_reader_read_yaml_object(reader);
                break;
            default:
                item = file_reader_read_line(reader);
                break;
        }
        
        if (item) {
            result->as.array.count++;
            result->as.array.values = realloc(result->as.array.values,
                                             result->as.array.count * sizeof(Value));
            result->as.array.values[result->as.array.count - 1] = *item;
            free(item); // Free the container, not the content
        }
    }
    
    kuyil_log_info("Read %d items from file", result->as.array.count);
    return result;
}

// Detect file type from extension
FileType file_reader_detect_type(const char* filepath) {
    if (!filepath) return FILE_TYPE_TEXT;
    
    const char* ext = strrchr(filepath, '.');
    if (!ext) return FILE_TYPE_TEXT;
    
    if (strcmp(ext, ".csv") == 0) return FILE_TYPE_CSV;
    if (strcmp(ext, ".json") == 0) return FILE_TYPE_JSON;
    if (strcmp(ext, ".yml") == 0 || strcmp(ext, ".yaml") == 0) return FILE_TYPE_YAML;
    
    return FILE_TYPE_TEXT;
}

// Native function: read lines from text file
Value kuyil_file_read_text(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("file_read_text requires 1 string argument (filepath)");
        return nil_result;
    }
    
    const char* filepath = args[0].as.string;
    FileReader* reader = file_reader_create(filepath, FILE_TYPE_TEXT);
    
    if (!reader) {
        kuyil_log_error("Failed to create file reader for: %s", filepath);
        return nil_result;
    }
    
    Value* result_ptr = file_reader_read_all(reader);
    Value result = result_ptr ? *result_ptr : nil_result;
    file_reader_destroy(reader);
    
    return result;
}

// Native function: read CSV file
Value kuyil_file_read_csv(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("file_read_csv requires at least 1 string argument (filepath)");
        return nil_result;
    }
    
    const char* filepath = args[0].as.string;
    FileReader* reader = file_reader_create(filepath, FILE_TYPE_CSV);
    
    if (!reader) {
        kuyil_log_error("Failed to create CSV reader for: %s", filepath);
        return nil_result;
    }
    
    // Optional delimiter parameter
    if (arg_count > 1 && args[1].type == VALUE_STRING && strlen(args[1].as.string) > 0) {
        reader->csv_config.delimiter = args[1].as.string[0];
    }
    
    Value* result_ptr = file_reader_read_all(reader);
    Value result = result_ptr ? *result_ptr : nil_result;
    file_reader_destroy(reader);
    
    return result;
}

// Native function: read JSON file
Value kuyil_file_read_json(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("file_read_json requires 1 string argument (filepath)");
        return nil_result;
    }
    
    const char* filepath = args[0].as.string;
    FileReader* reader = file_reader_create(filepath, FILE_TYPE_JSON);
    
    if (!reader) {
        kuyil_log_error("Failed to create JSON reader for: %s", filepath);
        return nil_result;
    }
    
    Value* result_ptr = file_reader_read_all(reader);
    Value result = result_ptr ? *result_ptr : nil_result;
    file_reader_destroy(reader);
    
    return result;
}

// Native function: read YAML file
Value kuyil_file_read_yaml(int arg_count, Value* args) {
    Value nil_result = {VALUE_NIL};
    
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        kuyil_log_error("file_read_yaml requires 1 string argument (filepath)");
        return nil_result;
    }
    
    const char* filepath = args[0].as.string;
    FileReader* reader = file_reader_create(filepath, FILE_TYPE_YAML);
    
    if (!reader) {
        kuyil_log_error("Failed to create YAML reader for: %s", filepath);
        return nil_result;
    }
    
    Value* result_ptr = file_reader_read_all(reader);
    Value result = result_ptr ? *result_ptr : nil_result;
    file_reader_destroy(reader);
    
    return result;
}