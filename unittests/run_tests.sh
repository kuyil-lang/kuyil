#!/bin/bash
# Run all local variable unit tests

echo "========================================"
echo "Running Local Variable Unit Tests"
echo "========================================"
echo ""

TESTS=(
    "test_locals_basic.kyl"
    "test_locals_multiple.kyl"
    "test_locals_after_scope.kyl"
    "test_locals_function_call.kyl"
    "test_locals_object_call.kyl"
    "test_locals_complex.kyl"
)

PASSED=0
FAILED=0

cd "$(dirname "$0")"

for test in "${TESTS[@]}"; do
    echo "Running: $test"
    echo "----------------------------------------"
    output=$(../kuyil "$test" 2>&1)
    if echo "$output" | grep -q "✓.*PASSED"; then
        echo "✓ PASSED"
        ((PASSED++))
    else
        echo "✗ FAILED"
        echo "$output" | tail -20
        ((FAILED++))
    fi
    echo ""
done

echo "========================================"
echo "Results: $PASSED passed, $FAILED failed"
echo "========================================"

if [ $FAILED -eq 0 ]; then
    exit 0
else
    exit 1
fi
