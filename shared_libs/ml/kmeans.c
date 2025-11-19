// K-Means Clustering Implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "../../src/ast.h"

typedef struct {
    int n_clusters;
    int max_iterations;
    double tolerance;
    
    double** centers;        // Cluster centers [n_clusters][n_features]
    int n_features;
    int n_samples;
    
    int* labels;            // Cluster assignments [n_samples]
    double inertia;         // Sum of squared distances
    int iterations;         // Number of iterations performed
    int converged;          // Whether algorithm converged
} KMeansModel;

// Helper: Extract 2D array from Value dataset
static double** value_to_2d_array(Value* dataset, int* out_samples, int* out_features) {
    if (!dataset || dataset->type != VALUE_ARRAY) {
        *out_samples = *out_features = 0;
        return NULL;
    }
    
    int n_samples = dataset->as.array.count;
    if (n_samples == 0) {
        *out_samples = *out_features = 0;
        return NULL;
    }
    
    // Get number of features from first sample
    if (dataset->as.array.values[0].type != VALUE_ARRAY) {
        *out_samples = *out_features = 0;
        return NULL;
    }
    int n_features = dataset->as.array.values[0].as.array.count;
    
    double** data = malloc(sizeof(double*) * n_samples);
    for (int i = 0; i < n_samples; i++) {
        data[i] = malloc(sizeof(double) * n_features);
        Value* row = &dataset->as.array.values[i];
        
        if (row->type == VALUE_ARRAY) {
            for (int j = 0; j < n_features && j < row->as.array.count; j++) {
                if (row->as.array.values[j].type == VALUE_NUMBER) {
                    data[i][j] = row->as.array.values[j].as.number;
                } else {
                    data[i][j] = 0.0;
                }
            }
        }
    }
    
    *out_samples = n_samples;
    *out_features = n_features;
    return data;
}

// Free 2D array
static void free_2d_array(double** arr, int n_rows) {
    if (arr) {
        for (int i = 0; i < n_rows; i++) {
            free(arr[i]);
        }
        free(arr);
    }
}

// Calculate Euclidean distance
static double euclidean_distance(double* p1, double* p2, int n_features) {
    double sum = 0.0;
    for (int i = 0; i < n_features; i++) {
        double diff = p1[i] - p2[i];
        sum += diff * diff;
    }
    return sqrt(sum);
}

// Initialize centers using k-means++ algorithm
static void initialize_centers_kmeans_plus_plus(KMeansModel* model, double** data, int n_samples) {
    // Choose first center randomly
    srand(time(NULL));
    int first_idx = rand() % n_samples;
    memcpy(model->centers[0], data[first_idx], sizeof(double) * model->n_features);
    
    // Choose remaining centers
    for (int k = 1; k < model->n_clusters; k++) {
        double* distances = malloc(sizeof(double) * n_samples);
        double total_dist = 0.0;
        
        // For each point, find distance to nearest center
        for (int i = 0; i < n_samples; i++) {
            double min_dist = DBL_MAX;
            for (int j = 0; j < k; j++) {
                double dist = euclidean_distance(data[i], model->centers[j], model->n_features);
                if (dist < min_dist) {
                    min_dist = dist;
                }
            }
            distances[i] = min_dist * min_dist; // Square for probability
            total_dist += distances[i];
        }
        
        // Choose next center with probability proportional to distance²
        double r = ((double)rand() / RAND_MAX) * total_dist;
        double cumsum = 0.0;
        int chosen_idx = 0;
        
        for (int i = 0; i < n_samples; i++) {
            cumsum += distances[i];
            if (cumsum >= r) {
                chosen_idx = i;
                break;
            }
        }
        
        memcpy(model->centers[k], data[chosen_idx], sizeof(double) * model->n_features);
        free(distances);
    }
}

// Assign each point to nearest cluster
static int assign_clusters(KMeansModel* model, double** data, int n_samples) {
    int changed = 0;
    
    for (int i = 0; i < n_samples; i++) {
        double min_dist = DBL_MAX;
        int closest_cluster = 0;
        
        for (int k = 0; k < model->n_clusters; k++) {
            double dist = euclidean_distance(data[i], model->centers[k], model->n_features);
            if (dist < min_dist) {
                min_dist = dist;
                closest_cluster = k;
            }
        }
        
        if (model->labels[i] != closest_cluster) {
            changed++;
            model->labels[i] = closest_cluster;
        }
    }
    
    return changed;
}

// Update cluster centers
static void update_centers(KMeansModel* model, double** data, int n_samples) {
    int* counts = calloc(model->n_clusters, sizeof(int));
    
    // Reset centers
    for (int k = 0; k < model->n_clusters; k++) {
        for (int j = 0; j < model->n_features; j++) {
            model->centers[k][j] = 0.0;
        }
    }
    
    // Sum points in each cluster
    for (int i = 0; i < n_samples; i++) {
        int cluster = model->labels[i];
        counts[cluster]++;
        for (int j = 0; j < model->n_features; j++) {
            model->centers[cluster][j] += data[i][j];
        }
    }
    
    // Compute means
    for (int k = 0; k < model->n_clusters; k++) {
        if (counts[k] > 0) {
            for (int j = 0; j < model->n_features; j++) {
                model->centers[k][j] /= counts[k];
            }
        }
    }
    
    free(counts);
}

// Calculate inertia (within-cluster sum of squares)
static void calculate_inertia(KMeansModel* model, double** data, int n_samples) {
    model->inertia = 0.0;
    
    for (int i = 0; i < n_samples; i++) {
        int cluster = model->labels[i];
        double dist = euclidean_distance(data[i], model->centers[cluster], model->n_features);
        model->inertia += dist * dist;
    }
}

// ============================================
// Exported Functions
// ============================================

Value kyl_ml_kmeansCreate(int arg_count, Value* args) {
    if (arg_count < 3) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    int n_clusters = (int)args[0].as.number;
    int max_iterations = (int)args[1].as.number;
    double tolerance = args[2].as.number;
    
    KMeansModel* model = malloc(sizeof(KMeansModel));
    model->n_clusters = n_clusters;
    model->max_iterations = max_iterations;
    model->tolerance = tolerance;
    model->centers = NULL;
    model->n_features = 0;
    model->n_samples = 0;
    model->labels = NULL;
    model->inertia = 0.0;
    model->iterations = 0;
    model->converged = 0;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)model;
    return result;
}

Value kyl_ml_kmeansFit(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    if (!model) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    int n_samples, n_features;
    double** data = value_to_2d_array(&args[1], &n_samples, &n_features);
    
    if (!data) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    model->n_samples = n_samples;
    model->n_features = n_features;
    
    // Allocate centers and labels
    model->centers = malloc(sizeof(double*) * model->n_clusters);
    for (int i = 0; i < model->n_clusters; i++) {
        model->centers[i] = malloc(sizeof(double) * n_features);
    }
    model->labels = malloc(sizeof(int) * n_samples);
    
    // Initialize labels to -1
    for (int i = 0; i < n_samples; i++) {
        model->labels[i] = -1;
    }
    
    // Initialize centers using k-means++
    initialize_centers_kmeans_plus_plus(model, data, n_samples);
    
    // Main K-Means loop
    double prev_inertia = DBL_MAX;
    model->converged = 0;
    
    for (model->iterations = 0; model->iterations < model->max_iterations; model->iterations++) {
        // Assign clusters
        int changed = assign_clusters(model, data, n_samples);
        
        // Update centers
        update_centers(model, data, n_samples);
        
        // Calculate inertia
        calculate_inertia(model, data, n_samples);
        
        // Check convergence
        if (fabs(prev_inertia - model->inertia) < model->tolerance) {
            model->converged = 1;
            break;
        }
        
        prev_inertia = model->inertia;
    }
    
    free_2d_array(data, n_samples);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = model->converged;
    return result;
}

Value kyl_ml_kmeansPredict(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    if (!model || !model->centers) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    int n_samples, n_features;
    double** data = value_to_2d_array(&args[1], &n_samples, &n_features);
    
    if (!data || n_features != model->n_features) {
        free_2d_array(data, n_samples);
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = n_samples;
    result.as.array.values = malloc(sizeof(Value) * n_samples);
    
    for (int i = 0; i < n_samples; i++) {
        double min_dist = DBL_MAX;
        int closest_cluster = 0;
        
        for (int k = 0; k < model->n_clusters; k++) {
            double dist = euclidean_distance(data[i], model->centers[k], model->n_features);
            if (dist < min_dist) {
                min_dist = dist;
                closest_cluster = k;
            }
        }
        
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = closest_cluster;
    }
    
    free_2d_array(data, n_samples);
    return result;
}

Value kyl_ml_kmeansFitPredict(int arg_count, Value* args) {
    kyl_ml_kmeansFit(arg_count, args);
    
    if (!args[0].as.number) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = model->n_samples;
    result.as.array.values = malloc(sizeof(Value) * model->n_samples);
    
    for (int i = 0; i < model->n_samples; i++) {
        result.as.array.values[i].type = VALUE_NUMBER;
        result.as.array.values[i].as.number = model->labels[i];
    }
    
    return result;
}

Value kyl_ml_kmeansGetCenters(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    if (!model || !model->centers) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = model->n_clusters;
    result.as.array.values = malloc(sizeof(Value) * model->n_clusters);
    
    for (int k = 0; k < model->n_clusters; k++) {
        result.as.array.values[k].type = VALUE_ARRAY;
        result.as.array.values[k].as.array.count = model->n_features;
        result.as.array.values[k].as.array.values = malloc(sizeof(Value) * model->n_features);
        
        for (int j = 0; j < model->n_features; j++) {
            result.as.array.values[k].as.array.values[j].type = VALUE_NUMBER;
            result.as.array.values[k].as.array.values[j].as.number = model->centers[k][j];
        }
    }
    
    return result;
}

Value kyl_ml_kmeansGetInertia(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_NUMBER, .as.number = 0.0};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = model ? model->inertia : 0.0;
    return result;
}

Value kyl_ml_kmeansGetIterations(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = model ? model->iterations : 0;
    return result;
}

Value kyl_ml_kmeansSave(int arg_count, Value* args) {
    if (arg_count < 2) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    if (!model || args[1].type != VALUE_STRING) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    const char* filename = args[1].as.string;
    FILE* f = fopen(filename, "w");
    if (!f) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    // Write model parameters
    fprintf(f, "KMEANS_MODEL_V1\n");
    fprintf(f, "n_clusters=%d\n", model->n_clusters);
    fprintf(f, "n_features=%d\n", model->n_features);
    fprintf(f, "max_iterations=%d\n", model->max_iterations);
    fprintf(f, "tolerance=%.10f\n", model->tolerance);
    fprintf(f, "iterations=%d\n", model->iterations);
    fprintf(f, "inertia=%.10f\n", model->inertia);
    fprintf(f, "converged=%d\n", model->converged);
    
    // Write cluster centers
    fprintf(f, "centers:\n");
    for (int i = 0; i < model->n_clusters; i++) {
        for (int j = 0; j < model->n_features; j++) {
            fprintf(f, "%.10f", model->centers[i][j]);
            if (j < model->n_features - 1) fprintf(f, ",");
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    return (Value){VALUE_BOOL, .as.boolean = 1};
}

Value kyl_ml_kmeansLoad(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    const char* filename = args[0].as.string;
    FILE* f = fopen(filename, "r");
    if (!f) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    char header[64];
    if (!fgets(header, sizeof(header), f) || strncmp(header, "KMEANS_MODEL_V1", 15) != 0) {
        fclose(f);
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    // Allocate model
    KMeansModel* model = malloc(sizeof(KMeansModel));
    
    // Read parameters
    fscanf(f, "n_clusters=%d\n", &model->n_clusters);
    fscanf(f, "n_features=%d\n", &model->n_features);
    fscanf(f, "max_iterations=%d\n", &model->max_iterations);
    fscanf(f, "tolerance=%lf\n", &model->tolerance);
    fscanf(f, "iterations=%d\n", &model->iterations);
    fscanf(f, "inertia=%lf\n", &model->inertia);
    fscanf(f, "converged=%d\n", &model->converged);
    
    // Allocate centers
    model->centers = malloc(sizeof(double*) * model->n_clusters);
    for (int i = 0; i < model->n_clusters; i++) {
        model->centers[i] = malloc(sizeof(double) * model->n_features);
    }
    
    // Read centers
    fgets(header, sizeof(header), f); // "centers:\n"
    for (int i = 0; i < model->n_clusters; i++) {
        for (int j = 0; j < model->n_features; j++) {
            fscanf(f, "%lf", &model->centers[i][j]);
            if (j < model->n_features - 1) fgetc(f); // skip comma
        }
        fgetc(f); // skip newline
    }
    
    model->labels = NULL;
    model->n_samples = 0;
    
    fclose(f);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)model;
    return result;
}

Value kyl_ml_kmeansDestroy(int arg_count, Value* args) {
    if (arg_count < 1) {
        return (Value){VALUE_BOOL, .as.boolean = 0};
    }
    
    KMeansModel* model = (KMeansModel*)(uintptr_t)args[0].as.number;
    if (model) {
        free_2d_array(model->centers, model->n_clusters);
        free(model->labels);
        free(model);
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = 1;
    return result;
}
