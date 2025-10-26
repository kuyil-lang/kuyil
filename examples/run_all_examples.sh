#!/bin/bash

# Kuyil Examples Test Runner
# Runs all example files and reports which ones are broken
#
# Usage:
#   ./run_all_examples.sh           # Run all examples
#   ./run_all_examples.sh -v        # Verbose mode (show output)
#   ./run_all_examples.sh -h        # Show help

set -e

# Parse command line arguments
VERBOSE=false
SHOW_HELP=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Show help if requested
if [ "$SHOW_HELP" = true ]; then
    echo "Kuyil Examples Test Runner"
    echo ""
    echo "Usage:"
    echo "  ./run_all_examples.sh           Run all examples"
    echo "  ./run_all_examples.sh -v        Verbose mode (show output)"
    echo "  ./run_all_examples.sh -h        Show this help"
    echo ""
    echo "This script tests all .kyl files in the examples/ directory"
    echo "and reports which ones execute successfully vs which ones fail."
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Navigate to kuyil root directory
cd "$(dirname "$0")/.."

# Check if kuyil executable exists
if [ ! -f "./kuyil" ]; then
    echo -e "${RED}Error: kuyil executable not found in $(pwd)${NC}"
    echo "Please run 'make' to build the project first"
    exit 1
fi

echo -e "${BLUE}=== Kuyil Examples Test Runner ===${NC}"
echo -e "${BLUE}Testing all examples in examples/ directory${NC}"
echo ""

# Arrays to track results
declare -a working_examples=()
declare -a broken_examples=()
declare -a error_details=()

# Get all .kyl files in examples directory
examples_dir="./examples"
kyl_files=($(find "$examples_dir" -name "*.kyl" -type f | sort))

if [ ${#kyl_files[@]} -eq 0 ]; then
    echo -e "${YELLOW}No .kyl files found in $examples_dir${NC}"
    exit 0
fi

echo -e "${BLUE}Found ${#kyl_files[@]} example files to test${NC}"
echo ""

# Function to run a single example and capture result
test_example() {
    local file="$1"
    local basename_file=$(basename "$file")
    
    echo -n "Testing $basename_file... "
    
    # Create temporary files for output and error
    local temp_out=$(mktemp)
    local temp_err=$(mktemp)
    
    # Run the example with timeout
    if timeout 30s ./kuyil "$file" > "$temp_out" 2> "$temp_err"; then
        local exit_code=$?
        
        # Check for specific error patterns in stderr only (not normal log output)
        if grep -q "FATAL\|Segmentation fault\|core dumped\|Aborted\|Error:" "$temp_err" 2>/dev/null; then
            echo -e "${RED}✗ FAILED (runtime error)${NC}"
            broken_examples+=("$basename_file")
            error_details+=("$basename_file: Runtime error detected")
        else
            echo -e "${GREEN}✓ PASSED${NC}"
            working_examples+=("$basename_file")
            
            # Show output in verbose mode
            if [ "$VERBOSE" = true ]; then
                echo -e "${BLUE}--- Output for $basename_file ---${NC}"
                cat "$temp_out" | head -20
                echo -e "${BLUE}--- End output ---${NC}"
                echo ""
            fi
        fi
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            echo -e "${RED}✗ FAILED (timeout)${NC}"
            broken_examples+=("$basename_file")
            error_details+=("$basename_file: Execution timeout (>30s)")
        else
            echo -e "${RED}✗ FAILED (exit code: $exit_code)${NC}"
            broken_examples+=("$basename_file")
            
            # Capture error details
            local error_msg=""
            if [ -s "$temp_err" ]; then
                error_msg=$(head -n 3 "$temp_err" | tr '\n' ' ')
            fi
            error_details+=("$basename_file: Exit code $exit_code - $error_msg")
        fi
    fi
    
    # Clean up temp files
    rm -f "$temp_out" "$temp_err"
}

# Test each example file
for file in "${kyl_files[@]}"; do
    test_example "$file"
done

echo ""
echo -e "${BLUE}=== TEST RESULTS SUMMARY ===${NC}"
echo ""

# Report working examples
if [ ${#working_examples[@]} -gt 0 ]; then
    echo -e "${GREEN}✓ Working Examples (${#working_examples[@]}):${NC}"
    for example in "${working_examples[@]}"; do
        echo -e "  ${GREEN}✓${NC} $example"
    done
    echo ""
fi

# Report broken examples
if [ ${#broken_examples[@]} -gt 0 ]; then
    echo -e "${RED}✗ Broken Examples (${#broken_examples[@]}):${NC}"
    for i in "${!broken_examples[@]}"; do
        echo -e "  ${RED}✗${NC} ${broken_examples[$i]}"
    done
    echo ""
    
    echo -e "${YELLOW}Error Details:${NC}"
    for detail in "${error_details[@]}"; do
        echo -e "  ${YELLOW}!${NC} $detail"
    done
    echo ""
fi

# Final statistics
total_examples=${#kyl_files[@]}
working_count=${#working_examples[@]}
broken_count=${#broken_examples[@]}

echo -e "${BLUE}=== STATISTICS ===${NC}"
echo -e "Total examples: $total_examples"
echo -e "${GREEN}Working: $working_count${NC}"
echo -e "${RED}Broken: $broken_count${NC}"

if [ $broken_count -eq 0 ]; then
    echo ""
    echo -e "${GREEN}🎉 All examples are working correctly!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}⚠️  $broken_count example(s) need attention${NC}"
    exit 1
fi