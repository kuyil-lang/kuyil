// KNN and Similarity Search for ML/AI
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "../../src/ast.h"

typedef struct {
    int index;
    double distance;
} DistancePair;

// Comparison function for sorting by distance
static int compare_distances(const void* a, const void* b) {
    DistancePair* pa = (DistancePair*)a;
    DistancePair* pb = (DistancePair*)b;
    if (pa->distance < pb->distance) return -1;
    if (pa->distance > pb->distance) return 1;
    return 0;
}

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

// Calculate distance based on metric type
static double calculate_distance(double* vec1, double* vec2, int len, const char* metric) {
    if (strcmp(metric, "cosine") == 0) {
        // Cosine distance = 1 - cosine similarity
        double dot_product = 0.0;
        double mag1 = 0.0;
        double mag2 = 0.0;
        
        for (int i = 0; i < len; i++) {
            dot_product += vec1[i] * vec2[i];
            mag1 += vec1[i] * vec1[i];
            mag2 += vec2[i] * vec2[i];
        }
        
        mag1 = sqrt(mag1);
        mag2 = sqrt(mag2);
        
        if (mag1 == 0.0 || mag2 == 0.0) {
            return 1.0;  // Maximum distance for zero vectors
        }
        
        double similarity = dot_product / (mag1 * mag2);
        return 1.0 - similarity;
    } else if (strcmp(metric, "manhattan") == 0) {
        double sum_abs = 0.0;
        for (int i = 0; i < len; i++) {
            sum_abs += fabs(vec1[i] - vec2[i]);
        }
        return sum_abs;
    } else {
        // Default: euclidean
        double sum_squares = 0.0;
        for (int i = 0; i < len; i++) {
            double diff = vec1[i] - vec2[i];
            sum_squares += diff * diff;
        }
        return sqrt(sum_squares);
    }
}

// Find K nearest neighbors
// Args: query_vector (array), dataset (array of arrays), k (number), metric (string, optional: "euclidean", "cosine", "manhattan")
Value kyl_ds_knnFind(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_ARRAY || 
        args[1].type != VALUE_ARRAY || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int k = (int)args[2].as.number;
    const char* metric = "euclidean";
    
    if (arg_count > 3 && args[3].type == VALUE_STRING) {
        metric = args[3].as.string;
    }
    
    int query_len;
    double* query = value_array_to_doubles(&args[0], &query_len);
    
    if (!query) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value* dataset = &args[1];
    int dataset_size = dataset->as.array.count;
    
    if (k > dataset_size) {
        k = dataset_size;
    }
    
    // Calculate distances for all vectors in dataset
    DistancePair* distances = malloc(sizeof(DistancePair) * dataset_size);
    int valid_count = 0;
    
    for (int i = 0; i < dataset_size; i++) {
        if (dataset->as.array.values[i].type == VALUE_ARRAY) {
            int vec_len;
            double* vec = value_array_to_doubles(&dataset->as.array.values[i], &vec_len);
            
            if (vec && vec_len == query_len) {
                distances[valid_count].index = i;
                distances[valid_count].distance = calculate_distance(query, vec, query_len, metric);
                valid_count++;
            }
            
            free(vec);
        }
    }
    
    free(query);
    
    if (valid_count == 0) {
        free(distances);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    // Sort by distance
    qsort(distances, valid_count, sizeof(DistancePair), compare_distances);
    
    // Return top K indices
    int result_k = (k < valid_count) ? k : valid_count;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = result_k;
    result.as.array.values = malloc(sizeof(Value) * result_k);
    
    for (int i = 0; i < result_k; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = (double)distances[i].index;
    }
    
    free(distances);
    return result;
}

// Find K nearest neighbors with distances
// Returns array of objects: [{index: N, distance: D}, ...]
Value kyl_ds_knnFindWithDistances(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_ARRAY || 
        args[1].type != VALUE_ARRAY || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int k = (int)args[2].as.number;
    const char* metric = "euclidean";
    
    if (arg_count > 3 && args[3].type == VALUE_STRING) {
        metric = args[3].as.string;
    }
    
    int query_len;
    double* query = value_array_to_doubles(&args[0], &query_len);
    
    if (!query) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value* dataset = &args[1];
    int dataset_size = dataset->as.array.count;
    
    if (k > dataset_size) {
        k = dataset_size;
    }
    
    // Calculate distances
    DistancePair* distances = malloc(sizeof(DistancePair) * dataset_size);
    int valid_count = 0;
    
    for (int i = 0; i < dataset_size; i++) {
        if (dataset->as.array.values[i].type == VALUE_ARRAY) {
            int vec_len;
            double* vec = value_array_to_doubles(&dataset->as.array.values[i], &vec_len);
            
            if (vec && vec_len == query_len) {
                distances[valid_count].index = i;
                distances[valid_count].distance = calculate_distance(query, vec, query_len, metric);
                valid_count++;
            }
            
            free(vec);
        }
    }
    
    free(query);
    
    if (valid_count == 0) {
        free(distances);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    // Sort by distance
    qsort(distances, valid_count, sizeof(DistancePair), compare_distances);
    
    // Return top K with distances
    int result_k = (k < valid_count) ? k : valid_count;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = result_k;
    result.as.array.values = malloc(sizeof(Value) * result_k);
    
    for (int i = 0; i < result_k; i++) {
        Value obj = {VALUE_OBJECT};
        obj.as.object.count = 2;
        obj.as.object.keys = malloc(sizeof(char*) * 2);
        obj.as.object.values = malloc(sizeof(Value) * 2);
        
        obj.as.object.keys[0] = strdup("index");
        obj.as.object.values[0].type = VALUE_NUMBER;
        obj.as.object.values[0].as.number = (double)distances[i].index;
        
        obj.as.object.keys[1] = strdup("distance");
        obj.as.object.values[1].type = VALUE_NUMBER;
        obj.as.object.values[1].as.number = distances[i].distance;
        
        result.as.array.values[i] = obj;
    }
    
    free(distances);
    return result;
}

// Find all neighbors within a distance threshold
// Args: query_vector (array), dataset (array of arrays), threshold (number), metric (string, optional)
Value kyl_ds_knnFindByThreshold(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_ARRAY || 
        args[1].type != VALUE_ARRAY || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    double threshold = args[2].as.number;
    const char* metric = "euclidean";
    
    if (arg_count > 3 && args[3].type == VALUE_STRING) {
        metric = args[3].as.string;
    }
    
    int query_len;
    double* query = value_array_to_doubles(&args[0], &query_len);
    
    if (!query) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value* dataset = &args[1];
    int dataset_size = dataset->as.array.count;
    
    // Find all within threshold
    DistancePair* matches = malloc(sizeof(DistancePair) * dataset_size);
    int match_count = 0;
    
    for (int i = 0; i < dataset_size; i++) {
        if (dataset->as.array.values[i].type == VALUE_ARRAY) {
            int vec_len;
            double* vec = value_array_to_doubles(&dataset->as.array.values[i], &vec_len);
            
            if (vec && vec_len == query_len) {
                double distance = calculate_distance(query, vec, query_len, metric);
                
                if (distance <= threshold) {
                    matches[match_count].index = i;
                    matches[match_count].distance = distance;
                    match_count++;
                }
            }
            
            free(vec);
        }
    }
    
    free(query);
    
    // Sort by distance
    qsort(matches, match_count, sizeof(DistancePair), compare_distances);
    
    // Return matching indices
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = match_count;
    result.as.array.values = malloc(sizeof(Value) * match_count);
    
    for (int i = 0; i < match_count; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = (double)matches[i].index;
    }
    
    free(matches);
    return result;
}

// Compute similarity matrix for a set of vectors
// Args: dataset (array of arrays), metric (string, optional)
// Returns: array of arrays (similarity matrix)
Value kyl_ds_knnSimilarityMatrix(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_ARRAY) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    const char* metric = "euclidean";
    if (arg_count > 1 && args[1].type == VALUE_STRING) {
        metric = args[1].as.string;
    }
    
    Value* dataset = &args[0];
    int size = dataset->as.array.count;
    
    // Create similarity matrix
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = size;
    result.as.array.values = malloc(sizeof(Value) * size);
    
    for (int i = 0; i < size; i++) {
        result.as.array.values[i].type = VALUE_ARRAY;
        result.as.array.values[i].as.array.count = size;
        result.as.array.values[i].as.array.values = malloc(sizeof(Value) * size);
        
        int vec1_len;
        double* vec1 = value_array_to_doubles(&dataset->as.array.values[i], &vec1_len);
        
        for (int j = 0; j < size; j++) {
            if (i == j) {
                // Distance to self is 0
                result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
                result.as.array.values[i].as.array.values[j].as.number = 0.0;
            } else {
                int vec2_len;
                double* vec2 = value_array_to_doubles(&dataset->as.array.values[j], &vec2_len);
                
                double distance = 0.0;
                if (vec1 && vec2 && vec1_len == vec2_len) {
                    distance = calculate_distance(vec1, vec2, vec1_len, metric);
                }
                
                result.as.array.values[i].as.array.values[j].type = VALUE_NUMBER;
                result.as.array.values[i].as.array.values[j].as.number = distance;
                
                free(vec2);
            }
        }
        
        free(vec1);
    }
    
    return result;
}
