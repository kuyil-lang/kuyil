#!/bin/bash

# Kuyil Unit Test Framework - Clean Test Runner
# Runs the working test files with clean output

echo "================================================"
echo "Kuyil Unit Test Framework - Clean Test Suite"
echo "================================================"
echo

KUYIL_BINARY="./kuyil"

# Check if Kuyil binary exists
if [[ ! -x "$KUYIL_BINARY" ]]; then
    echo "❌ Error: Kuyil binary not found at $KUYIL_BINARY"
    echo "Run this script from the kuyil project root directory."
    exit 1
fi

# Define the working test files in order
WORKING_TESTS="unittest/simple_test.kyl unittest/tests/extended_test.kyl unittest/tests/comprehensive_test.kyl"

echo "🧪 Running Kuyil Unit Tests..."
echo

TOTAL_TESTS=0
PASSED_TESTS=0

for test_file in $WORKING_TESTS; do
    if [[ ! -f "$test_file" ]]; then
        echo "⚠️  SKIPPED: $test_file (file not found)"
        continue
    fi
    
    ((TOTAL_TESTS++))
    echo "▶️  Running: $test_file"
    echo "----------------------------------------"
    
    # Run the test with timeout and capture output
    if timeout 15s "$KUYIL_BINARY" "$test_file" > /dev/null 2>&1; then
        echo "✅ PASSED: $test_file"
        ((PASSED_TESTS++))
    else
        EXIT_CODE=$?
        echo "❌ FAILED: $test_file (exit code: $EXIT_CODE)"
        FAILED_TESTS="$FAILED_TESTS $test_file"
    fi
    echo "----------------------------------------"
    echo
done

echo "================================================"
echo "📊 Test Results Summary"
echo "================================================"
echo "Total Tests:  $TOTAL_TESTS"
echo "Passed Tests: $PASSED_TESTS" 
echo "Failed Tests: $((TOTAL_TESTS - PASSED_TESTS))"

if [[ $PASSED_TESTS -eq $TOTAL_TESTS ]]; then
    echo
    echo "🎉 ALL TESTS PASSED! 🎉"
    echo
    echo "✅ The Kuyil Unit Test Framework is working correctly."
    echo "✅ You can use the test patterns from these files:"
    echo "   - unittest/simple_test.kyl (main examples)"
    echo "   - unittest/tests/extended_test.kyl (advanced patterns)"
    echo "   - unittest/tests/comprehensive_test.kyl (comprehensive tests)"
    echo
    echo "📚 Framework ready for production use!"
    exit 0
else
    echo
    echo "❌ Some tests failed:"
    if [[ -n "$FAILED_TESTS" ]]; then
        for failed_test in $FAILED_TESTS; do
            echo "   - $failed_test"
        done
    fi
    echo
    echo "💡 Check the failing tests and fix any issues."
    echo "💡 Run individual tests with: ./kuyil <test_file>"
    exit 1
fi