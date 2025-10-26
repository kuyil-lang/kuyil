#ifndef CONFIG_H
#define CONFIG_H

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// Configuration manager functions
Value* config_load_yaml(const char* filepath);
Value* config_merge(Value* base, Value* override);
Value* config_get(Value* config, const char* key_path);
void config_set(Value* config, const char* key_path, Value* value);

// Utility functions
char* yaml_read_file(const char* filepath);
char* yaml_trim(char* str);

#endif // CONFIG_H