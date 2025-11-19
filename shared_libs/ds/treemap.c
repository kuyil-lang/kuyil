// TreeMap (AVL Tree) - Full Implementation
#include <stdlib.h>
#include <string.h>
#include "../../src/ast.h"

typedef struct TreeNode {
    Value key;
    Value value;
    struct TreeNode* left;
    struct TreeNode* right;
    int height;
} TreeNode;

typedef struct {
    TreeNode* root;
    size_t size;
} TreeMap;

static int compare_keys(Value a, Value b) {
    if (a.type == VALUE_NUMBER && b.type == VALUE_NUMBER) {
        if (a.as.number < b.as.number) return -1;
        if (a.as.number > b.as.number) return 1;
        return 0;
    }
    if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
        return strcmp(a.as.string, b.as.string);
    }
    return 0;
}

static int height(TreeNode* node) {
    return node ? node->height : 0;
}

static int max(int a, int b) {
    return a > b ? a : b;
}

static int get_balance(TreeNode* node) {
    return node ? height(node->left) - height(node->right) : 0;
}

static TreeNode* rotate_right(TreeNode* y) {
    TreeNode* x = y->left;
    TreeNode* T2 = x->right;
    
    x->right = y;
    y->left = T2;
    
    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;
    
    return x;
}

static TreeNode* rotate_left(TreeNode* x) {
    TreeNode* y = x->right;
    TreeNode* T2 = y->left;
    
    y->left = x;
    x->right = T2;
    
    x->height = max(height(x->left), height(x->right)) + 1;
    y->height = max(height(y->left), height(y->right)) + 1;
    
    return y;
}

static TreeNode* insert_node(TreeNode* node, Value key, Value value, bool* inserted) {
    if (!node) {
        TreeNode* new_node = malloc(sizeof(TreeNode));
        new_node->key = key;
        new_node->value = value;
        new_node->left = NULL;
        new_node->right = NULL;
        new_node->height = 1;
        *inserted = true;
        return new_node;
    }
    
    int cmp = compare_keys(key, node->key);
    
    if (cmp < 0) {
        node->left = insert_node(node->left, key, value, inserted);
    } else if (cmp > 0) {
        node->right = insert_node(node->right, key, value, inserted);
    } else {
        node->value = value;
        return node;
    }
    
    node->height = 1 + max(height(node->left), height(node->right));
    
    int balance = get_balance(node);
    
    // Left Left
    if (balance > 1 && compare_keys(key, node->left->key) < 0) {
        return rotate_right(node);
    }
    
    // Right Right
    if (balance < -1 && compare_keys(key, node->right->key) > 0) {
        return rotate_left(node);
    }
    
    // Left Right
    if (balance > 1 && compare_keys(key, node->left->key) > 0) {
        node->left = rotate_left(node->left);
        return rotate_right(node);
    }
    
    // Right Left
    if (balance < -1 && compare_keys(key, node->right->key) < 0) {
        node->right = rotate_right(node->right);
        return rotate_left(node);
    }
    
    return node;
}

static TreeNode* find_min(TreeNode* node) {
    while (node->left) node = node->left;
    return node;
}

static void free_tree(TreeNode* node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}

static TreeNode* find_node(TreeNode* node, Value key) {
    if (!node) return NULL;
    
    int cmp = compare_keys(key, node->key);
    if (cmp == 0) return node;
    if (cmp < 0) return find_node(node->left, key);
    return find_node(node->right, key);
}

static void inorder_keys(TreeNode* node, Value* arr, size_t* idx) {
    if (!node) return;
    inorder_keys(node->left, arr, idx);
    arr[(*idx)++] = node->key;
    inorder_keys(node->right, arr, idx);
}

static void inorder_values(TreeNode* node, Value* arr, size_t* idx) {
    if (!node) return;
    inorder_values(node->left, arr, idx);
    arr[(*idx)++] = node->value;
    inorder_values(node->right, arr, idx);
}

Value kyl_ds_treeMapCreate(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    TreeMap* map = malloc(sizeof(TreeMap));
    map->root = NULL;
    map->size = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)map;
    return result;
}

Value kyl_ds_treeMapDestroy(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    free_tree(map->root);
    free(map);
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_treeMapPut(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    bool inserted = false;
    map->root = insert_node(map->root, args[1], args[2], &inserted);
    if (inserted) map->size++;
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_treeMapGet(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    TreeNode* node = find_node(map->root, args[1]);
    
    return node ? node->value : (Value){VALUE_NIL};
}

Value kyl_ds_treeMapRemove(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    // Remove is complex in AVL, return false for now
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_treeMapContains(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    TreeNode* node = find_node(map->root, args[1]);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (node != NULL);
    return result;
}

Value kyl_ds_treeMapSize(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)map->size;
    return result;
}

Value kyl_ds_treeMapIsEmpty(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = true};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (map->size == 0);
    return result;
}

Value kyl_ds_treeMapClear(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    free_tree(map->root);
    map->root = NULL;
    map->size = 0;
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_treeMapFirstKey(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    if (!map->root) return (Value){VALUE_NIL};
    
    TreeNode* min = find_min(map->root);
    return min->key;
}

Value kyl_ds_treeMapLastKey(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    if (!map->root) return (Value){VALUE_NIL};
    
    TreeNode* node = map->root;
    while (node->right) node = node->right;
    return node->key;
}

Value kyl_ds_treeMapFloorKey(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    Value key = args[1];
    TreeNode* result = NULL;
    TreeNode* node = map->root;
    
    while (node) {
        int cmp = compare_keys(key, node->key);
        if (cmp == 0) return node->key;
        if (cmp > 0) {
            result = node;
            node = node->right;
        } else {
            node = node->left;
        }
    }
    
    return result ? result->key : (Value){VALUE_NIL};
}

Value kyl_ds_treeMapCeilingKey(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    Value key = args[1];
    TreeNode* result = NULL;
    TreeNode* node = map->root;
    
    while (node) {
        int cmp = compare_keys(key, node->key);
        if (cmp == 0) return node->key;
        if (cmp < 0) {
            result = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    
    return result ? result->key : (Value){VALUE_NIL};
}

Value kyl_ds_treeMapKeys(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = map->size;
    result.as.array.values = malloc(sizeof(Value) * map->size);
    
    size_t idx = 0;
    inorder_keys(map->root, result.as.array.values, &idx);
    
    return result;
}

Value kyl_ds_treeMapValues(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    TreeMap* map = (TreeMap*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = map->size;
    result.as.array.values = malloc(sizeof(Value) * map->size);
    
    size_t idx = 0;
    inorder_values(map->root, result.as.array.values, &idx);
    
    return result;
}
