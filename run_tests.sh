#!/bin/bash

# Kuyil Test Runner
echo "=== Kuyil Language Test Suite ==="
echo

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# If legacy tests directory is missing, delegate to the unit test runner
if [ ! -d "tests" ]; then
    echo "Legacy tests/ directory not found; delegating to unittest runner..."
    if [ -x "./unittest/run_tests.sh" ]; then
        ./unittest/run_tests.sh
        exit $?
    else
        echo "unittest/run_tests.sh not found or not executable."
        exit 1
    fi
fi

# Test categories (legacy)
BASIC_TESTS="simple_test comprehensive_test victory_test"
STRING_TESTS="simple_string_test string_functions_test interpolation_test"
DYNAMIC_TESTS="dynamic_execution_test simple_compile_once_test"
DATE_TESTS="date_functions_test timestamp_format_test"
REACTIVE_TESTS="reactive_streams_demo simple_observable_test"

run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .kyl)
    
    printf "${BLUE}Running: ${test_name}${NC}\n"
    
    if ./kuyil "tests/$test_file" > /dev/null 2>&1; then
        printf "${GREEN}✅ PASS: ${test_name}${NC}\n"
        return 0
    else
        printf "${RED}❌ FAIL: ${test_name}${NC}\n"
        return 1
    fi
}

run_test_verbose() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .kyl)
    
    printf "${BLUE}=== Running: ${test_name} ===${NC}\n"
    ./kuyil "tests/$test_file"
    echo
}

# Parse command line arguments
case "$1" in
    "basic"|"")
        echo "Running Basic Language Tests..."
        for test in $BASIC_TESTS; do
            run_test "$test.kyl"
        done
        ;;
    "string")
        echo "Running String Processing Tests..."
        for test in $STRING_TESTS; do
            run_test "$test.kyl"
        done
        ;;
    "dynamic")
        echo "Running Dynamic Execution Tests..."
        for test in $DYNAMIC_TESTS; do
            run_test "$test.kyl"
        done
        ;;
    "date")
        echo "Running Date/Time Tests..."
        for test in $DATE_TESTS; do
            run_test "$test.kyl"
        done
        ;;
    "reactive")
        echo "Running Reactive Programming Tests..."
        for test in $REACTIVE_TESTS; do
            run_test "$test.kyl"
        done
        ;;
    "all")
        echo "Running All Tests..."
        passed=0
        total=0
        
        for category in basic string dynamic date reactive; do
            echo
            printf "${BLUE}=== ${category^^} TESTS ===${NC}\n"
            case $category in
                "basic") tests="$BASIC_TESTS" ;;
                "string") tests="$STRING_TESTS" ;;
                "dynamic") tests="$DYNAMIC_TESTS" ;;
                "date") tests="$DATE_TESTS" ;;
                "reactive") tests="$REACTIVE_TESTS" ;;
            esac
            
            for test in $tests; do
                if run_test "$test.kyl"; then
                    ((passed++))
                fi
                ((total++))
            done
        done
        
        echo
        printf "${BLUE}=== TEST SUMMARY ===${NC}\n"
        printf "Passed: ${GREEN}$passed${NC}/$total\n"
        
        if [ $passed -eq $total ]; then
            printf "${GREEN}🎉 All tests passed!${NC}\n"
        else
            printf "${RED}⚠️  Some tests failed${NC}\n"
        fi
        ;;
    "demo")
        echo "Running Interactive Demonstrations..."
        run_test_verbose "dynamic_execution_demo.kyl"
        ;;
    *)
        echo "Kuyil Test Runner"
        echo
        echo "Usage: $0 [category]"
        echo
        echo "Categories:"
        echo "  basic    - Core language functionality"
        echo "  string   - String processing and manipulation"
        echo "  dynamic  - Dynamic script execution"
        echo "  date     - Date and time operations"
        echo "  reactive - Reactive programming features"
        echo "  all      - Run all test categories"
        echo "  demo     - Interactive demonstrations"
        echo
        echo "Examples:"
        echo "  $0           # Run basic tests"
        echo "  $0 dynamic   # Run dynamic execution tests"
        echo "  $0 all       # Run complete test suite"
        echo "  $0 demo      # Show feature demonstrations"
        ;;
esac