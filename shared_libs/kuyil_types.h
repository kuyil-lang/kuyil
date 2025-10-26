#ifndef KUYIL_TYPES_H
#define KUYIL_TYPES_H

#include <stdbool.h>

// Value types - must match the main VM's enum exactly
typedef enum {
    VALUE_NIL,
    VALUE_BOOL, 
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_ARRAY,
    VALUE_OBJECT,
    VALUE_FUNCTION
} ValueType;

// Forward declarations for complex types
typedef struct Function Function;
typedef struct NativeFunction NativeFunction;
typedef struct Closure Closure;
typedef struct Upvalue Upvalue;
typedef struct Class Class;
typedef struct Instance Instance;
typedef struct BoundMethod BoundMethod;

// Must match the main VM's structures exactly
typedef struct {
    int count;
    struct Value* values;
} ValueArray;

typedef struct {
    int count;
    char** keys;
    struct Value* values;
} ValueObject;

typedef struct {
    struct Function* function;  
    struct Value* captured_vars;  
    int capture_count;
} FunctionValue;

// Value struct - must match the main VM's struct exactly
typedef struct Value {
    ValueType type;
    union {
        bool boolean;
        double number;
        char* string;
        ValueArray array;
        ValueObject object;
        FunctionValue function;
    } as;
} Value;

// Function signature for shared library functions
typedef Value (*SharedLibFunction)(int arg_count, Value* args);

#endif // KUYIL_TYPES_H