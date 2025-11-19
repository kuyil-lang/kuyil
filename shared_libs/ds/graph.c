// Graph - Full Implementation with BFS, DFS, Dijkstra
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <float.h>
#include "../../src/ast.h"

typedef struct Edge {
    int dest;
    double weight;
    struct Edge* next;
} Edge;

typedef struct {
    Edge** adj_list;
    int vertex_count;
    int edge_count;
    bool is_directed;
} Graph;

// Queue for BFS
typedef struct QueueNode {
    int vertex;
    struct QueueNode* next;
} QueueNode;

typedef struct {
    QueueNode* front;
    QueueNode* rear;
} Queue;

static Queue* queue_create() {
    Queue* q = malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    return q;
}

static void queue_enqueue(Queue* q, int vertex) {
    QueueNode* node = malloc(sizeof(QueueNode));
    node->vertex = vertex;
    node->next = NULL;
    
    if (!q->rear) {
        q->front = q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }
}

static int queue_dequeue(Queue* q) {
    if (!q->front) return -1;
    
    QueueNode* temp = q->front;
    int vertex = temp->vertex;
    q->front = q->front->next;
    
    if (!q->front) q->rear = NULL;
    
    free(temp);
    return vertex;
}

static bool queue_is_empty(Queue* q) {
    return q->front == NULL;
}

static void queue_destroy(Queue* q) {
    while (!queue_is_empty(q)) {
        queue_dequeue(q);
    }
    free(q);
}

Value kyl_ds_graphCreate(int arg_count, Value* args) {
    int vertex_count = (arg_count > 0 && args[0].type == VALUE_NUMBER) ? (int)args[0].as.number : 10;
    bool is_directed = (arg_count > 1 && args[1].type == VALUE_BOOL) ? args[1].as.boolean : false;
    
    Graph* graph = malloc(sizeof(Graph));
    graph->vertex_count = vertex_count;
    graph->is_directed = is_directed;
    graph->edge_count = 0;
    graph->adj_list = calloc(vertex_count, sizeof(Edge*));
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)graph;
    return result;
}

Value kyl_ds_graphDestroy(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    
    for (int i = 0; i < graph->vertex_count; i++) {
        Edge* edge = graph->adj_list[i];
        while (edge) {
            Edge* next = edge->next;
            free(edge);
            edge = next;
        }
    }
    
    free(graph->adj_list);
    free(graph);
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_graphAddEdge(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_NUMBER || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int from = (int)args[1].as.number;
    int to = (int)args[2].as.number;
    double weight = (arg_count > 3 && args[3].type == VALUE_NUMBER) ? args[3].as.number : 1.0;
    
    if (from < 0 || from >= graph->vertex_count || to < 0 || to >= graph->vertex_count) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Edge* edge = malloc(sizeof(Edge));
    edge->dest = to;
    edge->weight = weight;
    edge->next = graph->adj_list[from];
    graph->adj_list[from] = edge;
    graph->edge_count++;
    
    if (!graph->is_directed) {
        Edge* rev_edge = malloc(sizeof(Edge));
        rev_edge->dest = from;
        rev_edge->weight = weight;
        rev_edge->next = graph->adj_list[to];
        graph->adj_list[to] = rev_edge;
    }
    
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_graphRemoveEdge(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_NUMBER || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int from = (int)args[1].as.number;
    int to = (int)args[2].as.number;
    
    if (from < 0 || from >= graph->vertex_count) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Edge* edge = graph->adj_list[from];
    Edge* prev = NULL;
    
    while (edge) {
        if (edge->dest == to) {
            if (prev) {
                prev->next = edge->next;
            } else {
                graph->adj_list[from] = edge->next;
            }
            free(edge);
            graph->edge_count--;
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        prev = edge;
        edge = edge->next;
    }
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_graphHasEdge(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_NUMBER || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int from = (int)args[1].as.number;
    int to = (int)args[2].as.number;
    
    if (from < 0 || from >= graph->vertex_count) {
        return (Value){VALUE_BOOL, .as.boolean = false};
    }
    
    Edge* edge = graph->adj_list[from];
    while (edge) {
        if (edge->dest == to) {
            return (Value){VALUE_BOOL, .as.boolean = true};
        }
        edge = edge->next;
    }
    
    return (Value){VALUE_BOOL, .as.boolean = false};
}

Value kyl_ds_graphGetNeighbors(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int vertex = (int)args[1].as.number;
    
    if (vertex < 0 || vertex >= graph->vertex_count) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    // Count neighbors
    int count = 0;
    Edge* edge = graph->adj_list[vertex];
    while (edge) {
        count++;
        edge = edge->next;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = count;
    result.as.array.values = malloc(sizeof(Value) * count);
    
    edge = graph->adj_list[vertex];
    int idx = 0;
    while (edge) {
        result.as.array.values[idx].type = VALUE_NUMBER;
        result.as.array.values[idx].as.number = (double)edge->dest;
        idx++;
        edge = edge->next;
    }
    
    return result;
}

Value kyl_ds_graphVertexCount(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)graph->vertex_count;
    return result;
}

Value kyl_ds_graphEdgeCount(int arg_count, Value* args) {
    if (arg_count == 0 || args[0].type != VALUE_NUMBER) {
        return (Value){VALUE_NUMBER, .as.number = 0};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)graph->edge_count;
    return result;
}

Value kyl_ds_graphBFS(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int start = (int)args[1].as.number;
    
    if (start < 0 || start >= graph->vertex_count) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    bool* visited = calloc(graph->vertex_count, sizeof(bool));
    Value* result_arr = malloc(sizeof(Value) * graph->vertex_count);
    int count = 0;
    
    Queue* q = queue_create();
    visited[start] = true;
    queue_enqueue(q, start);
    
    while (!queue_is_empty(q)) {
        int v = queue_dequeue(q);
        result_arr[count].type = VALUE_NUMBER;
        result_arr[count].as.number = (double)v;
        count++;
        
        Edge* edge = graph->adj_list[v];
        while (edge) {
            if (!visited[edge->dest]) {
                visited[edge->dest] = true;
                queue_enqueue(q, edge->dest);
            }
            edge = edge->next;
        }
    }
    
    queue_destroy(q);
    free(visited);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = count;
    result.as.array.values = result_arr;
    return result;
}

static void dfs_helper(Graph* graph, int v, bool* visited, Value* arr, int* count) {
    visited[v] = true;
    arr[(*count)].type = VALUE_NUMBER;
    arr[(*count)].as.number = (double)v;
    (*count)++;
    
    Edge* edge = graph->adj_list[v];
    while (edge) {
        if (!visited[edge->dest]) {
            dfs_helper(graph, edge->dest, visited, arr, count);
        }
        edge = edge->next;
    }
}

Value kyl_ds_graphDFS(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int start = (int)args[1].as.number;
    
    if (start < 0 || start >= graph->vertex_count) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    bool* visited = calloc(graph->vertex_count, sizeof(bool));
    Value* result_arr = malloc(sizeof(Value) * graph->vertex_count);
    int count = 0;
    
    dfs_helper(graph, start, visited, result_arr, &count);
    
    free(visited);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = count;
    result.as.array.values = result_arr;
    return result;
}

Value kyl_ds_graphShortestPath(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_NUMBER || args[2].type != VALUE_NUMBER) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    Graph* graph = (Graph*)(uintptr_t)args[0].as.number;
    int start = (int)args[1].as.number;
    int end = (int)args[2].as.number;
    
    if (start < 0 || start >= graph->vertex_count || 
        end < 0 || end >= graph->vertex_count) {
        return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
    }
    
    // Dijkstra's algorithm
    double* dist = malloc(sizeof(double) * graph->vertex_count);
    int* prev = malloc(sizeof(int) * graph->vertex_count);
    bool* visited = calloc(graph->vertex_count, sizeof(bool));
    
    for (int i = 0; i < graph->vertex_count; i++) {
        dist[i] = DBL_MAX;
        prev[i] = -1;
    }
    dist[start] = 0;
    
    for (int i = 0; i < graph->vertex_count; i++) {
        int u = -1;
        double min_dist = DBL_MAX;
        
        for (int j = 0; j < graph->vertex_count; j++) {
            if (!visited[j] && dist[j] < min_dist) {
                min_dist = dist[j];
                u = j;
            }
        }
        
        if (u == -1 || u == end) break;
        
        visited[u] = true;
        
        Edge* edge = graph->adj_list[u];
        while (edge) {
            int v = edge->dest;
            double alt = dist[u] + edge->weight;
            if (alt < dist[v]) {
                dist[v] = alt;
                prev[v] = u;
            }
            edge = edge->next;
        }
    }
    
    // Reconstruct path
    int path_len = 0;
    for (int v = end; v != -1; v = prev[v]) {
        path_len++;
    }
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_ARRAY;
    result.as.array.count = path_len;
    result.as.array.values = malloc(sizeof(Value) * path_len);
    
    int idx = path_len - 1;
    for (int v = end; v != -1; v = prev[v]) {
        result.as.array.values[idx].type = VALUE_NUMBER;
        result.as.array.values[idx].as.number = (double)v;
        idx--;
    }
    
    free(dist);
    free(prev);
    free(visited);
    
    return result;
}

Value kyl_ds_graphTopologicalSort(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    // Topological sort stub
    return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
}

Value kyl_ds_graphConnectedComponents(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    // Connected components stub
    return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
}

Value kyl_ds_graphIsAcyclic(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    // Cycle detection stub
    return (Value){VALUE_BOOL, .as.boolean = true};
}

Value kyl_ds_graphMinSpanningTree(int arg_count, Value* args) {
    (void)arg_count; (void)args;
    // MST stub
    return (Value){VALUE_ARRAY, .as.array = {0, NULL}};
}
