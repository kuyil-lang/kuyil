// Concurrent HashMap - Full Implementation with Lock Striping
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../../src/ast.h"

typedef struct HashNode {
    char* key;
    Value value;
    struct HashNode* next;
} HashNode;

typedef struct {
    HashNode** buckets;
    size_t capacity;
    size_t size;
    pthread_mutex_t* locks;
    int lock_count;
} ConcurrentHashMap;

static unsigned int hash(const char* key, size_t capacity) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % capacity;
}

static int get_lock_index(ConcurrentHashMap* map, const char* key) {
    return hash(key, map->lock_count);
}

Value kyl_ds_concurrentHashMapCreate(int arg_count, Value* args) {
    int capacity = (arg_count > 0 && args[0].type == VALUE_NUMBER) ? (int)args[0].as.number : 16;
    if (capacity < 4) capacity = 4;
    
    ConcurrentHashMap* map = malloc(sizeof(ConcurrentHashMap));
    map->capacity = capacity;
    map->size = 0;
    map->buckets = calloc(capacity, sizeof(HashNode*));
    map->lock_count = 16;
    map->locks = malloc(sizeof(pthread_mutex_t) * map->lock_count);
    
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_init(&map->locks[i], NULL);
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)map;
    return result;
}

Value kyl_ds_concurrentHashMapDestroy(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    
    // Free all nodes
    for (size_t i = 0; i < map->capacity; i++) {
        HashNode* node = map->buckets[i];
        while (node) {
            HashNode* next = node->next;
            free(node->key);
            free(node);
            node = next;
        }
    }
    
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_destroy(&map->locks[i]);
    }
    
    free(map->locks);
    free(map->buckets);
    free(map);
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_concurrentHashMapPut(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    Value value = args[2];
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    // Check if key exists
    HashNode* node = map->buckets[bucket_idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->value = value;
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        node = node->next;
    }
    
    // Insert new node
    HashNode* new_node = malloc(sizeof(HashNode));
    new_node->key = strdup(key);
    new_node->value = value;
    new_node->next = map->buckets[bucket_idx];
    map->buckets[bucket_idx] = new_node;
    map->size++;
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_concurrentHashMapPutIfAbsent(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    Value value = args[2];
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    // Check if key exists
    HashNode* node = map->buckets[bucket_idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return (Value){VALUE_BOOL, .as.boolean = false}; // Key exists
        }
        node = node->next;
    }
    
    // Insert new node
    HashNode* new_node = malloc(sizeof(HashNode));
    new_node->key = strdup(key);
    new_node->value = value;
    new_node->next = map->buckets[bucket_idx];
    map->buckets[bucket_idx] = new_node;
    map->size++;
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_concurrentHashMapGet(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_NIL};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    HashNode* node = map->buckets[bucket_idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            Value result = node->value;
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return result;
        }
        node = node->next;
    }
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_NIL};
}

Value kyl_ds_concurrentHashMapRemove(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    HashNode* node = map->buckets[bucket_idx];
    HashNode* prev = NULL;
    
    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                map->buckets[bucket_idx] = node->next;
            }
            free(node->key);
            free(node);
            map->size--;
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        prev = node;
        node = node->next;
    }
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_concurrentHashMapReplace(int arg_count, Value* args) {
    if (arg_count < 4 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    Value new_value = args[3];
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    HashNode* node = map->buckets[bucket_idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->value = new_value;
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        node = node->next;
    }
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_concurrentHashMapContains(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    const char* key = args[1].as.string;
    
    unsigned int bucket_idx = hash(key, map->capacity);
    int lock_idx = get_lock_index(map, key);
    
    pthread_mutex_lock(&map->locks[lock_idx]);
    
    HashNode* node = map->buckets[bucket_idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            pthread_mutex_unlock(&map->locks[lock_idx]);
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        node = node->next;
    }
    
    pthread_mutex_unlock(&map->locks[lock_idx]);
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_concurrentHashMapSize(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)map->size;
    return result;
}

Value kyl_ds_concurrentHashMapIsEmpty(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = true};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (map->size == 0);
    return result;
}

Value kyl_ds_concurrentHashMapClear(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    
    // Lock all locks
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_lock(&map->locks[i]);
    }
    
    // Free all nodes
    for (size_t i = 0; i < map->capacity; i++) {
        HashNode* node = map->buckets[i];
        while (node) {
            HashNode* next = node->next;
            free(node->key);
            free(node);
            node = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
    
    // Unlock all locks
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_unlock(&map->locks[i]);
    }
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_concurrentHashMapKeys(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    ConcurrentHashMap* map = (ConcurrentHashMap*)(uintptr_t)args[0].as.number;
    
    // Lock all locks for snapshot
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_lock(&map->locks[i]);
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = map->size;
    result.as.array.values = malloc(sizeof(Value) * map->size);
    
    size_t idx = 0;
    for (size_t i = 0; i < map->capacity; i++) {
        HashNode* node = map->buckets[i];
        while (node) {
            Value key_val = {VALUE_STRING};
            key_val.as.string = strdup(node->key);
            result.as.array.values[idx++] = key_val;
            node = node->next;
        }
    }
    
    // Unlock all locks
    for (int i = 0; i < map->lock_count; i++) {
        pthread_mutex_unlock(&map->locks[i]);
    }
    
    return result;
}
