#ifndef HEAPFS_H
#define HEAPFS_H

#include "ast.h"  // Use VM's Value type, not shared library's
#include <stddef.h>

// HeapFS - In-Memory Virtual Filesystem
// Allows embedding files directly in the binary for single-file distribution

typedef struct {
    const char* path;
    const char* content;
    size_t size;
    const char* mime_type;
} HeapFSFile;

typedef struct {
    HeapFSFile* files;
    int count;
    int capacity;
} HeapFS;

// Initialize the heap filesystem
void heapfs_init(HeapFS* fs, int initial_capacity);

// Add a file to the heap filesystem
void heapfs_add_file(HeapFS* fs, const char* path, const char* content, size_t size, const char* mime_type);

// Get file info from heap filesystem (returns same structure as file_info)
Value heapfs_get_file_info(HeapFS* fs, const char* path);

// Check if file exists in heap filesystem
int heapfs_exists(HeapFS* fs, const char* path);

// Cleanup heap filesystem
void heapfs_cleanup(HeapFS* fs);

// Kuyil VM interface functions
Value kuyil_heapfs_info(int arg_count, Value* args);
Value kuyil_heapfs_exists(int arg_count, Value* args);
Value kuyil_heapfs_list(int arg_count, Value* args);

// Global heap filesystem instance
extern HeapFS g_heapfs;

// C-level accessor functions for webview URI scheme handlers
HeapFSFile* heapfs_get_c_file_info(const char* path);
size_t heapfs_c_get_file_size(HeapFSFile* file);
const char* heapfs_c_get_file_data(HeapFSFile* file);
const char* heapfs_c_get_file_mime(HeapFSFile* file);

// Helper macro to embed files at compile time
#define HEAPFS_EMBED_FILE(fs, path, content, mime) \
    heapfs_add_file(fs, path, content, sizeof(content) - 1, mime)

#endif // HEAPFS_H
