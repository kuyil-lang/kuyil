#ifndef KUYIL_FILE_READER_H
#define KUYIL_FILE_READER_H

#include <stdio.h>
#include "vm.h"

// File reader types
typedef enum {
    FILE_TYPE_TEXT,
    FILE_TYPE_CSV,
    FILE_TYPE_JSON,
    FILE_TYPE_YAML
} FileType;

// CSV configuration
typedef struct {
    char delimiter;
    bool has_header;
    bool skip_empty_lines;
} CSVConfig;

// File reader context
typedef struct {
    FILE* file;
    FileType type;
    CSVConfig csv_config;
    char* current_line;
    size_t line_buffer_size;
    int line_number;
    bool eof_reached;
} FileReader;

// File reader functions
FileReader* file_reader_create(const char* filepath, FileType type);
void file_reader_destroy(FileReader* reader);
bool file_reader_has_next(FileReader* reader);
Value* file_reader_read_line(FileReader* reader);
Value* file_reader_read_csv_row(FileReader* reader);
Value* file_reader_read_json_object(FileReader* reader);
Value* file_reader_read_yaml_object(FileReader* reader);
Value* file_reader_read_all(FileReader* reader);

// Utility functions
FileType file_reader_detect_type(const char* filepath);
Value* file_reader_parse_csv_line(const char* line, const CSVConfig* config);
Value* file_reader_parse_json_line(const char* line);
Value* file_reader_parse_yaml_line(const char* line);

// Native functions for VM integration
Value kuyil_file_read_text(int arg_count, Value* args);
Value kuyil_file_read_csv(int arg_count, Value* args);
Value kuyil_file_read_json(int arg_count, Value* args);
Value kuyil_file_read_yaml(int arg_count, Value* args);

#endif // KUYIL_FILE_READER_H