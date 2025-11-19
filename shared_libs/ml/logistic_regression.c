// Logistic Regression for Binary Classification
// Implements sigmoid activation, log loss, gradient descent

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ast.h"

typedef struct {
    double* coefficients;  // Weight vector
    double intercept;      // Bias term
    int n_features;
    bool fit_intercept;
    double learning_rate;
    int max_iterations;
    double tolerance;
    bool converged;
    int iterations_run;
} LogisticRegressionModel;

// Sigmoid function: 1 / (1 + e^(-z))
static double sigmoid(double z) {
    // Clip to prevent overflow
    if (z > 500.0) return 1.0;
    if (z < -500.0) return 0.0;
    return 1.0 / (1.0 + exp(-z));
}

// Convert Value array to C double array
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
        if (row.type != VALUE_ARRAY) {
            // Cleanup and return NULL
            for (int j = 0; j < i; j++) free(data[j]);
            free(data);
            return NULL;
        }
        for (int j = 0; j < *n_features; j++) {
            Value v = row.as.array.values[j];
            data[i][j] = (v.type == VALUE_NUMBER) ? v.as.number : 0.0;
        }
    }
    return data;
}

static double* value_to_1d_array(Value arr, int* length) {
    if (arr.type != VALUE_ARRAY) return NULL;
    *length = arr.as.array.count;
    double* data = (double*)malloc(sizeof(double) * (*length));
    for (int i = 0; i < *length; i++) {
        Value v = arr.as.array.values[i];
        data[i] = (v.type == VALUE_NUMBER) ? v.as.number : 0.0;
    }
    return data;
}

// Create logistic regression model
Value kyl_ml_logisticRegressionCreate(int argc, Value* args) {
    if (argc < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    bool fit_intercept = (args[0].type == VALUE_BOOL) ? args[0].as.boolean : true;
    double learning_rate = (args[1].type == VALUE_NUMBER) ? args[1].as.number : 0.01;
    int max_iterations = (int)((args[2].type == VALUE_NUMBER) ? args[2].as.number : 1000);
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)malloc(sizeof(LogisticRegressionModel));
    model->coefficients = NULL;
    model->intercept = 0.0;
    model->n_features = 0;
    model->fit_intercept = fit_intercept;
    model->learning_rate = learning_rate;
    model->max_iterations = max_iterations;
    model->tolerance = 1e-4;
    model->converged = false;
    model->iterations_run = 0;
    
    Value v = {VALUE_NUMBER};
    v.as.number = (double)(uintptr_t)model;
    return v;
}

// Fit logistic regression using gradient descent
Value kyl_ml_logisticRegressionFit(int argc, Value* args) {
    if (argc < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    if (args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    
    int n_samples, n_features;
    double** X = value_to_2d_array(args[1], &n_samples, &n_features);
    if (!X) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int y_len;
    double* y = value_to_1d_array(args[2], &y_len);
    if (!y || y_len != n_samples) {
        for (int i = 0; i < n_samples; i++) free(X[i]);
        free(X);
        if (y) free(y);
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    // Initialize weights
    model->n_features = n_features;
    model->coefficients = (double*)calloc(n_features, sizeof(double));
    model->intercept = 0.0;
    
    // Gradient descent
    double prev_loss = INFINITY;
    for (int iter = 0; iter < model->max_iterations; iter++) {
        // Compute predictions and loss
        double loss = 0.0;
        double* grad = (double*)calloc(n_features, sizeof(double));
        double grad_intercept = 0.0;
        
        for (int i = 0; i < n_samples; i++) {
            // Compute linear combination: z = w·x + b
            double z = model->intercept;
            for (int j = 0; j < n_features; j++) {
                z += model->coefficients[j] * X[i][j];
            }
            
            double pred = sigmoid(z);
            double error = pred - y[i];
            
            // Log loss: -[y·log(p) + (1-y)·log(1-p)]
            double p = fmax(1e-15, fmin(1.0 - 1e-15, pred));  // Clip for numerical stability
            loss += -(y[i] * log(p) + (1.0 - y[i]) * log(1.0 - p));
            
            // Gradients
            for (int j = 0; j < n_features; j++) {
                grad[j] += error * X[i][j];
            }
            grad_intercept += error;
        }
        
        loss /= n_samples;
        
        // Update weights
        for (int j = 0; j < n_features; j++) {
            model->coefficients[j] -= model->learning_rate * (grad[j] / n_samples);
        }
        if (model->fit_intercept) {
            model->intercept -= model->learning_rate * (grad_intercept / n_samples);
        }
        
        free(grad);
        
        // Check convergence
        if (fabs(prev_loss - loss) < model->tolerance) {
            model->converged = true;
            model->iterations_run = iter + 1;
            break;
        }
        prev_loss = loss;
        model->iterations_run = iter + 1;
    }
    
    // Cleanup
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    free(y);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}

// Predict class labels (0 or 1)
Value kyl_ml_logisticRegressionPredict(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    
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
    
    // Predict
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * n_samples);
    result.as.array.count = n_samples;
    result.as.array.count = n_samples;
    
    for (int i = 0; i < n_samples; i++) {
        double z = model->intercept;
        for (int j = 0; j < n_features; j++) {
            z += model->coefficients[j] * X[i][j];
        }
        double prob = sigmoid(z);
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = (prob >= 0.5) ? 1.0 : 0.0;
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

// Predict probabilities
Value kyl_ml_logisticRegressionPredictProba(int argc, Value* args) {
    if (argc < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    
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
        double z = model->intercept;
        for (int j = 0; j < n_features; j++) {
            z += model->coefficients[j] * X[i][j];
        }
        double prob = sigmoid(z);
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = prob;
    }
    
    for (int i = 0; i < n_samples; i++) free(X[i]);
    free(X);
    
    return result;
}

Value kyl_ml_logisticRegressionGetCoefficients(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    // Array is inline
    result.as.array.values = (Value*)malloc(sizeof(Value) * model->n_features);
    result.as.array.count = model->n_features;
    result.as.array.count = model->n_features;
    
    for (int i = 0; i < model->n_features; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->coefficients[i];
    }
    
    return result;
}

Value kyl_ml_logisticRegressionGetIntercept(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = model->intercept;
    return result;
}

Value kyl_ml_logisticRegressionScore(int argc, Value* args) {
    if (argc < 3 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    // Get predictions
    Value pred_args[2] = {args[0], args[1]};
    Value y_pred = kyl_ml_logisticRegressionPredict(2, pred_args);
    
    if (y_pred.type != VALUE_ARRAY) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int y_len;
    double* y_true = value_to_1d_array(args[2], &y_len);
    if (!y_true) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    // Calculate accuracy
    int correct = 0;
    for (int i = 0; i < y_len && i < y_pred.as.array.count; i++) {
        double pred = y_pred.as.array.values[i].as.number;
        if (fabs(pred - y_true[i]) < 0.5) correct++;
    }
    
    free(y_true);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)correct / y_len;
    return result;
}

Value kyl_ml_logisticRegressionDestroy(int argc, Value* args) {
    if (argc < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    LogisticRegressionModel* model = (LogisticRegressionModel*)(uintptr_t)args[0].as.number;
    if (model->coefficients) free(model->coefficients);
    free(model);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = true;
    return result;
}
