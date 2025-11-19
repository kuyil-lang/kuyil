// Linear Regression Implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../src/ast.h"

typedef struct {
    double* coefficients;   // Weights [n_features]
    double intercept;       // Bias term
    int n_features;
    int fit_intercept;
    char method[32];        // "normal" or "gradient_descent"
} LinearRegressionModel;

// Helper: Matrix operations
static double** value_to_2d_array(Value* dataset, int* out_rows, int* out_cols);
static void free_2d_array(double** arr, int n_rows);
static double* value_to_1d_array(Value* arr, int* out_len);

// Normal equation: θ = (X^T X)^(-1) X^T y
static int fit_normal_equation(LinearRegressionModel* model, double** X, double* y, int n_samples, int n_features) {
    // Simplified implementation for demonstration
    // In production, use proper matrix library (BLAS/LAPACK)
    
    // For now, use gradient descent as fallback
    return 0; // Indicate to use gradient descent
}

// Gradient descent
static void fit_gradient_descent(LinearRegressionModel* model, double** X, double* y, int n_samples, int n_features) {
    double learning_rate = 0.01;
    int max_iterations = 1000;
    
    // Initialize coefficients to zero
    for (int i = 0; i < n_features; i++) {
        model->coefficients[i] = 0.0;
    }
    model->intercept = 0.0;
    
    // Gradient descent
    for (int iter = 0; iter < max_iterations; iter++) {
        double* gradients = calloc(n_features, sizeof(double));
        double intercept_gradient = 0.0;
        
        // Compute gradients
        for (int i = 0; i < n_samples; i++) {
            double prediction = model->intercept;
            for (int j = 0; j < n_features; j++) {
                prediction += model->coefficients[j] * X[i][j];
            }
            
            double error = prediction - y[i];
            
            for (int j = 0; j < n_features; j++) {
                gradients[j] += error * X[i][j];
            }
            intercept_gradient += error;
        }
        
        // Update parameters
        for (int j = 0; j < n_features; j++) {
            model->coefficients[j] -= learning_rate * gradients[j] / n_samples;
        }
        if (model->fit_intercept) {
            model->intercept -= learning_rate * intercept_gradient / n_samples;
        }
        
        free(gradients);
    }
}

Value kyl_ml_linearRegressionCreate(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    LinearRegressionModel* model = malloc(sizeof(LinearRegressionModel));
    model->fit_intercept = (int)args[0].as.number;
    
    if (args[1].type == VALUE_STRING) {
        strncpy(model->method, args[1].as.string, 31);
    } else {
        strcpy(model->method, "gradient_descent");
    }
    
    model->coefficients = NULL;
    model->intercept = 0.0;
    model->n_features = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)model;
    return result;
}

Value kyl_ml_linearRegressionFit(int arg_count, Value* args) {
    if (arg_count < 3) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (!model) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(&args[1], &n_samples, &n_features);
    
    int y_len;
    double* y = value_to_1d_array(&args[2], &y_len);
    
    if (!X || !y || n_samples != y_len) {
        free_2d_array(X, n_samples);
        free(y);
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    model->n_features = n_features;
    model->coefficients = malloc(sizeof(double) * n_features);
    
    fit_gradient_descent(model, X, y, n_samples, n_features);
    
    free_2d_array(X, n_samples);
    free(y);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = 1;
    return result;
}

Value kyl_ml_linearRegressionPredict(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (!model || !model->coefficients) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int n_samples, n_features;
    double** X = value_to_2d_array(&args[1], &n_samples, &n_features);
    
    if (!X) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = n_samples;
    result.as.array.values = malloc(sizeof(Value) * n_samples);
    
    for (int i = 0; i < n_samples; i++) {
        double prediction = model->intercept;
        for (int j = 0; j < n_features; j++) {
            prediction += model->coefficients[j] * X[i][j];
        }
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = prediction;
    }
    
    free_2d_array(X, n_samples);
    return result;
}

Value kyl_ml_linearRegressionGetCoefficients(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (!model || !model->coefficients) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = model->n_features;
    result.as.array.values = malloc(sizeof(Value) * model->n_features);
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->coefficients[i];
    }
    
    return result;
}

Value kyl_ml_linearRegressionGetIntercept(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = model ? model->intercept : 0.0;
    return result;
}

Value kyl_ml_linearRegressionScore(int arg_count, Value* args) {
    // Calculate R² score
    if (arg_count < 3) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (!model) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    // Get predictions
    Value pred_result = kyl_ml_linearRegressionPredict(2, args);
    
    int y_len;
    double* y_true = value_to_1d_array(&args[2], &y_len);
    
    if (!y_true || pred_result.as.array.count != y_len) {
        free(y_true);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    // Calculate R²
    double y_mean = 0.0;
    for (int i = 0; i < y_len; i++) {
        y_mean += y_true[i];
    }
    y_mean /= y_len;
    
    double ss_tot = 0.0, ss_res = 0.0;
    for (int i = 0; i < y_len; i++) {
        double y_pred = pred_result.as.array.values[i].as.number;
        ss_tot += (y_true[i] - y_mean) * (y_true[i] - y_mean);
        ss_res += (y_true[i] - y_pred) * (y_true[i] - y_pred);
    }
    
    double r2 = 1.0 - (ss_res / ss_tot);
    
    free(y_true);
    free(pred_result.as.array.values);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = r2;
    return result;
}

Value kyl_ml_linearRegressionDestroy(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (model) {
        free(model->coefficients);
        free(model);
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = 1;
    return result;
}

// Helper implementations
static double** value_to_2d_array(Value* dataset, int* out_rows, int* out_cols) {
    if (!dataset || dataset->type != VALUE_ARRAY) {
        *out_rows = *out_cols = 0;
        return NULL;
    }
    
    int n_rows = dataset->as.array.count;
    if (n_rows == 0 || dataset->as.array.values[0].type != VALUE_ARRAY) {
        *out_rows = *out_cols = 0;
        return NULL;
    }
    
    int n_cols = dataset->as.array.values[0].as.array.count;
    double** data = malloc(sizeof(double*) * n_rows);
    
    for (int i = 0; i < n_rows; i++) {
        data[i] = malloc(sizeof(double) * n_cols);
        Value* row = &dataset->as.array.values[i];
        
        for (int j = 0; j < n_cols; j++) {
            if (row->as.array.values[j].type == VALUE_NUMBER) {
                data[i][j] = row->as.array.values[j].as.number;
            } else {
                data[i][j] = 0.0;
            }
        }
    }
    
    *out_rows = n_rows;
    *out_cols = n_cols;
    return data;
}

static void free_2d_array(double** arr, int n_rows) {
    if (arr) {
        for (int i = 0; i < n_rows; i++) {
            free(arr[i]);
        }
        free(arr);
    }
}

static double* value_to_1d_array(Value* arr, int* out_len) {
    if (!arr || arr->type != VALUE_ARRAY) {
        *out_len = 0;
        return NULL;
    }
    
    int len = arr->as.array.count;
    double* result = malloc(sizeof(double) * len);
    
    for (int i = 0; i < len; i++) {
        if (arr->as.array.values[i].type == VALUE_NUMBER) {
            result[i] = arr->as.array.values[i].as.number;
        } else {
            result[i] = 0.0;
        }
    }
    
    *out_len = len;
    return result;
}

Value kyl_ml_linearRegressionSave(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    LinearRegressionModel* model = (LinearRegressionModel*)(uintptr_t)args[0].as.number;
    if (!model || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    const char* filename = args[1].as.string;
    FILE* f = fopen(filename, "w");
    if (!f) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    // Write model parameters
    fprintf(f, "LINEAR_REGRESSION_V1\n");
    fprintf(f, "n_features=%d\n", model->n_features);
    fprintf(f, "fit_intercept=%d\n", model->fit_intercept);
    fprintf(f, "method=%s\n", model->method);
    fprintf(f, "intercept=%.10f\n", model->intercept);
    
    // Write coefficients
    fprintf(f, "coefficients:\n");
    for (int i = 0; i < model->n_features; i++) {
        fprintf(f, "%.10f", model->coefficients[i]);
        if (i < model->n_features - 1) fprintf(f, ",");
    }
    fprintf(f, "\n");
    
    fclose(f);
    return (Value){VALUE_BOOL, .as.boolean = 1};
}

Value kyl_ml_linearRegressionLoad(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    const char* filename = args[0].as.string;
    FILE* f = fopen(filename, "r");
    if (!f) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    char header[64];
    if (!fgets(header, sizeof(header), f) || strncmp(header, "LINEAR_REGRESSION_V1", 20) != 0) {
        fclose(f);
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    // Allocate model
    LinearRegressionModel* model = malloc(sizeof(LinearRegressionModel));
    
    // Read parameters
    fscanf(f, "n_features=%d\n", &model->n_features);
    fscanf(f, "fit_intercept=%d\n", &model->fit_intercept);
    fscanf(f, "method=%s\n", model->method);
    fscanf(f, "intercept=%lf\n", &model->intercept);
    
    // Allocate and read coefficients
    model->coefficients = malloc(sizeof(double) * model->n_features);
    fgets(header, sizeof(header), f); // "coefficients:\n"
    for (int i = 0; i < model->n_features; i++) {
        fscanf(f, "%lf", &model->coefficients[i]);
        if (i < model->n_features - 1) fgetc(f); // skip comma
    }
    
    fclose(f);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)model;
    return result;
}
