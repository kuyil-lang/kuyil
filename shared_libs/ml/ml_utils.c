// ML Utilities: train_test_split, k_fold_split, shuffle_data

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ast.h"

// Fisher-Yates shuffle
static void shuffle_indices(int* indices, int n, unsigned int seed) {
    srand(seed);
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
}

// train_test_split(X, y, test_size, random_seed)
// Returns: {X_train, X_test, y_train, y_test}
Value kyl_ml_trainTestSplit(int argc, Value* args) {
    if (argc < 4 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value X = args[0];
    Value y = args[1];
    double test_size = (args[2].type == VALUE_NUMBER) ? args[2].as.number : 0.2;
    unsigned int seed = (unsigned int)((args[3].type == VALUE_NUMBER) ? args[3].as.number : time(NULL));
    
    int n_samples = X.as.array.count;
    int n_test = (int)(n_samples * test_size);
    int n_train = n_samples - n_test;
    
    // Create shuffled indices
    int* indices = (int*)malloc(n_samples * sizeof(int));
    for (int i = 0; i < n_samples; i++) indices[i] = i;
    shuffle_indices(indices, n_samples, seed);
    
    // Create X_train
    Value X_train = {VALUE_ARRAY};
    X_train.as.array = (Array*)malloc(sizeof(Array));
    X_train.as.array.values = (Value*)malloc(sizeof(Value) * n_train);
    X_train.as.array.count = n_train;
    X_train.as.array.count = n_train;
    for (int i = 0; i < n_train; i++) {
        X_train.as.array.values[i] = X.as.array.values[indices[i]];
    }
    
    // Create X_test
    Value X_test = {VALUE_ARRAY};
    X_test.as.array = (Array*)malloc(sizeof(Array));
    X_test.as.array.values = (Value*)malloc(sizeof(Value) * n_test);
    X_test.as.array.count = n_test;
    X_test.as.array.count = n_test;
    for (int i = 0; i < n_test; i++) {
        X_test.as.array.values[i] = X.as.array.values[indices[n_train + i]];
    }
    
    // Create y_train
    Value y_train = {VALUE_ARRAY};
    y_train.as.array = (Array*)malloc(sizeof(Array));
    y_train.as.array.values = (Value*)malloc(sizeof(Value) * n_train);
    y_train.as.array.count = n_train;
    y_train.as.array.count = n_train;
    for (int i = 0; i < n_train; i++) {
        y_train.as.array.values[i] = y.as.array.values[indices[i]];
    }
    
    // Create y_test
    Value y_test = {VALUE_ARRAY};
    y_test.as.array = (Array*)malloc(sizeof(Array));
    y_test.as.array.values = (Value*)malloc(sizeof(Value) * n_test);
    y_test.as.array.count = n_test;
    y_test.as.array.count = n_test;
    for (int i = 0; i < n_test; i++) {
        y_test.as.array.values[i] = y.as.array.values[indices[n_train + i]];
    }
    
    free(indices);
    
    // Return as object with keys
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    result.as.object = (Object*)malloc(sizeof(Object));
    result.as.object.count = 4;
    result.as.object.count = 4;
    result.as.object.keys = (char**)malloc(sizeof(char*) * 4);
    result.as.object.values = (Value*)malloc(sizeof(Value) * 4);
    
    result.as.object.keys[0] = strdup("X_train");
    result.as.object.values[0] = X_train;
    result.as.object.keys[1] = strdup("X_test");
    result.as.object.values[1] = X_test;
    result.as.object.keys[2] = strdup("y_train");
    result.as.object.values[2] = y_train;
    result.as.object.keys[3] = strdup("y_test");
    result.as.object.values[3] = y_test;
    
    return result;
}

// k_fold_split(X, y, n_splits)
// Returns: array of {train_indices, test_indices}
Value kyl_ml_kFoldSplit(int argc, Value* args) {
    if (argc < 3 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value X = args[0];
    Value y = args[1];
    int n_splits = (int)((args[2].type == VALUE_NUMBER) ? args[2].as.number : 5);
    
    int n_samples = X.as.array.count;
    int fold_size = n_samples / n_splits;
    
    // Create result array of fold objects
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_splits);
    result.as.array.count = n_splits;
    result.as.array.count = n_splits;
    
    for (int fold = 0; fold < n_splits; fold++) {
        int test_start = fold * fold_size;
        int test_end = (fold == n_splits - 1) ? n_samples : (fold + 1) * fold_size;
        int n_test = test_end - test_start;
        int n_train = n_samples - n_test;
        
        // Create train_indices
        Value train_indices = {VALUE_ARRAY};
        train_indices.as.array = (Array*)malloc(sizeof(Array));
        train_indices.as.array.values = (Value*)malloc(sizeof(Value) * n_train);
        train_indices.as.array.count = n_train;
        train_indices.as.array.count = n_train;
        
        int train_idx = 0;
        for (int i = 0; i < n_samples; i++) {
            if (i < test_start || i >= test_end) {
                train_indices.as.array.values[train_idx].type = VALUE_NUMBER;
                train_indices.as.array.values[train_idx].as.number = i;
                train_idx++;
            }
        }
        
        // Create test_indices
        Value test_indices = {VALUE_ARRAY};
        test_indices.as.array = (Array*)malloc(sizeof(Array));
        test_indices.as.array.values = (Value*)malloc(sizeof(Value) * n_test);
        test_indices.as.array.count = n_test;
        test_indices.as.array.count = n_test;
        
        for (int i = 0; i < n_test; i++) {
            test_indices.as.array.values[i].type = VALUE_NUMBER;
            test_indices.as.array.values[i].as.number = test_start + i;
        }
        
        // Create fold object
        Value fold_obj = {VALUE_OBJECT};
        fold_obj.as.object = (Object*)malloc(sizeof(Object));
        fold_obj.as.object.count = 2;
        fold_obj.as.object.count = 2;
        fold_obj.as.object.keys = (char**)malloc(sizeof(char*) * 2);
        fold_obj.as.object.values = (Value*)malloc(sizeof(Value) * 2);
        
        fold_obj.as.object.keys[0] = strdup("train_indices");
        fold_obj.as.object.values[0] = train_indices;
        fold_obj.as.object.keys[1] = strdup("test_indices");
        fold_obj.as.object.values[1] = test_indices;
        
        result.as.array.values[fold] = fold_obj;
    }
    
    return result;
}

// shuffle_data(X, y, random_seed)
// Returns: {X, y} shuffled
Value kyl_ml_shuffleData(int argc, Value* args) {
    if (argc < 3 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value X = args[0];
    Value y = args[1];
    unsigned int seed = (unsigned int)((args[2].type == VALUE_NUMBER) ? args[2].as.number : time(NULL));
    
    int n_samples = X.as.array.count;
    
    // Create shuffled indices
    int* indices = (int*)malloc(n_samples * sizeof(int));
    for (int i = 0; i < n_samples; i++) indices[i] = i;
    shuffle_indices(indices, n_samples, seed);
    
    // Create shuffled X
    Value X_shuffled = {VALUE_ARRAY};
    X_shuffled.as.array = (Array*)malloc(sizeof(Array));
    X_shuffled.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    X_shuffled.as.array.count = n_samples;
    X_shuffled.as.array.count = n_samples;
    for (int i = 0; i < n_samples; i++) {
        X_shuffled.as.array.values[i] = X.as.array.values[indices[i]];
    }
    
    // Create shuffled y
    Value y_shuffled = {VALUE_ARRAY};
    y_shuffled.as.array = (Array*)malloc(sizeof(Array));
    y_shuffled.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    y_shuffled.as.array.count = n_samples;
    y_shuffled.as.array.count = n_samples;
    for (int i = 0; i < n_samples; i++) {
        y_shuffled.as.array.values[i] = y.as.array.values[indices[i]];
    }
    
    free(indices);
    
    // Return as object
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_OBJECT;
    result.as.object = (Object*)malloc(sizeof(Object));
    result.as.object.count = 2;
    result.as.object.count = 2;
    result.as.object.keys = (char**)malloc(sizeof(char*) * 2);
    result.as.object.values = (Value*)malloc(sizeof(Value) * 2);
    
    result.as.object.keys[0] = strdup("X");
    result.as.object.values[0] = X_shuffled;
    result.as.object.keys[1] = strdup("y");
    result.as.object.values[1] = y_shuffled;
    
    return result;
}
