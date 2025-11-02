#define _GNU_SOURCE
#include "libkylstr.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// String length function
Value kyl_str_length(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
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

// String replace function
Value kyl_str_replace(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_STRING || 
        args[1].type != VALUE_STRING || args[2].type != VALUE_STRING) {
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
        return result;
    }
    
    const char* str = args[0].as.string;
    const char* delimiter = args[1].as.string;
    
    if (strlen(delimiter) == 0) {
        // Empty delimiter, return original string
        Value result;
        result.type = VALUE_STRING;
        result.as.string = strdup(str);
        return result;
    }
    
    // Count how many parts we'll have
    int count = 1;
    const char* temp = str;
    while ((temp = strstr(temp, delimiter)) != NULL) {
        count++;
        temp += strlen(delimiter);
    }
    
    // Build result as pipe-separated string
    char* result_str = malloc(strlen(str) + count * 10); // Extra space for metadata
    result_str[0] = '\0';
    
    char* str_copy = strdup(str);
    char* token = strtok(str_copy, delimiter);
    int first = 1;
    
    while (token != NULL) {
        if (!first) {
            strcat(result_str, "|");
        }
        strcat(result_str, token);
        first = 0;
        token = strtok(NULL, delimiter);
    }
    
    free(str_copy);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

// Number conversion function
Value kyl_str_to_number(int arg_count, Value* args) {
    if (arg_count != 1) {
        Value result = {VALUE_NIL};
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
        Value result = {VALUE_NIL};
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