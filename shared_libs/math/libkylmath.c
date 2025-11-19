#include "libkylmath.h"
#include <math.h>
#include <stddef.h>

// Export interface signatures for auto-binding (lowerCamel)
__attribute__((visibility("default")))
const char* kyl_interface_signature_text =
    "math abs(x: float64) -> float64\n"
    "math floor(x: float64) -> float64\n"
    "math ceil(x: float64) -> float64\n"
    "math round(x: float64) -> float64\n"
    "math sqrt(x: float64) -> float64\n"
    "math pow(x: float64, y: float64) -> float64\n"
    "math sin(x: float64) -> float64\n"
    "math cos(x: float64) -> float64\n"
    "math tan(x: float64) -> float64\n";

// Math library functions
Value kyl_math_abs(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = fabs(args[0].as.number);
    return result;
}

Value kyl_math_floor(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = floor(args[0].as.number);
    return result;
}

Value kyl_math_ceil(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = ceil(args[0].as.number);
    return result;
}

Value kyl_math_round(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = round(args[0].as.number);
    return result;
}

// Additional math functions
Value kyl_math_sqrt(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = sqrt(args[0].as.number);
    return result;
}

Value kyl_math_pow(int arg_count, Value* args) {
    if (arg_count != 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = pow(args[0].as.number, args[1].as.number);
    return result;
}

Value kyl_math_sin(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = sin(args[0].as.number);
    return result;
}

Value kyl_math_cos(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = cos(args[0].as.number);
    return result;
}

Value kyl_math_tan(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_NUMBER) {
        Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NIL;
        return result;
    }
    
    Value result;
    result.type = VALUE_NUMBER;
    result.as.number = tan(args[0].as.number);
    return result;
}