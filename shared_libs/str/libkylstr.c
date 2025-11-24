#define _GNU_SOURCE
#include "libkylstr.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <regex.h>

// Export interface signatures for auto-binding (lowerCamel)
__attribute__((visibility("default")))
const char* kyl_interface_signature_text =
    "str length(input: string) -> int32\n"
    "str substring(input: string, start: int32, end: int32) -> string\n"
    "str upper(input: string) -> string\n"
    "str lower(input: string) -> string\n"
    "str trim(input: string) -> string\n"
    "str startsWith(input: string, prefix: string) -> bool\n"
    "str endsWith(input: string, suffix: string) -> bool\n"
    "str contains(haystack: string, needle: string) -> bool\n"
    "str indexOf(haystack: string, needle: string) -> int32\n"
    "str replace(input: string, from: string, to: string) -> string\n"
    "str split(input: string, delimiter: string) -> array\n"
    "str toNumber(input: string) -> float64\n"
    "str toString(value: number|bool|string|nil) -> string\n"
    "str slice(input: string, start: int32, end: int32) -> string\n"
    "str regexMatch(input: string, pattern: string) -> bool\n"
    "str regexExtract(input: string, pattern: string) -> string\n"
    "str regexReplace(input: string, pattern: string, replacement: string) -> string\n"
    "str bytesToString(bytes: array) -> string\n";

// String length function
Value kyl_str_length(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (double)strlen(args[0].as.string);
    return result;
}

// String substring function
Value kyl_str_substring(int arg_count, Value* args) {
    if (arg_count < 2 || arg_count > 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    int len = strlen(str);
    int start = (int)args[1].as.number;
    int end = (arg_count == 3 && args[2].type == VALUE_NUMBER) ? (int)args[2].as.number : len;
    
    // Bounds checking
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        Value result;
        result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    // Extract substring
    int sub_len = end - start;
    char* substring = malloc(sub_len + 1);
    memcpy(substring, str + start, sub_len);
    substring[sub_len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = substring;
    return result;
}

// String upper case function
Value kyl_str_upper(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    int len = strlen(str);
    char* upper_str = malloc(len + 1);
    
    for (int i = 0; i < len; i++) {
        upper_str[i] = toupper(str[i]);
    }
    upper_str[len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = upper_str;
    return result;
}

// String lower case function
Value kyl_str_lower(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    int len = strlen(str);
    char* lower_str = malloc(len + 1);
    
    for (int i = 0; i < len; i++) {
        lower_str[i] = tolower(str[i]);
    }
    lower_str[len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = lower_str;
    return result;
}

// String trim function
Value kyl_str_trim(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    
    // Find start (skip leading whitespace)
    while (*str && isspace(*str)) str++;
    
    // Find end (skip trailing whitespace)
    const char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    
    // Create trimmed string
    int len = end - str + 1;
    char* trimmed = malloc(len + 1);
    memcpy(trimmed, str, len);
    trimmed[len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = trimmed;
    return result;
}

// String startsWith function
Value kyl_str_starts_with(int arg_count, Value* args) {
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = false;
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        return result;
    }
    const char* input = args[0].as.string;
    const char* prefix = args[1].as.string;
    size_t in_len = strlen(input);
    size_t p_len = strlen(prefix);
    if (p_len > in_len) {
        return result;
    }
    result.as.boolean = (strncmp(input, prefix, p_len) == 0);
    return result;
}

// String endsWith function
Value kyl_str_ends_with(int arg_count, Value* args) {
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = false;
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        return result;
    }
    const char* input = args[0].as.string;
    const char* suffix = args[1].as.string;
    size_t in_len = strlen(input);
    size_t s_len = strlen(suffix);
    if (s_len > in_len) {
        return result;
    }
    result.as.boolean = (strncmp(input + (in_len - s_len), suffix, s_len) == 0);
    return result;
}

// String contains function
Value kyl_str_contains(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
        result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = strstr(args[0].as.string, args[1].as.string) != NULL;
    return result;
}

// String indexOf function - returns first index of needle in haystack, or -1 if not found
Value kyl_str_indexOf(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
        result.type = VALUE_NUMBER;
        result.as.number = -1;
        return result;
    }
    
    const char* haystack = args[0].as.string;
    const char* needle = args[1].as.string;
    const char* pos = strstr(haystack, needle);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = (pos != NULL) ? (double)(pos - haystack) : -1.0;
    return result;
}

// String replace function
Value kyl_str_replace(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    const char* old_sub = args[1].as.string;
    const char* new_sub = args[2].as.string;
    
    char* pos = strstr(str, old_sub);
    if (pos == NULL) {
        // No replacement needed, return copy of original
        Value result;
        result.type = VALUE_STRING;
        result.as.string = strdup(str);
        return result;
    }
    
    int old_len = strlen(old_sub);
    int new_len = strlen(new_sub);
    int prefix_len = pos - str;
    int suffix_len = strlen(pos + old_len);
    
    char* result_str = malloc(prefix_len + new_len + suffix_len + 1);
    memcpy(result_str, str, prefix_len);
    memcpy(result_str + prefix_len, new_sub, new_len);
    memcpy(result_str + prefix_len + new_len, pos + old_len, suffix_len);
    result_str[prefix_len + new_len + suffix_len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

// String split function - returns array as string with | separator
Value kyl_str_split(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    const char* delimiter = args[1].as.string;
    
    if (strlen(delimiter) == 0) {
        // Empty delimiter, return array with original string
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
        result.as.array.values = malloc(sizeof(Value));
        result.as.array.count = 1;
        result.as.array.values[0].type = VALUE_STRING;
        result.as.array.values[0].as.string = strdup(str);
        return result;
    }
    
    // Count how many parts we'll have
    int count = 1;
    const char* temp = str;
    while ((temp = strstr(temp, delimiter)) != NULL) {
        count++;
        temp += strlen(delimiter);
    }
    
    // Create array
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.values = malloc(sizeof(Value) * count);
    result.as.array.count = 0;
    
    // Split and populate array
    char* str_copy = strdup(str);
    char* token = strtok(str_copy, delimiter);
    
    while (token != NULL) {
        Value part = {VALUE_STRING};
        part.as.string = strdup(token);
        result.as.array.values[result.as.array.count++] = part;
        token = strtok(NULL, delimiter);
    }
    
    free(str_copy);
    return result;
}

// Number conversion function
Value kyl_str_to_number(int arg_count, Value* args) {
    if (arg_count != 1) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    
    switch (args[0].type) {
        case VALUE_NUMBER:
            result.as.number = args[0].as.number;
            break;
        case VALUE_STRING: {
            char* endptr;
            double num = strtod(args[0].as.string, &endptr);
            if (*endptr == '\0') {
                result.as.number = num;
            } else {
                result.type = VALUE_NIL; // Invalid number format
            }
            break;
        }
        case VALUE_BOOL:
            result.as.number = args[0].as.boolean ? 1.0 : 0.0;
            break;
        default:
            result.type = VALUE_NIL;
            break;
    }
    
    return result;
}

// String conversion function
Value kyl_str_to_string(int arg_count, Value* args) {
    if (arg_count != 1) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_STRING;
    
    switch (args[0].type) {
        case VALUE_STRING:
            result.as.string = strdup(args[0].as.string);
            break;
        case VALUE_NUMBER: {
            char* str = malloc(64);  // Increased size for large integers
            // Check if number is an integer (no decimal part)
            if (args[0].as.number == (long long)args[0].as.number) {
                // Integer: use fixed format to avoid scientific notation
                snprintf(str, 64, "%.0f", args[0].as.number);
            } else {
                // Float: use %g but ensure no scientific notation for reasonable numbers
                snprintf(str, 64, "%.15g", args[0].as.number);
            }
            result.as.string = str;
            break;
        }
        case VALUE_BOOL:
            result.as.string = strdup(args[0].as.boolean ? "true" : "false");
            break;
        case VALUE_NIL:
            result.as.string = strdup("nil");
            break;
        default:
            result.as.string = strdup("[Object]");
            break;
    }
    
    return result;
}
// String slice function (Python-style slicing with negative indices)
Value kyl_str_slice(int arg_count, Value* args) {
    if (arg_count < 2 || arg_count > 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* str = args[0].as.string;
    int len = strlen(str);
    int start = (int)args[1].as.number;
    int end = (arg_count == 3 && args[2].type == VALUE_NUMBER) ? (int)args[2].as.number : len;
    
    // Handle negative indices (Python-style)
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    // Bounds checking
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        Value result;
        result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    // Create substring
    int sub_len = end - start;
    char* result_str = (char*)malloc(sub_len + 1);
    strncpy(result_str, str + start, sub_len);
    result_str[sub_len] = '\0';
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

// Regex match function (returns true if pattern matches)
Value kyl_str_regexMatch(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
        result.as.boolean = false;
        return result;
    }
    
    const char* input = args[0].as.string;
    const char* pattern = args[1].as.string;
    
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    
    Value result;
    result.type = VALUE_BOOL;
    
    if (ret != 0) {
        // Regex compilation failed
        result.as.boolean = false;
        return result;
    }
    
    ret = regexec(&regex, input, 0, NULL, 0);
    result.as.boolean = (ret == 0);
    
    regfree(&regex);
    return result;
}

// Regex extract function (returns first match or empty string)
Value kyl_str_regexExtract(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    const char* input = args[0].as.string;
    const char* pattern = args[1].as.string;
    
    regex_t regex;
    regmatch_t match[2];  // Support for one capture group
    
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    
    if (ret != 0) {
        // Regex compilation failed
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
        result.as.string = strdup("");
        return result;
    }
    
    ret = regexec(&regex, input, 2, match, 0);
    
    Value result;
    result.type = VALUE_STRING;
    
    if (ret == 0) {
        // Match found - extract first capture group if exists, otherwise full match
        int idx = (match[1].rm_so != -1) ? 1 : 0;
        int start = match[idx].rm_so;
        int end = match[idx].rm_eo;
        int len = end - start;
        
        char* extracted = (char*)malloc(len + 1);
        strncpy(extracted, input + start, len);
        extracted[len] = '\0';
        result.as.string = extracted;
    } else {
        // No match
        result.as.string = strdup("");
    }
    
    regfree(&regex);
    return result;
}

// Regex replace function (replaces all matches)
Value kyl_str_regexReplace(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_STRING;
        result.as.string = (arg_count > 0 && args[0].type == VALUE_STRING) ? strdup(args[0].as.string) : strdup("");
        return result;
    }
    
    const char* input = args[0].as.string;
    const char* pattern = args[1].as.string;
    const char* replacement = args[2].as.string;
    
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    
    if (ret != 0) {
        // Regex compilation failed - return original string
        Value result;
        memset(&result, 0, sizeof(Value));
        result.type = VALUE_STRING;
        result.as.string = strdup(input);
        return result;
    }
    
    // Build result string by finding and replacing all matches
    char* result_str = malloc(1);
    result_str[0] = '\0';
    int result_len = 0;
    
    const char* pos = input;
    regmatch_t match;
    
    while (regexec(&regex, pos, 1, &match, 0) == 0) {
        // Add text before match
        int before_len = match.rm_so;
        result_str = realloc(result_str, result_len + before_len + 1);
        strncat(result_str, pos, before_len);
        result_len += before_len;
        
        // Add replacement text
        int repl_len = strlen(replacement);
        result_str = realloc(result_str, result_len + repl_len + 1);
        strcat(result_str, replacement);
        result_len += repl_len;
        
        // Move past this match
        pos += match.rm_eo;
        
        // Prevent infinite loop on zero-length matches
        if (match.rm_so == match.rm_eo) {
            if (*pos != '\0') {
                result_str = realloc(result_str, result_len + 2);
                result_str[result_len] = *pos;
                result_str[result_len + 1] = '\0';
                result_len++;
                pos++;
            } else {
                break;
            }
        }
    }
    
    // Add remaining text
    int remaining_len = strlen(pos);
    result_str = realloc(result_str, result_len + remaining_len + 1);
    strcat(result_str, pos);
    
    regfree(&regex);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

// Convert byte array to string
Value kyl_str_bytesToString(int arg_count, Value* args) {
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    
    if (arg_count != 1 || args[0].type != VALUE_ARRAY) {
        return result;
    }
    
    ValueArray arr = args[0].as.array;
    
    // Allocate string buffer
    char* str = malloc(arr.count + 1);
    if (!str) {
        return result;
    }
    
    // Convert each byte (number) to char
    for (int i = 0; i < arr.count; i++) {
        if (arr.values[i].type != VALUE_NUMBER) {
            free(str);
            return result;
        }
        int byte_val = (int)arr.values[i].as.number;
        // Clamp to valid byte range
        if (byte_val < 0) byte_val = 0;
        if (byte_val > 255) byte_val = 255;
        str[i] = (char)byte_val;
    }
    str[arr.count] = '\0';
    
    result.type = VALUE_STRING;
    result.as.string = str;
    return result;
}
