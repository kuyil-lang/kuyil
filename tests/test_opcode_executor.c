// test_opcode_executor.c - Unit tests for shared opcode executor
#include "../src/opcode_executor.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

// Test framework
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static void run_test_##name() { \
        printf("  Running: %s ... ", #name); \
        fflush(stdout); \
        test_##name(); \
        tests_run++; \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
                   #cond, __FILE__, __LINE__); \
            tests_failed++; \
            tests_passed--; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL\n    Expected %s == %s\n    at %s:%d\n", \
                   #a, #b, __FILE__, __LINE__); \
            tests_failed++; \
            tests_passed--; \
            return; \
        } \
    } while(0)

#define ASSERT_DOUBLE_EQ(a, b) \
    do { \
        if (fabs((a) - (b)) > 0.0001) { \
            printf("FAIL\n    Expected %s == %s (%.4f vs %.4f)\n    at %s:%d\n", \
                   #a, #b, (double)(a), (double)(b), __FILE__, __LINE__); \
            tests_failed++; \
            tests_passed--; \
            return; \
        } \
    } while(0)

// Helper to create test context
static ExecContext create_test_context(Value* stack, Value** stack_top, bool* has_error, char* error_msg) {
    ExecContext ctx = {
        .stack = stack,
        .stack_top = stack_top,
        .stack_capacity = 256,
        .current_frame_slots = NULL,
        .has_error = has_error,
        .error_message = error_msg,
        .error_msg_size = 256,
        .type = EXEC_CTX_VM,
        .vm_ptr = NULL
    };
    return ctx;
}

// ====================
// TESTS: Arithmetic
// ====================

TEST(add_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 5 and 3
    Value a = {.type = VALUE_NUMBER, .as = {.number = 5.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 3.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute ADD
    ASSERT_TRUE(exec_add(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 8.0);
}

TEST(add_strings) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push "hello" and "world"
    Value a = {.type = VALUE_STRING, .as = {.string = "hello"}};
    Value b = {.type = VALUE_STRING, .as = {.string = "world"}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute ADD
    ASSERT_TRUE(exec_add(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_STRING);
    ASSERT_TRUE(strcmp(result.as.string, "helloworld") == 0);
}

TEST(subtract_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 10 and 3
    Value a = {.type = VALUE_NUMBER, .as = {.number = 10.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 3.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute SUBTRACT
    ASSERT_TRUE(exec_subtract(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 7.0);
}

TEST(multiply_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 6 and 7
    Value a = {.type = VALUE_NUMBER, .as = {.number = 6.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 7.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute MULTIPLY
    ASSERT_TRUE(exec_multiply(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 42.0);
}

TEST(divide_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 20 and 4
    Value a = {.type = VALUE_NUMBER, .as = {.number = 20.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 4.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute DIVIDE
    ASSERT_TRUE(exec_divide(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 5.0);
}

TEST(modulo_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 10 and 3
    Value a = {.type = VALUE_NUMBER, .as = {.number = 10.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 3.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute MODULO
    ASSERT_TRUE(exec_modulo(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 1.0);
}

// ====================
// TESTS: Logic
// ====================

TEST(not_true) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push true
    Value v = {.type = VALUE_BOOL, .as = {.boolean = true}};
    exec_push(&ctx, v);
    
    // Execute NOT
    ASSERT_TRUE(exec_not(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, false);
}

TEST(and_operation) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push true and false
    Value a = {.type = VALUE_BOOL, .as = {.boolean = true}};
    Value b = {.type = VALUE_BOOL, .as = {.boolean = false}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute AND
    ASSERT_TRUE(exec_and(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, false);
}

TEST(or_operation) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push true and false
    Value a = {.type = VALUE_BOOL, .as = {.boolean = true}};
    Value b = {.type = VALUE_BOOL, .as = {.boolean = false}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute OR
    ASSERT_TRUE(exec_or(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, true);
}

// ====================
// TESTS: Comparisons
// ====================

TEST(equal_numbers) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 5 and 5
    Value a = {.type = VALUE_NUMBER, .as = {.number = 5.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 5.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute EQUAL
    ASSERT_TRUE(exec_equal(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, true);
}

TEST(greater_than) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 10 and 5
    Value a = {.type = VALUE_NUMBER, .as = {.number = 10.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 5.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute GREATER
    ASSERT_TRUE(exec_greater(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, true);
}

TEST(less_than) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push 3 and 7
    Value a = {.type = VALUE_NUMBER, .as = {.number = 3.0}};
    Value b = {.type = VALUE_NUMBER, .as = {.number = 7.0}};
    exec_push(&ctx, a);
    exec_push(&ctx, b);
    
    // Execute LESS
    ASSERT_TRUE(exec_less(&ctx));
    ASSERT_TRUE(!has_error);
    
    // Check result
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, true);
}

// ====================
// TESTS: Stack Operations
// ====================

TEST(push_pop) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    // Push a value
    Value v = {.type = VALUE_NUMBER, .as = {.number = 42.0}};
    exec_push(&ctx, v);
    
    // Pop it back
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(result.as.number, 42.0);
    ASSERT_TRUE(!has_error);
}

TEST(push_nil) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    exec_push_nil(&ctx);
    
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_NIL);
    ASSERT_TRUE(!has_error);
}

TEST(push_true) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    exec_push_true(&ctx);
    
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, true);
    ASSERT_TRUE(!has_error);
}

TEST(push_false) {
    Value stack[256];
    Value* stack_top = stack;
    bool has_error = false;
    char error_msg[256] = {0};
    
    ExecContext ctx = create_test_context(stack, &stack_top, &has_error, error_msg);
    
    exec_push_false(&ctx);
    
    Value result = exec_pop(&ctx);
    ASSERT_EQ(result.type, VALUE_BOOL);
    ASSERT_EQ(result.as.boolean, false);
    ASSERT_TRUE(!has_error);
}

// ====================
// Main Test Runner
// ====================

int main() {
    printf("\n=== Opcode Executor Unit Tests ===\n\n");
    
    printf("Arithmetic Operations:\n");
    run_test_add_numbers();
    run_test_add_strings();
    run_test_subtract_numbers();
    run_test_multiply_numbers();
    run_test_divide_numbers();
    run_test_modulo_numbers();
    
    printf("\nLogic Operations:\n");
    run_test_not_true();
    run_test_and_operation();
    run_test_or_operation();
    
    printf("\nComparison Operations:\n");
    run_test_equal_numbers();
    run_test_greater_than();
    run_test_less_than();
    
    printf("\nStack Operations:\n");
    run_test_push_pop();
    run_test_push_nil();
    run_test_push_true();
    run_test_push_false();
    
    printf("\n=== Test Results ===\n");
    printf("Total:  %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n✅ All tests PASSED!\n\n");
        return 0;
    } else {
        printf("\n❌ Some tests FAILED!\n\n");
        return 1;
    }
}
