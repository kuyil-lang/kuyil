// ML Metrics Implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../src/ast.h"

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

// Accuracy Score
Value kyl_ml_accuracyScore(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int correct = 0;
    for (int i = 0; i < len1; i++) {
        if ((int)y_true[i] == (int)y_pred[i]) {
            correct++;
        }
    }
    
    double accuracy = (double)correct / len1;
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = accuracy;
    return result;
}

// Precision Score (binary classification)
Value kyl_ml_precisionScore(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int tp = 0, fp = 0;
    for (int i = 0; i < len1; i++) {
        int true_label = (int)y_true[i];
        int pred_label = (int)y_pred[i];
        
        if (pred_label == 1) {
            if (true_label == 1) {
                tp++;
            } else {
                fp++;
            }
        }
    }
    
    double precision = (tp + fp > 0) ? (double)tp / (tp + fp) : 0.0;
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = precision;
    return result;
}

// Recall Score (binary classification)
Value kyl_ml_recallScore(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int tp = 0, fn = 0;
    for (int i = 0; i < len1; i++) {
        int true_label = (int)y_true[i];
        int pred_label = (int)y_pred[i];
        
        if (true_label == 1) {
            if (pred_label == 1) {
                tp++;
            } else {
                fn++;
            }
        }
    }
    
    double recall = (tp + fn > 0) ? (double)tp / (tp + fn) : 0.0;
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = recall;
    return result;
}

// F1 Score
Value kyl_ml_f1Score(int arg_count, Value* args) {
    Value precision_result = kyl_ml_precisionScore(arg_count, args);
    Value recall_result = kyl_ml_recallScore(arg_count, args);
    
    double precision = precision_result.as.number;
    double recall = recall_result.as.number;
    
    double f1 = (precision + recall > 0) ? 2 * (precision * recall) / (precision + recall) : 0.0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = f1;
    return result;
}

// Mean Squared Error
Value kyl_ml_meanSquaredError(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double mse = 0.0;
    for (int i = 0; i < len1; i++) {
        double diff = y_true[i] - y_pred[i];
        mse += diff * diff;
    }
    mse /= len1;
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = mse;
    return result;
}

// Root Mean Squared Error
Value kyl_ml_rootMeanSquaredError(int arg_count, Value* args) {
    Value mse_result = kyl_ml_meanSquaredError(arg_count, args);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = sqrt(mse_result.as.number);
    return result;
}

// Mean Absolute Error
Value kyl_ml_meanAbsoluteError(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double mae = 0.0;
    for (int i = 0; i < len1; i++) {
        mae += fabs(y_true[i] - y_pred[i]);
    }
    mae /= len1;
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = mae;
    return result;
}

// R² Score
Value kyl_ml_r2Score(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    // Calculate mean
    double y_mean = 0.0;
    for (int i = 0; i < len1; i++) {
        y_mean += y_true[i];
    }
    y_mean /= len1;
    
    // Calculate R²
    double ss_tot = 0.0, ss_res = 0.0;
    for (int i = 0; i < len1; i++) {
        ss_tot += (y_true[i] - y_mean) * (y_true[i] - y_mean);
        ss_res += (y_true[i] - y_pred[i]) * (y_true[i] - y_pred[i]);
    }
    
    double r2 = 1.0 - (ss_res / ss_tot);
    
    free(y_true);
    free(y_pred);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = r2;
    return result;
}

// Confusion Matrix (binary classification)
Value kyl_ml_confusionMatrix(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int len1, len2;
    double* y_true = value_to_1d_array(&args[0], &len1);
    double* y_pred = value_to_1d_array(&args[1], &len2);
    
    if (!y_true || !y_pred || len1 != len2) {
        free(y_true);
        free(y_pred);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int tp = 0, tn = 0, fp = 0, fn = 0;
    
    for (int i = 0; i < len1; i++) {
        int true_label = (int)y_true[i];
        int pred_label = (int)y_pred[i];
        
        if (true_label == 1 && pred_label == 1) tp++;
        else if (true_label == 0 && pred_label == 0) tn++;
        else if (true_label == 0 && pred_label == 1) fp++;
        else if (true_label == 1 && pred_label == 0) fn++;
    }
    
    free(y_true);
    free(y_pred);
    
    // Return [[tn, fp], [fn, tp]]
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = 2;
    result.as.array.values = malloc(sizeof(Value) * 2);
    
    // Row 0: [tn, fp]
    result.as.array.values[0].type = VALUE_ARRAY;
    result.as.array.values[0].as.array.count = 2;
    result.as.array.values[0].as.array.values = malloc(sizeof(Value) * 2);
    result.as.array.values[0].as.array.values[0] = (Value){VALUE_NUMBER, .as.number = tn};
    result.as.array.values[0].as.array.values[1] = (Value){VALUE_NUMBER, .as.number = fp};
    
    // Row 1: [fn, tp]
    result.as.array.values[1].type = VALUE_ARRAY;
    result.as.array.values[1].as.array.count = 2;
    result.as.array.values[1].as.array.values = malloc(sizeof(Value) * 2);
    result.as.array.values[1].as.array.values[0] = (Value){VALUE_NUMBER, .as.number = fn};
    result.as.array.values[1].as.array.values[1] = (Value){VALUE_NUMBER, .as.number = tp};
    
    return result;
}
