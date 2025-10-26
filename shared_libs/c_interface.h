// Example: Pure C interface approach
#ifndef KUYIL_C_INTERFACE_H
#define KUYIL_C_INTERFACE_H

// Opaque handle to a Kuyil value
typedef void* KuyilValue;

// C-friendly function signatures
typedef struct {
    int type;           // 0=nil, 1=bool, 2=number, 3=string
    union {
        int boolean;
        double number;
        char* string;
    } value;
} KuyilCValue;

// Conversion functions
KuyilValue kuyil_create_number(double num);
KuyilValue kuyil_create_string(const char* str);
double kuyil_get_number(KuyilValue val);
char* kuyil_get_string(KuyilValue val);

// Library function signature
typedef KuyilValue (*KuyilCFunction)(int argc, KuyilValue* argv);

#endif