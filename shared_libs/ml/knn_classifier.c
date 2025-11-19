// KNN Classifier - Uses existing ds_knnFind with majority voting

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ast.h"

// External KNN search function from data structures library
extern Value kyl_ds_knnFind(int argc, Value* args);

typedef struct {
    int n_neighbors;
    char* metric;  // "euclidean", "manhattan", "cosine"
    Value X_train;  // Stored training data
    Value y_train;  // Stored training labels
    int n_samples;
    int n_features;
} KNNClassifierModel;

Value kyl_ml_knnClassifierCreate(int argc, Value* args) {
    if (argc < 2) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_neighbors = (int)((args[0].type == VALUE_NUMBER) ? args[0].as.number : 5);
    const char* metric = (args[1].type == VALUE_STRING) ? args[1].as.string : "euclidean";
    
    KNNClassifierModel* model = (KNNClassifierModel*)malloc(sizeof(KNNClassifierModel));
    model->n_neighbors = n_neighbors;
    model->metric = strdup(metric);
    model->X_train.type = VALUE_NIL;
    model->y_train.type = VALUE_NIL;
    model->n_samples = 0;
    model->n_features = 0;
    
    Value v = {VALUE_NUMBER};
    v.as.number = (double)(uintptr_t)model;
    return v;
}

Value kyl_ml_knnClassifierFit(int argc, Value* args) {
    if (argc < 3 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KNNClassifierModel* model = (KNNClassifierModel*)(uintptr_t)args[0].as.number;
    
    // Just store references (KNN is lazy - no training needed)
    model->X_train = args[1];
    model->y_train = args[2];
    
    if (args[1].type == VALUE_ARRAY && args[1].as.array.count > 0) {
        model->n_samples = args[1].as.array.count;
        if (args[1].as.array.values[0].type == VALUE_ARRAY) {
            model->n_features = args[1].as.array.values[0].as.array.count;
        }
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ml_knnClassifierPredict(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KNNClassifierModel* model = (KNNClassifierModel*)(uintptr_t)args[0].as.number;
    Value X_test = args[1];
    
    if (X_test.type != VALUE_ARRAY || model->X_train.type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_test = X_test.as.array.count;
    
    // Create result array
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_test);
    result.as.array.count = n_test;
    result.as.array.count = n_test;
    
    // For each test point, find k nearest neighbors and do majority vote
    for (int i = 0; i < n_test; i++) {
        Value query = X_test.as.array.values[i];
        
        // Call ds_knnFind(dataset, query, k, metric)
        Value knn_args[4];
        knn_args[0] = model->X_train;
        knn_args[1] = query;
        knn_args[2].type = VALUE_NUMBER;
        knn_args[2].as.number = model->n_neighbors;
        knn_args[3].type = VALUE_STRING;
        knn_args[3].as.string = model->metric;
        
        Value neighbors = kyl_ds_knnFind(4, knn_args);
        
        if (neighbors.type != VALUE_ARRAY) {
            result.as.array.values[i].type = VALUE_NUMBER;
            result.as.array.values[i].as.number = 0.0;
            continue;
        }
        
        // Majority vote: count class occurrences
        // neighbors is array of {index, distance} objects
        int* class_counts = (int*)calloc(100, sizeof(int));  // Assume max 100 classes
        int max_class = 0;
        
        for (int j = 0; j < neighbors.as.array.count; j++) {
            Value neighbor = neighbors.as.array.values[j];
            if (neighbor.type == VALUE_OBJECT) {
                // Get index from neighbor object
                Value* idx_val = NULL;
                for (int k = 0; k < neighbor.as.object.count; k++) {
                    if (strcmp(neighbor.as.object.keys[k], "index") == 0) {
                        idx_val = &neighbor.as.object.values[k];
                        break;
                    }
                }
                
                if (idx_val && idx_val->type == VALUE_NUMBER) {
                    int idx = (int)idx_val->as.number;
                    if (idx >= 0 && idx < model->y_train.as.array.count) {
                        Value label = model->y_train.as.array.values[idx];
                        if (label.type == VALUE_NUMBER) {
                            int class_id = (int)label.as.number;
                            if (class_id >= 0 && class_id < 100) {
                                class_counts[class_id]++;
                                if (class_counts[class_id] > class_counts[max_class]) {
                                    max_class = class_id;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = (double)max_class;
        
        free(class_counts);
    }
    
    return result;
}

Value kyl_ml_knnClassifierPredictProba(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KNNClassifierModel* model = (KNNClassifierModel*)(uintptr_t)args[0].as.number;
    Value X_test = args[1];
    
    if (X_test.type != VALUE_ARRAY || model->X_train.type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_test = X_test.as.array.count;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_test);
    result.as.array.count = n_test;
    result.as.array.count = n_test;
    
    for (int i = 0; i < n_test; i++) {
        Value query = X_test.as.array.values[i];
        
        Value knn_args[4];
        knn_args[0] = model->X_train;
        knn_args[1] = query;
        knn_args[2].type = VALUE_NUMBER;
        knn_args[2].as.number = model->n_neighbors;
        knn_args[3].type = VALUE_STRING;
        knn_args[3].as.string = model->metric;
        
        Value neighbors = kyl_ds_knnFind(4, knn_args);
        
        if (neighbors.type != VALUE_ARRAY) {
            result.as.array.values[i].type = VALUE_NUMBER;
            result.as.array.values[i].as.number = 0.0;
            continue;
        }
        
        // Return probability of positive class (class 1)
        int count_positive = 0;
        for (int j = 0; j < neighbors.as.array.count; j++) {
            Value neighbor = neighbors.as.array.values[j];
            if (neighbor.type == VALUE_OBJECT) {
                Value* idx_val = NULL;
                for (int k = 0; k < neighbor.as.object.count; k++) {
                    if (strcmp(neighbor.as.object.keys[k], "index") == 0) {
                        idx_val = &neighbor.as.object.values[k];
                        break;
                    }
                }
                
                if (idx_val && idx_val->type == VALUE_NUMBER) {
                    int idx = (int)idx_val->as.number;
                    if (idx >= 0 && idx < model->y_train.as.array.count) {
                        Value label = model->y_train.as.array.values[idx];
                        if (label.type == VALUE_NUMBER && label.as.number >= 0.5) {
                            count_positive++;
                        }
                    }
                }
            }
        }
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = (double)count_positive / model->n_neighbors;
    }
    
    return result;
}

Value kyl_ml_knnClassifierScore(int argc, Value* args) {
    if (argc < 3 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value pred_args[2] = {args[0], args[1]};
    Value y_pred = kyl_ml_knnClassifierPredict(2, pred_args);
    
    if (y_pred.type != VALUE_ARRAY || args[2].type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int correct = 0;
    int total = y_pred.as.array.count;
    
    for (int i = 0; i < total && i < args[2].as.array.count; i++) {
        double pred = y_pred.as.array.values[i].as.number;
        double actual = args[2].as.array.values[i].as.number;
        if (fabs(pred - actual) < 0.5) correct++;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)correct / total;
    return result;
}

Value kyl_ml_knnClassifierDestroy(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KNNClassifierModel* model = (KNNClassifierModel*)(uintptr_t)args[0].as.number;
    if (model->metric) free(model->metric);
    free(model);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
