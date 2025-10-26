#ifndef LIBKYLDATETIME_H
#define LIBKYLDATETIME_H

#include "../kuyil_types.h"

// DateTime library functions
Value kyl_date_now(int arg_count, Value* args);
Value kyl_date_current(int arg_count, Value* args);
Value kyl_datetime_now(int arg_count, Value* args);
Value kyl_datetime_current(int arg_count, Value* args);
Value kyl_date_add(int arg_count, Value* args);
Value kyl_date_sub(int arg_count, Value* args);
Value kyl_date_diff(int arg_count, Value* args);
Value kyl_date_unix(int arg_count, Value* args);
Value kyl_date_from_unix(int arg_count, Value* args);
Value kyl_date_iso(int arg_count, Value* args);
Value kyl_date_format(int arg_count, Value* args);

#endif