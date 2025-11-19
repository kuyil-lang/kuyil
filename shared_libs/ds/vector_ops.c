// Vector Operations for ML/AI - Similarity, Distance, and Vector Math
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "../../src/ast.h"

// Convert array Value to double array
static double* value_array_to_doubles(Value* arr, int* out_len) {
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

// Dot product of two vectors
Value kyl_ds_vectorDotProduct(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double dot_product = 0.0;
    for (int i = 0; i < len1; i++) {
        dot_product += vec1[i] * vec2[i];
    }
    
    free(vec1);
    free(vec2);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = dot_product;
    return result;
}

// Magnitude (L2 norm) of a vector
Value kyl_ds_vectorMagnitude(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_ARRAY) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len;
    double* vec = value_array_to_doubles(&args[0], &len);
    
    if (!vec) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double sum_squares = 0.0;
    for (int i = 0; i < len; i++) {
        sum_squares += vec[i] * vec[i];
    }
    
    free(vec);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = sqrt(sum_squares);
    return result;
}

// Normalize a vector (unit vector)
Value kyl_ds_vectorNormalize(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_ARRAY) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int len;
    double* vec = value_array_to_doubles(&args[0], &len);
    
    if (!vec) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    // Calculate magnitude
    double mag = 0.0;
    for (int i = 0; i < len; i++) {
        mag += vec[i] * vec[i];
    }
    mag = sqrt(mag);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = len;
    result.as.array.values = malloc(sizeof(Value) * len);
    
    if (mag == 0.0) {
        // Return zero vector if magnitude is zero
        for (int i = 0; i < len; i++) {
            result.as.array.values[i].type = VALUE_NUMBER;
            result.as.array.values[i].as.number = 0.0;
        }
    } else {
        for (int i = 0; i < len; i++) {
            result.as.array.values[i].type = VALUE_NUMBER;
            result.as.array.values[i].as.number = vec[i] / mag;
        }
    }
    
    free(vec);
    return result;
}

// Cosine similarity between two vectors
Value kyl_ds_vectorCosineSimilarity(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double dot_product = 0.0;
    double mag1 = 0.0;
    double mag2 = 0.0;
    
    for (int i = 0; i < len1; i++) {
        dot_product += vec1[i] * vec2[i];
        mag1 += vec1[i] * vec1[i];
        mag2 += vec2[i] * vec2[i];
    }
    
    free(vec1);
    free(vec2);
    
    mag1 = sqrt(mag1);
    mag2 = sqrt(mag2);
    
    if (mag1 == 0.0 || mag2 == 0.0) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = dot_product / (mag1 * mag2);
    return result;
}

// Euclidean distance between two vectors
Value kyl_ds_vectorEuclideanDistance(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double sum_squares = 0.0;
    for (int i = 0; i < len1; i++) {
        double diff = vec1[i] - vec2[i];
        sum_squares += diff * diff;
    }
    
    free(vec1);
    free(vec2);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = sqrt(sum_squares);
    return result;
}

// Manhattan distance between two vectors
Value kyl_ds_vectorManhattanDistance(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    double sum_abs = 0.0;
    for (int i = 0; i < len1; i++) {
        sum_abs += fabs(vec1[i] - vec2[i]);
    }
    
    free(vec1);
    free(vec2);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = sum_abs;
    return result;
}

// Vector addition
Value kyl_ds_vectorAdd(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = len1;
    result.as.array.values = malloc(sizeof(Value) * len1);
    
    for (int i = 0; i < len1; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = vec1[i] + vec2[i];
    }
    
    free(vec1);
    free(vec2);
    return result;
}

// Vector subtraction
Value kyl_ds_vectorSubtract(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_ARRAY) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int len1, len2;
    double* vec1 = value_array_to_doubles(&args[0], &len1);
    double* vec2 = value_array_to_doubles(&args[1], &len2);
    
    if (!vec1 || !vec2 || len1 != len2) {
        free(vec1);
        free(vec2);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = len1;
    result.as.array.values = malloc(sizeof(Value) * len1);
    
    for (int i = 0; i < len1; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = vec1[i] - vec2[i];
    }
    
    free(vec1);
    free(vec2);
    return result;
}

// Scalar multiplication
Value kyl_ds_vectorScale(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_ARRAY || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int len;
    double* vec = value_array_to_doubles(&args[0], &len);
    double scalar = args[1].as.number;
    
    if (!vec) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = len;
    result.as.array.values = malloc(sizeof(Value) * len);
    
    for (int i = 0; i < len; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = vec[i] * scalar;
    }
    
    free(vec);
    return result;
}
