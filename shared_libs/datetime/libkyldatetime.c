#include "libkyldatetime.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Export interface signatures for auto-binding (lowerCamel)
__attribute__((visibility("default")))
const char* kyl_interface_signature_text =
    "datetime now() -> string\n"
    "datetime current() -> string\n"
    "date now() -> string\n"
    "date unix() -> int64\n"
    "date iso(date: string) -> string\n";

// DateTime library functions
Value kyl_date_now(int arg_count, Value* args) {
    (void)arg_count; (void)args; // Suppress unused parameter warnings
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    char* date_str = malloc(11); // YYYY-MM-DD + null terminator
    strftime(date_str, 11, "%Y-%m-%d", tm_info);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = date_str;
    return result;
}

Value kyl_date_current(int arg_count, Value* args) {
    // Alias for date_now
    return kyl_date_now(arg_count, args);
}

Value kyl_datetime_now(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    char* datetime_str = malloc(20); // YYYY-MM-DD HH:MM:SS + null terminator
    strftime(datetime_str, 20, "%Y-%m-%d %H:%M:%S", tm_info);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = datetime_str;
    return result;
}

Value kyl_datetime_current(int arg_count, Value* args) {
    // Alias for datetime_now
    return kyl_datetime_now(arg_count, args);
}

Value kyl_date_add(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_STRING || args[1].type != VALUE_NUMBER || args[2].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* date_str = args[0].as.string;
    int amount = (int)args[1].as.number;
    const char* unit = args[2].as.string;
    
    // Parse input date (YYYY-MM-DD format)
    struct tm tm_info = {0};
    if (sscanf(date_str, "%d-%d-%d", &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday) != 3) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    tm_info.tm_year -= 1900; // tm_year is years since 1900
    tm_info.tm_mon -= 1;     // tm_mon is 0-based
    
    // Add based on unit
    if (strcmp(unit, "days") == 0 || strcmp(unit, "day") == 0) {
        tm_info.tm_mday += amount;
    } else if (strcmp(unit, "months") == 0 || strcmp(unit, "month") == 0) {
        tm_info.tm_mon += amount;
    } else if (strcmp(unit, "years") == 0 || strcmp(unit, "year") == 0) {
        tm_info.tm_year += amount;
    } else {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    // Normalize the date
    mktime(&tm_info);
    
    char* result_str = malloc(11);
    strftime(result_str, 11, "%Y-%m-%d", &tm_info);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = result_str;
    return result;
}

Value kyl_date_sub(int arg_count, Value* args) {
    if (arg_count != 3 || args[0].type != VALUE_STRING || args[1].type != VALUE_NUMBER || args[2].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    // Create negative amount and call date_add
    Value negative_args[3];
    negative_args[0] = args[0];
    negative_args[1].type = VALUE_NUMBER;
    negative_args[1].as.number = -args[1].as.number;
    negative_args[2] = args[2];
    
    return kyl_date_add(3, negative_args);
}

Value kyl_date_diff(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* date1_str = args[0].as.string;
    const char* date2_str = args[1].as.string;
    
    // Parse both dates
    struct tm tm1 = {0}, tm2 = {0};
    if (sscanf(date1_str, "%d-%d-%d", &tm1.tm_year, &tm1.tm_mon, &tm1.tm_mday) != 3 ||
        sscanf(date2_str, "%d-%d-%d", &tm2.tm_year, &tm2.tm_mon, &tm2.tm_mday) != 3) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    tm1.tm_year -= 1900; tm1.tm_mon -= 1;
    tm2.tm_year -= 1900; tm2.tm_mon -= 1;
    
    time_t time1 = mktime(&tm1);
    time_t time2 = mktime(&tm2);
    
    // Return difference in days
    double diff_seconds = difftime(time2, time1);
    double diff_days = diff_seconds / (24 * 60 * 60);
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = diff_days;
    return result;
}

Value kyl_date_unix(int arg_count, Value* args) {
    if (arg_count == 0) {
        // Return current unix timestamp
        Value result;
        result.type = VALUE_NUMBER;
        result.as.number = (double)time(NULL);
        return result;
    }
    
    if (arg_count == 1 && args[0].type == VALUE_STRING) {
        // Convert date string to unix timestamp
        const char* date_str = args[0].as.string;
        struct tm tm_info = {0};
        
        // Try different date formats
        if (sscanf(date_str, "%d-%d-%d %d:%d:%d", 
                   &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
                   &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) == 6) {
            // DateTime format
            tm_info.tm_year -= 1900; tm_info.tm_mon -= 1;
        } else if (sscanf(date_str, "%d-%d-%d", &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday) == 3) {
            // Date only format
            tm_info.tm_year -= 1900; tm_info.tm_mon -= 1;
        } else {
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
            return result;
        }
        
        time_t timestamp = mktime(&tm_info);
        
        Value result;
        result.type = VALUE_NUMBER;
        result.as.number = (double)timestamp;
        return result;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
    return result;
}

Value kyl_date_from_unix(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    time_t timestamp = (time_t)args[0].as.number;
    struct tm* tm_info = localtime(&timestamp);
    
    char* datetime_str = malloc(20);
    strftime(datetime_str, 20, "%Y-%m-%d %H:%M:%S", tm_info);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = datetime_str;
    return result;
}

Value kyl_date_iso(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    struct tm* tm_info = gmtime(&tv.tv_sec);
    
    char* iso_str = malloc(30); // ISO format with milliseconds
    snprintf(iso_str, 30, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = iso_str;
    return result;
}

Value kyl_date_format(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_STRING || args[1].type != VALUE_STRING) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    const char* date_str = args[0].as.string;
    const char* format_str = args[1].as.string;
    
    struct tm tm_info = {0};
    
    // Parse input date
    if (sscanf(date_str, "%d-%d-%d %d:%d:%d", 
               &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
               &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) == 6) {
        tm_info.tm_year -= 1900; tm_info.tm_mon -= 1;
    } else if (sscanf(date_str, "%d-%d-%d", &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday) == 3) {
        tm_info.tm_year -= 1900; tm_info.tm_mon -= 1;
    } else {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    char* formatted_str = malloc(100); // Generous buffer for formatted output
    strftime(formatted_str, 100, format_str, &tm_info);
    
    Value result;
    result.type = VALUE_STRING;
    result.as.string = formatted_str;
    return result;
}