// Feature Scalers: StandardScaler and MinMaxScaler

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ast.h"

// ============================================
// StandardScaler: (X - mean) / std
// ============================================

typedef struct {
    double* mean;
    double* std;
    int n_features;
    bool fitted;
} StandardScalerModel;

static double** value_to_2d_array(Value dataset, int* n_samples, int* n_features) {
    if (dataset.type != VALUE_ARRAY) return NULL;
    *n_samples = dataset.as.array.count;
    if (*n_samples == 0) return NULL;
    
    Value first = dataset.as.array.values[0];
    if (first.type != VALUE_ARRAY) return NULL;
    *n_features = first.as.array.count;
    
    double** data = (double**)malloc(sizeof(double*) * (*n_samples));
    for (int i = 0; i < *n_samples; i++) {
        data[i] = (double*)malloc(sizeof(double) * (*n_features));
        Value row = dataset.as.array.values[i];
        for (int j = 0; j < *n_features; j++) {
            Value v = row.as.array.values[j];
            data[i][j] = (v.type == VALUE_NUMBER) ? v.as.number : 0.0;
        }
    }
    return data;
}

Value kyl_ml_scalerCreate(int argc, Value* args) {
    StandardScalerModel* model = (StandardScalerModel*)malloc(sizeof(StandardScalerModel));
    model->mean = NULL;
    model->std = NULL;
    model->n_features = 0;
    model->fitted = false;
    
    Value v = {VALUE_NUMBER};
    v.as.number = (double)(uintptr_t)model;
    return v;
}

Value kyl_ml_scalerFit(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    model->n_features = n_features;
    model->mean = (double*)calloc(n_features, sizeof(double));
    model->std = (double*)calloc(n_features, sizeof(double));
    
    // Calculate mean
    for (int j = 0; j < n_features; j++) {
        for (int i = 0; i < n_samples; i++) {
            model->mean[j] += X[i][j];
        }
        model->mean[j] /= n_samples;
    }
    
    // Calculate std
    for (int j = 0; j < n_features; j++) {
        for (int i = 0; i < n_samples; i++) {
            double diff = X[i][j] - model->mean[j];
            model->std[j] += diff * diff;
        }
        model->std[j] = sqrt(model->std[j] / n_samples);
        if (model->std[j] < 1e-10) model->std[j] = 1.0;  // Avoid division by zero
    }
    
    model->fitted = true;
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ml_scalerTransform(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X || n_features != model->n_features) {
        if (X) {
            for (int i = 0; i < n_samples; i++) free(X[i]);
            free(X);
        }
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    // Create result array
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    result.as.array.count = n_samples;
    result.as.array.count = n_samples;
    
    for (int i = 0; i < n_samples; i++) {
        result.as.array.values[i].type = VALUE_ARRAY;
        result.as.array.values[i].as.array = (Array*)malloc(sizeof(Array));
        result.as.array.values[i].as.array.values = (Value*)malloc(sizeof(Value) * n_features);
        result.as.array.values[i].as.array.count = n_features;
        result.as.array.values[i].as.array.count = n_features;
        
        for (int j = 0; j < n_features; j++) {
            double scaled = (X[i][j] - model->mean[j]) / model->std[j];
            result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
            result.as.array.values[i].as.array.values[j].as.number = scaled;
        }
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

Value kyl_ml_scalerFitTransform(int argc, Value* args) {
    Value fit_result = kyl_ml_scalerFit(argc, args);
    if (fit_result.type == VALUE_NIL) return fit_result;
    return kyl_ml_scalerTransform(argc, args);
}

Value kyl_ml_scalerInverseTransform(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X || n_features != model->n_features) {
        if (X) {
            for (int i = 0; i < n_samples; i++) free(X[i]);
            free(X);
        }
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    result.as.array.count = n_samples;
    result.as.array.count = n_samples;
    
    for (int i = 0; i < n_samples; i++) {
        result.as.array.values[i].type = VALUE_ARRAY;
        result.as.array.values[i].as.array = (Array*)malloc(sizeof(Array));
        result.as.array.values[i].as.array.values = (Value*)malloc(sizeof(Value) * n_features);
        result.as.array.values[i].as.array.count = n_features;
        result.as.array.values[i].as.array.count = n_features;
        
        for (int j = 0; j < n_features; j++) {
            double original = X[i][j] * model->std[j] + model->mean[j];
            result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
            result.as.array.values[i].as.array.values[j].as.number = original;
        }
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

Value kyl_ml_scalerGetMean(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * model->n_features);
    result.as.array.count = model->n_features;
    result.as.array.count = model->n_features;
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->mean[i];
    }
    
    return result;
}

Value kyl_ml_scalerGetStd(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * model->n_features);
    result.as.array.count = model->n_features;
    result.as.array.count = model->n_features;
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->std[i];
    }
    
    return result;
}

Value kyl_ml_scalerDestroy(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    StandardScalerModel* model = (StandardScalerModel*)(uintptr_t)args[0].as.number;
    if (model->mean) free(model->mean);
    if (model->std) free(model->std);
    free(model);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// ============================================
// MinMaxScaler: (X - min) / (max - min) * (target_max - target_min) + target_min
// ============================================

typedef struct {
    double* min_vals;
    double* max_vals;
    int n_features;
    double feature_min;  // target range min (default 0)
    double feature_max;  // target range max (default 1)
    bool fitted;
} MinMaxScalerModel;

Value kyl_ml_minmaxScalerCreate(int argc, Value* args) {
    double min_val = (argc > 0 && args[0].type == VALUE_NUMBER) ? args[0].as.number : 0.0;
    double max_val = (argc > 1 && args[1].type == VALUE_NUMBER) ? args[1].as.number : 1.0;
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)malloc(sizeof(MinMaxScalerModel));
    model->min_vals = NULL;
    model->max_vals = NULL;
    model->n_features = 0;
    model->feature_min = min_val;
    model->feature_max = max_val;
    model->fitted = false;
    
    Value v = {VALUE_NUMBER};
    v.as.number = (double)(uintptr_t)model;
    return v;
}

Value kyl_ml_minmaxScalerFit(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    model->n_features = n_features;
    model->min_vals = (double*)malloc(n_features * sizeof(double));
    model->max_vals = (double*)malloc(n_features * sizeof(double));
    
    // Initialize with first row
    for (int j = 0; j < n_features; j++) {
        model->min_vals[j] = X[0][j];
        model->max_vals[j] = X[0][j];
    }
    
    // Find min and max
    for (int j = 0; j < n_features; j++) {
        for (int i = 0; i < n_samples; i++) {
            if (X[i][j] < model->min_vals[j]) model->min_vals[j] = X[i][j];
            if (X[i][j] > model->max_vals[j]) model->max_vals[j] = X[i][j];
        }
        // Avoid division by zero
        if (fabs(model->max_vals[j] - model->min_vals[j]) < 1e-10) {
            model->max_vals[j] = model->min_vals[j] + 1.0;
        }
    }
    
    model->fitted = true;
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

Value kyl_ml_minmaxScalerTransform(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X || n_features != model->n_features) {
        if (X) {
            for (int i = 0; i < n_samples; i++) free(X[i]);
            free(X);
        }
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    result.as.array.count = n_samples;
    result.as.array.count = n_samples;
    
    for (int i = 0; i < n_samples; i++) {
        result.as.array.values[i].type = VALUE_ARRAY;
        result.as.array.values[i].as.array = (Array*)malloc(sizeof(Array));
        result.as.array.values[i].as.array.values = (Value*)malloc(sizeof(Value) * n_features);
        result.as.array.values[i].as.array.count = n_features;
        result.as.array.values[i].as.array.count = n_features;
        
        for (int j = 0; j < n_features; j++) {
            double std_val = (X[i][j] - model->min_vals[j]) / (model->max_vals[j] - model->min_vals[j]);
            double scaled = std_val * (model->feature_max - model->feature_min) + model->feature_min;
            result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
            result.as.array.values[i].as.array.values[j].as.number = scaled;
        }
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

Value kyl_ml_minmaxScalerFitTransform(int argc, Value* args) {
    Value fit_result = kyl_ml_minmaxScalerFit(argc, args);
    if (fit_result.type == VALUE_NIL) return fit_result;
    return kyl_ml_minmaxScalerTransform(argc, args);
}

Value kyl_ml_minmaxScalerInverseTransform(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X || n_features != model->n_features) {
        if (X) {
            for (int i = 0; i < n_samples; i++) free(X[i]);
            free(X);
        }
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    result.as.array.count = n_samples;
    result.as.array.count = n_samples;
    
    for (int i = 0; i < n_samples; i++) {
        result.as.array.values[i].type = VALUE_ARRAY;
        result.as.array.values[i].as.array = (Array*)malloc(sizeof(Array));
        result.as.array.values[i].as.array.values = (Value*)malloc(sizeof(Value) * n_features);
        result.as.array.values[i].as.array.count = n_features;
        result.as.array.values[i].as.array.count = n_features;
        
        for (int j = 0; j < n_features; j++) {
            double std_val = (X[i][j] - model->feature_min) / (model->feature_max - model->feature_min);
            double original = std_val * (model->max_vals[j] - model->min_vals[j]) + model->min_vals[j];
            result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
            result.as.array.values[i].as.array.values[j].as.number = original;
        }
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

Value kyl_ml_minmaxScalerGetMin(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * model->n_features);
    result.as.array.count = model->n_features;
    result.as.array.count = model->n_features;
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->min_vals[i];
    }
    
    return result;
}

Value kyl_ml_minmaxScalerGetMax(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    if (!model->fitted) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * model->n_features);
    result.as.array.count = model->n_features;
    result.as.array.count = model->n_features;
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->max_vals[i];
    }
    
    return result;
}

Value kyl_ml_minmaxScalerDestroy(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    MinMaxScalerModel* model = (MinMaxScalerModel*)(uintptr_t)args[0].as.number;
    if (model->min_vals) free(model->min_vals);
    if (model->max_vals) free(model->max_vals);
    free(model);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
