// Test VM Pool - Isolated VMs for parallel test execution
// Provides VM isolation for @Test decorated functions

#ifndef TEST_VM_POOL_H
#define TEST_VM_POOL_H

#include "vm.h"
#include <pthread.h>
#include <stdbool.h>

// Test VM structure - isolated VM with test state
typedef struct {
    VM vm;
    bool in_use;
    int test_id;
    char* test_name;
    pthread_mutex_t lock;
} TestVM;

// VM Pool for parallel test execution
typedef struct {
    TestVM* vms;
    int pool_size;
    int active_count;
    pthread_mutex_t pool_lock;
} TestVMPool;

// Mock function registry for testing
typedef struct {
    char* function_name;
    Value return_value;
    Value* when_args;     // Conditional: return value only when args match
    int when_arg_count;
    int call_count;       // Track number of calls
    Value* call_history;  // Store arguments from each call
    int history_capacity;
} MockFunction;

typedef struct {
    MockFunction* mocks;
    int count;
    int capacity;
    pthread_mutex_t lock;
} MockRegistry;

// Pool management
TestVMPool* test_pool_create(int pool_size);
void test_pool_destroy(TestVMPool* pool);
TestVM* test_pool_acquire(TestVMPool* pool);
void test_pool_release(TestVMPool* pool, TestVM* test_vm);

// Mock function management
MockRegistry* mock_registry_create();
void mock_registry_destroy(MockRegistry* registry);
void mock_register(MockRegistry* registry, const char* func_name, Value return_value, 
                  Value* when_args, int when_arg_count);
bool mock_find(MockRegistry* registry, const char* func_name, Value* args, int arg_count,
               Value* out_return_value);
void mock_record_call(MockRegistry* registry, const char* func_name, Value* args, int arg_count);
int mock_registry_get_call_count(MockRegistry* registry, const char* func_name);
void mock_registry_clear(MockRegistry* registry, const char* func_name);
void mock_clear_all(MockRegistry* registry);

// Test execution helpers
bool run_test_in_isolated_vm(TestVMPool* pool, const char* test_name, Function* test_func);

#endif // TEST_VM_POOL_H
