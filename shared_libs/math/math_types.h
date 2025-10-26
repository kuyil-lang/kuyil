// Example: For complete isolation, each library could have its own types
// This approach eliminates even the shared kuyil_types.h dependency

#ifndef LIBKYLMATH_TYPES_H
#define LIBKYLMATH_TYPES_H

#include <stdbool.h>

// Local type definitions - must match VM exactly
typedef enum {
    VALUE_BOOL,
    VALUE_NIL, 
    VALUE_NUMBER,
    VALUE_STRING,
    // ... other types as needed
} ValueType;

typedef union {
    bool boolean;
    double number;
    char* string;
    // ... other fields as needed
} ValueUnion;

typedef struct {
    ValueType type;
    ValueUnion as;
} Value;

#endif