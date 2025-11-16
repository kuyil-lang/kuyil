// Define _GNU_SOURCE to get strdup before any includes
#define _GNU_SOURCE

#include "heapfs.h"
#include <stdlib.h>
#include <string.h>

// Global heap filesystem instance
HeapFS g_heapfs = {NULL, 0, 0};

void heapfs_init(HeapFS* fs, int initial_capacity) {
    fs->capacity = initial_capacity > 0 ? initial_capacity : 16;
    fs->count = 0;
    fs->files = malloc(sizeof(HeapFSFile) * fs->capacity);
}

void heapfs_add_file(HeapFS* fs, const char* path, const char* content, size_t size, const char* mime_type) {
    if (fs->count >= fs->capacity) {
        fs->capacity *= 2;
        fs->files = realloc(fs->files, sizeof(HeapFSFile) * fs->capacity);
    }
    
    HeapFSFile* file = &fs->files[fs->count++];
    file->path = strdup(path);
    file->content = content;  // Points to embedded data, no copy needed
    file->size = size;
    file->mime_type = strdup(mime_type);
}

int heapfs_exists(HeapFS* fs, const char* path) {
    for (int i = 0; i < fs->count; i++) {
        if (strcmp(fs->files[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

Value heapfs_get_file_info(HeapFS* fs, const char* path) {
    Value result = {.type = VALUE_NIL};
    
    // Find file in heap filesystem
    HeapFSFile* file = NULL;
    for (int i = 0; i < fs->count; i++) {
        if (strcmp(fs->files[i].path, path) == 0) {
            file = &fs->files[i];
            break;
        }
    }
    
    if (!file) {
        return result;
    }
    
    // Convert content to byte array
    Value* byte_values = malloc(file->size * sizeof(Value));
    for (size_t i = 0; i < file->size; i++) {
        byte_values[i].type = VALUE_NUMBER;
        byte_values[i].as.number = (double)(unsigned char)file->content[i];
    }
    
    // Create result object with 7 fields (same as file_info)
    result.type = VALUE_OBJECT;
    result.as.object.count = 7;
    result.as.object.keys = malloc(7 * sizeof(char*));
    result.as.object.values = malloc(7 * sizeof(Value));
    
    // path field
    result.as.object.keys[0] = strdup("path");
    result.as.object.values[0].type = VALUE_STRING;
    result.as.object.values[0].as.string = strdup(file->path);
    
    // size field
    result.as.object.keys[1] = strdup("size");
    result.as.object.values[1].type = VALUE_NUMBER;
    result.as.object.values[1].as.number = (double)file->size;
    
    // contentType field
    result.as.object.keys[2] = strdup("contentType");
    result.as.object.values[2].type = VALUE_STRING;
    result.as.object.values[2].as.string = strdup(file->mime_type);
    
    // modifiedTime field (0 for embedded files)
    result.as.object.keys[3] = strdup("modifiedTime");
    result.as.object.values[3].type = VALUE_NUMBER;
    result.as.object.values[3].as.number = 0;
    
    // readable field (always true for embedded files)
    result.as.object.keys[4] = strdup("readable");
    result.as.object.values[4].type = VALUE_BOOL;
    result.as.object.values[4].as.boolean = 1;
    
    // writable field (always false for embedded files)
    result.as.object.keys[5] = strdup("writable");
    result.as.object.values[5].type = VALUE_BOOL;
    result.as.object.values[5].as.boolean = 0;
    
    // data field (byte array)
    result.as.object.keys[6] = strdup("data");
    result.as.object.values[6].type = VALUE_ARRAY;
    result.as.object.values[6].as.array.count = file->size;
    result.as.object.values[6].as.array.values = byte_values;
    
    return result;
}

void heapfs_cleanup(HeapFS* fs) {
    for (int i = 0; i < fs->count; i++) {
        free((char*)fs->files[i].path);
        free((char*)fs->files[i].mime_type);
        // Note: content is not freed as it points to embedded data
    }
    free(fs->files);
    fs->files = NULL;
    fs->count = 0;
    fs->capacity = 0;
}

// Kuyil VM interface functions

Value kuyil_heapfs_info(int arg_count, Value* args) {
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        Value result = {.type = VALUE_NIL};
        return result;
    }
    
    return heapfs_get_file_info(&g_heapfs, args[0].as.string);
}

Value kuyil_heapfs_exists(int arg_count, Value* args) {
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = 0;
    
    if (arg_count != 1 || args[0].type != VALUE_STRING) {
        return result;
    }
    
    result.as.boolean = heapfs_exists(&g_heapfs, args[0].as.string);
    return result;
}

Value kuyil_heapfs_list(int arg_count, Value* args) {
    (void)arg_count;
    (void)args;
    
    Value result;
    result.type = VALUE_ARRAY;
    result.as.array.count = g_heapfs.count;
    result.as.array.values = malloc(g_heapfs.count * sizeof(Value));
    
    for (int i = 0; i < g_heapfs.count; i++) {
        result.as.array.values[i].type = VALUE_STRING;
        result.as.array.values[i].as.string = strdup(g_heapfs.files[i].path);
    }
    
    return result;
}

// C-level accessor functions for webview URI scheme handlers
// These provide direct access without Value wrapping

HeapFSFile* heapfs_get_c_file_info(const char* path) {
    for (int i = 0; i < g_heapfs.count; i++) {
        if (strcmp(g_heapfs.files[i].path, path) == 0) {
            return &g_heapfs.files[i];
        }
    }
    return NULL;
}

size_t heapfs_c_get_file_size(HeapFSFile* file) {
    return file ? file->size : 0;
}

const char* heapfs_c_get_file_data(HeapFSFile* file) {
    return file ? file->content : NULL;
}

const char* heapfs_c_get_file_mime(HeapFSFile* file) {
    return file ? file->mime_type : "application/octet-stream";
}

