#!/usr/bin/env bash
# Test runner for avatar recursion bugs
# This runs the kuyil interpreter and checks results

# Don't exit on error, we want to run all tests
set +e

KUYIL=../../kuyil
TESTS_PASSED=0
TESTS_FAILED=0

echo "=============================================="
echo "  Avatar Recursion Tests"
echo "=============================================="
echo

# Helper function to run a test
run_test() {
    local test_name="$1"
    local script="$2"
    local expected="$3"
    
    echo -n "Test: $test_name ... "
    
    # Write script to temp file
    local tmpfile=$(mktemp)
    echo "$script" > "$tmpfile"
    
    # Run and capture output
    local output=$(timeout 5 $KUYIL "$tmpfile" 2>&1)
    local exit_code=$?
    rm -f "$tmpfile"
    
    if [ $exit_code -eq 124 ]; then
        local result="TIMEOUT"
    else
        local result=$(echo "$output" | tail -1)
    fi
    
    if [ "$result" = "$expected" ]; then
        echo "PASSED"
        ((TESTS_PASSED++))
    else
        echo "FAILED (expected: $expected, got: $result)"
        ((TESTS_FAILED++))
    fi
}

# Test 1: Simple factorial (should work)
run_test "avatar_factorial" \
    "fn factorial(n) {
        if (n <= 1) { return 1 }
        return n * factorial(n - 1)
    }
    let av = avatar factorial(5)
    print(await av)" \
    "120"

# Test 2: Countdown recursion (BUG - currently returns 1, should return 3)
run_test "avatar_countdown" \
    "fn countdown(n) {
        if (n <= 0) { return 0 }
        return 1 + countdown(n - 1)
    }
    let av = avatar countdown(3)
    print(await av)" \
    "3"

# Test 3: Multiply recursive (BUG - currently returns wrong value)
run_test "avatar_multiply_recursive" \
    "fn multiply_recursive(n) {
        if (n <= 0) { return 1 }
        return 2 * multiply_recursive(n - 1)
    }
    let av = avatar multiply_recursive(3)
    print(await av)" \
    "8"

# Test 4: Fibonacci (BUG)
run_test "avatar_fibonacci" \
    "fn fibonacci(n) {
        if (n <= 1) { return n }
        return fibonacci(n - 1) + fibonacci(n - 2)
    }
    let av = avatar fibonacci(7)
    print(await av)" \
    "13"

# Summary
echo
echo "=============================================="
echo "  Results"
echo "=============================================="
echo "Passed: $TESTS_PASSED"
echo "Failed: $TESTS_FAILED"
echo "Total:  $((TESTS_PASSED + TESTS_FAILED))"
echo "=============================================="

if [ $TESTS_FAILED -gt 0 ]; then
    exit 1
fi
