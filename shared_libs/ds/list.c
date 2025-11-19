// Dynamic List - Full Implementation
#include <stdlib.h>
#include <string.h>
#include "../../src/ast.h"

typedef struct {
    Value* values;
    size_t size;
    size_t capacity;
} List;

static void resize(List* list) {
    list->capacity *= 2;
    list->values = realloc(list->values, sizeof(Value) * list->capacity);
}

static bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VALUE_NUMBER: return a.as.number == b.as.number;
        case VALUE_BOOL: return a.as.boolean == b.as.boolean;
        case VALUE_STRING: return a.as.string && b.as.string && strcmp(a.as.string, b.as.string) == 0;
        case VALUE_NIL: return true;
        default: return false;
    }
}

static int compare_values(const void* a, const void* b) {
    Value* va = (Value*)a;
    Value* vb = (Value*)b;
    
    if (va->type == VALUE_NUMBER && vb->type == VALUE_NUMBER) {
        if (va->as.number < vb->as.number) return -1;
        if (va->as.number > vb->as.number) return 1;
        return 0;
    }
    return 0;
}

Value kyl_ds_listCreate(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    List* list = malloc(sizeof(List));
    list->capacity = 16;
    list->size = 0;
    list->values = malloc(sizeof(Value) * list->capacity);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)list;
    return result;
}

Value kyl_ds_listDestroy(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    List* list = (List*)(uintptr_t)args[0].as.number;
    free(list->values);
    free(list);
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listAdd(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    if (list->size >= list->capacity) {
        resize(list);
    }
    
    list->values[list->size++] = args[1];
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listInsert(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    int index = (int)args[1].as.number;
    
    if (index < 0 || index > (int)list->size) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    if (list->size >= list->capacity) {
        resize(list);
    }
    
    // Shift elements right
    for (size_t i = list->size; i > (size_t)index; i--) {
        list->values[i] = list->values[i - 1];
    }
    
    list->values[index] = args[2];
    list->size++;
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listRemove(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    Value value = args[1];
    
    for (size_t i = 0; i < list->size; i++) {
        if (value_equals(list->values[i], value)) {
            // Shift elements left
            for (size_t j = i; j < list->size - 1; j++) {
                list->values[j] = list->values[j + 1];
            }
            list->size--;
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
    }
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_listRemoveAt(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    int index = (int)args[1].as.number;
    
    if (index < 0 || index >= (int)list->size) {
        return (Value){VALUE_NIL};
    }
    
    Value result = list->values[index];
    
    // Shift elements left
    for (size_t i = index; i < list->size - 1; i++) {
        list->values[i] = list->values[i + 1];
    }
    list->size--;
    
    return result;
}

Value kyl_ds_listGet(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_NIL};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    int index = (int)args[1].as.number;
    
    if (index < 0 || index >= (int)list->size) {
        return (Value){VALUE_NIL};
    }
    
    return list->values[index];
}

Value kyl_ds_listSet(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    int index = (int)args[1].as.number;
    
    if (index < 0 || index >= (int)list->size) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    list->values[index] = args[2];
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listSize(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)list->size;
    return result;
}

Value kyl_ds_listIsEmpty(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = true};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = (list->size == 0);
    return result;
}

Value kyl_ds_listClear(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    list->size = 0;
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listContains(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    Value value = args[1];
    
    for (size_t i = 0; i < list->size; i++) {
        if (value_equals(list->values[i], value)) {
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
    }
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_listIndexOf(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = -1};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    Value value = args[1];
    
    for (size_t i = 0; i < list->size; i++) {
        if (value_equals(list->values[i], value)) {
            Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
            result.as.number = (double)i;
            return result;
        }
    }
    
    return (Value){VALUE_NUMBER, .as.number = -1};
}

Value kyl_ds_listSort(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    if (list->size > 0) {
        qsort(list->values, list->size, sizeof(Value), compare_values);
    }
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listReverse(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    size_t left = 0;
    size_t right = list->size - 1;
    
    while (left < right) {
        Value temp = list->values[left];
        list->values[left] = list->values[right];
        list->values[right] = temp;
        left++;
        right--;
    }
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_listToArray(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    List* list = (List*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = list->size;
    result.as.array.values = malloc(sizeof(Value) * list->size);
    
    for (size_t i = 0; i < list->size; i++) {
        result.as.array.values[i] = list->values[i];
    }
    
    return result;
}
