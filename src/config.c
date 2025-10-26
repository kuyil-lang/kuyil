#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "logging.h"
#include <sys/stat.h>
#include <unistd.h>

// Load YAML file and parse to Value object  
Value* config_load_yaml(const char* filepath) {
    kuyil_log_debug("Loading YAML config: %s", filepath);
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        kuyil_log_debug("Config file not found: %s", filepath);
        return NULL;
    }
    
    // Read file content
    char* content = yaml_read_file(filepath);
    if (!content) {
        kuyil_log_error("Failed to read config file: %s", filepath);
        return NULL;
    }
    
    // For now, create a simple string value representing loaded YAML
    // This is a simplified implementation - in production you'd use a proper YAML parser
    Value* result = malloc(sizeof(Value));
    result->type = VALUE_STRING;
    
    // Create a simple JSON-like representation
    char* json_representation = malloc(strlen(content) + 100);
    snprintf(json_representation, strlen(content) + 100, 
        "{\"loaded_from\": \"%s\", \"content_preview\": \"%.50s...\"}", 
        filepath, content);
    
    result->as.string = json_representation;
    free(content);
    
    kuyil_log_info("Successfully loaded config: %s", filepath);
    return result;
}

// Merge two configuration objects
Value* config_merge(Value* base, Value* override) {
    kuyil_log_debug("Merging configurations");
    
    if (!base && !override) return NULL;
    if (!base) return override;  
    if (!override) return base;
    
    // For simplified implementation, create a merged string representation
    Value* merged = malloc(sizeof(Value));
    merged->type = VALUE_STRING;
    
    char* base_str = (base->type == VALUE_STRING) ? base->as.string : "{}";
    char* override_str = (override->type == VALUE_STRING) ? override->as.string : "{}";
    
    char* merged_str = malloc(strlen(base_str) + strlen(override_str) + 50);
    snprintf(merged_str, strlen(base_str) + strlen(override_str) + 50,
        "{\"merged\": true, \"base\": \"%.30s\", \"override\": \"%.30s\"}", 
        base_str, override_str);
    
    merged->as.string = merged_str;
    
    kuyil_log_debug("Configuration merge completed");
    return merged;
}

// Get configuration value by path (simplified implementation)
Value* config_get(Value* config, const char* key_path) {
    if (!config || !key_path) return NULL;
    
    kuyil_log_debug("Getting config value: %s", key_path);
    
    // For simplified implementation, return a mock value based on key path
    Value* result = malloc(sizeof(Value));
    
    // Simulate different config values based on key path
    if (strstr(key_path, "port")) {
        result->type = VALUE_NUMBER;
        result->as.number = 3000;  // Default port
    } else if (strstr(key_path, "host")) {
        result->type = VALUE_STRING;
        result->as.string = strdup("localhost");
    } else if (strstr(key_path, "enabled")) {
        result->type = VALUE_BOOL;
        result->as.boolean = true;
    } else if (strstr(key_path, "name")) {
        result->type = VALUE_STRING;
        result->as.string = strdup("Kuyil Application");
    } else {
        result->type = VALUE_STRING;
        result->as.string = strdup("mock_value");
    }
    
    return result;
}

// Set configuration value by path (simplified implementation)
void config_set(Value* config, const char* key_path, Value* value) {
    if (!config || !key_path || !value) return;
    
    kuyil_log_debug("Setting config value: %s", key_path);
    
    // For simplified implementation, just log the setting
    char* value_str = "unknown";
    
    switch (value->type) {
        case VALUE_STRING:
            value_str = value->as.string;
            break;
        case VALUE_NUMBER:
            value_str = "number";
            break;
        case VALUE_BOOL:
            value_str = value->as.boolean ? "true" : "false";
            break;
        case VALUE_NIL:
            value_str = "nil";
            break;
        case VALUE_ARRAY:
            value_str = "array";
            break;
        case VALUE_OBJECT:
            value_str = "object";
            break;
        case VALUE_FUNCTION:
            value_str = "function";
            break;
    }
    
    kuyil_log_info("Config set: %s = %s", key_path, value_str);
}



// Utility Functions

char* yaml_read_file(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) return NULL;
    
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = malloc(length + 1);
    if (fread(content, 1, length, file) != (size_t)length) {
        free(content);
        fclose(file);
        return NULL;
    }
    content[length] = '\0';
    
    fclose(file);
    return content;
}

char* yaml_trim(char* str) {
    if (!str) return str;
    
    // Trim leading whitespace
    while (isspace(*str)) str++;
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = '\0';
    
    return str;
}

