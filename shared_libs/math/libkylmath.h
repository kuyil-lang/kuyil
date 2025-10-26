#ifndef LIBKYLMATH_H
#define LIBKYLMATH_H

#include "../kuyil_types.h"

// Math library functions
Value kyl_math_abs(int arg_count, Value* args);
Value kyl_math_floor(int arg_count, Value* args);
Value kyl_math_ceil(int arg_count, Value* args);
Value kyl_math_round(int arg_count, Value* args);
Value kyl_math_sqrt(int arg_count, Value* args);
Value kyl_math_pow(int arg_count, Value* args);
Value kyl_math_sin(int arg_count, Value* args);
Value kyl_math_cos(int arg_count, Value* args);
Value kyl_math_tan(int arg_count, Value* args);

#endif