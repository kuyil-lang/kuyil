#ifndef LIBKYLSTR_H
#define LIBKYLSTR_H

#include "../kuyil_types.h"

// String library functions
Value kyl_str_length(int arg_count, Value* args);
Value kyl_str_substring(int arg_count, Value* args);
Value kyl_str_upper(int arg_count, Value* args);
Value kyl_str_lower(int arg_count, Value* args);
Value kyl_str_trim(int arg_count, Value* args);
Value kyl_str_contains(int arg_count, Value* args);
Value kyl_str_replace(int arg_count, Value* args);
Value kyl_str_split(int arg_count, Value* args);
Value kyl_str_to_number(int arg_count, Value* args);
Value kyl_str_to_string(int arg_count, Value* args);

#endif