/**
 * Unit tests for Avatar Runtime
 * Tests recursion, stack management, and frame handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../src/vm.h"
#include "../../src/avatar_runtime.h"

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static void run_test_##name() { \
        printf("Running test: %s ... ", #name); \
        fflush(stdout); \
        tests_run++; \
        test_##name(); \
    } \
    static void test_##name()

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAILED\n  Expected: %g, Got: %g - %s\n", (double)(expected), (double)(actual), msg); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            printf("FAILED\n  %s\n", msg); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASSED\n"); \
        tests_passed++; \
    } while(0)

// Helper to compile and run Kuyil code
static Value run_kuyil_code(const char* source, VM* vm) {
    InterpretResult result = vm_interpret(vm, source);
    
    if (result != INTERPRET_OK) {
        fprintf(stderr, "Runtime error\n");
        Value val = {VALUE_NIL};
        return val;
    }
    
    // Get result from stack top
    if (vm->stack_top > vm->stack) {
        return vm_pop(vm);
    }
    
    Value val = {VALUE_NIL};
    return val;
}

// Helper to run code in avatar - returns script handle for later execution
static int compile_for_avatar(const char* source) {
    return vm_compile_script(source, 0);
}

// ============================================================================
// TESTS
// ============================================================================

TEST(simple_factorial) {
    const char* code = 
        "fn factorial(n) {\n"
        "    if (n <= 1) { return 1 }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "factorial(5)";
    
    VM vm;
    vm_init(&vm);
    Value result = run_kuyil_code(code, &vm);
    
    ASSERT_EQ(VALUE_NUMBER, result.type, "Result should be a number");
    ASSERT_EQ(120, result.as.number, "factorial(5) should be 120");
    
    vm_free(&vm);
    TEST_PASS();
}

TEST(avatar_factorial) {
    // Write test script to file and run it
    const char* script = 
        "fn factorial(n) {\n"
        "    if (n <= 1) { return 1 }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "let av = avatar factorial(5)\n"
        "await av";
    
    VM vm;
    vm_init(&vm);
    
    InterpretResult result = vm_interpret(&vm, script);
    ASSERT_EQ(INTERPRET_OK, result, "Script should execute successfully");
    
    // Get result from stack
    Value val = vm_pop(&vm);
    ASSERT_EQ(VALUE_NUMBER, val.type, "Result should be a number");
    ASSERT_EQ(120, val.as.number, "Avatar factorial(5) should be 120");
    
    vm_free(&vm);
    TEST_PASS();
}

TEST(avatar_countdown_recursion) {
    // BUG REPRODUCTION: countdown(3) returns 1 instead of 3
    const char* script = 
        "fn countdown(n) {\n"
        "    if (n <= 0) { return 0 }\n"
        "    return 1 + countdown(n - 1)\n"
        "}\n"
        "let av = avatar countdown(3)\n"
        "await av";
    
    VM vm;
    vm_init(&vm);
    
    InterpretResult result = vm_interpret(&vm, script);
    ASSERT_EQ(INTERPRET_OK, result, "Script should execute successfully");
    
    Value val = vm_pop(&vm);
    ASSERT_EQ(VALUE_NUMBER, val.type, "Result should be a number");
    ASSERT_EQ(3, val.as.number, "Avatar countdown(3) should be 3 (BUG: currently returns 1)");
    
    vm_free(&vm);
    TEST_PASS();
}

TEST(avatar_multiply_recursive) {
    // BUG REPRODUCTION: multiply_recursive(3) returns wrong value
    const char* script = 
        "fn multiply_recursive(n) {\n"
        "    if (n <= 0) { return 1 }\n"
        "    return 2 * multiply_recursive(n - 1)\n"
        "}\n"
        "let av = avatar multiply_recursive(3)\n"
        "await av";
    
    VM vm;
    vm_init(&vm);
    
    InterpretResult result = vm_interpret(&vm, script);
    ASSERT_EQ(INTERPRET_OK, result, "Script should execute successfully");
    
    Value val = vm_pop(&vm);
    ASSERT_EQ(VALUE_NUMBER, val.type, "Result should be a number");
    ASSERT_EQ(8, val.as.number, "Avatar multiply_recursive(3) should be 8 (2^3)");
    
    vm_free(&vm);
    TEST_PASS();
}

TEST(avatar_fibonacci) {
    const char* script = 
        "fn fibonacci(n) {\n"
        "    if (n <= 1) { return n }\n"
        "    return fibonacci(n - 1) + fibonacci(n - 2)\n"
        "}\n"
        "let av = avatar fibonacci(7)\n"
        "await av";
    
    VM vm;
    vm_init(&vm);
    
    InterpretResult result = vm_interpret(&vm, script);
    ASSERT_EQ(INTERPRET_OK, result, "Script should execute successfully");
    
    Value val = vm_pop(&vm);
    ASSERT_EQ(VALUE_NUMBER, val.type, "Result should be a number");
    ASSERT_EQ(13, val.as.number, "Avatar fibonacci(7) should be 13");
    
    vm_free(&vm);
    TEST_PASS();
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(int argc, char** argv) {
    printf("==============================================\n");
    printf("  Avatar Runtime Unit Tests\n");
    printf("==============================================\n\n");
    
    // Run tests
    run_test_simple_factorial();
    run_test_avatar_factorial();
    run_test_avatar_countdown_recursion();
    run_test_avatar_multiply_recursive();
    run_test_avatar_fibonacci();
    
    // Summary
    printf("\n==============================================\n");
    printf("  Test Results\n");
    printf("==============================================\n");
    printf("Total:  %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("==============================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
