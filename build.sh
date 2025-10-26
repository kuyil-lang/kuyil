#!/bin/bash

# Kuyil Build Script
# Automated build and test script for Kuyil

set -e  # Exit on any error

echo "================================================"
echo "Kuyil Build Script v1.0.0"
echo "Fast scripting language with HTTP/REST support"
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Install dependencies based on OS
install_dependencies() {
    log_info "Installing dependencies..."
    
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux
        if command_exists apt-get; then
            # Ubuntu/Debian
            sudo apt-get update
            sudo apt-get install -y build-essential libcurl4-openssl-dev
        elif command_exists yum; then
            # CentOS/RHEL
            sudo yum install -y gcc curl-devel
        elif command_exists dnf; then
            # Fedora
            sudo dnf install -y gcc curl-devel
        elif command_exists pacman; then
            # Arch Linux
            sudo pacman -S gcc curl
        else
            log_warning "Unknown Linux distribution. Please install gcc and libcurl manually."
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if command_exists brew; then
            brew install curl
        else
            log_error "Homebrew not found. Please install Homebrew first."
            exit 1
        fi
    else
        log_warning "Unknown operating system. Please install dependencies manually."
    fi
}

# Build Kuyil
build_kuyil() {
    log_info "Building Kuyil..."
    
    if [ ! -f "Makefile" ]; then
        log_error "Makefile not found. Please run this script from the Kuyil root directory."
        exit 1
    fi
    
    make clean
    make all
    
    if [ -f "kuyil" ]; then
        log_success "Kuyil built successfully!"
    else
        log_error "Build failed!"
        exit 1
    fi
}

# Run tests
run_tests() {
    log_info "Running tests..."
    
    # Basic functionality test
    echo 'print("Test: Basic functionality")' | ./kuyil -
    
    # Run example scripts
    if [ -d "examples" ]; then
        log_info "Running example scripts..."
        ./kuyil examples/hello.kyl
    fi
    
    log_success "All tests passed!"
}

# Performance benchmark
run_benchmark() {
    log_info "Running performance benchmark..."
    
    echo "Fibonacci benchmark (computing fib(30)):"
    echo 'fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) } print("fib(30) = " + fib(30))' | time ./kuyil -
    
    log_success "Benchmark completed!"
}

# Memory test
run_memory_test() {
    if command_exists valgrind; then
        log_info "Running memory leak test..."
        echo 'print("Memory test")' | valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./kuyil - 2>&1 | tee valgrind.log
        
        if grep -q "ERROR SUMMARY: 0 errors" valgrind.log; then
            log_success "No memory leaks detected!"
            rm -f valgrind.log
        else
            log_warning "Memory issues detected. Check valgrind.log for details."
        fi
    else
        log_warning "Valgrind not found. Skipping memory test."
    fi
}

# Code quality check
run_code_analysis() {
    if command_exists cppcheck; then
        log_info "Running static code analysis..."
        cppcheck --enable=all --std=c99 --error-exitcode=1 src/ 2>&1 | tee cppcheck.log
        
        if [ $? -eq 0 ]; then
            log_success "No static analysis issues found!"
            rm -f cppcheck.log
        else
            log_warning "Code analysis issues found. Check cppcheck.log for details."
        fi
    else
        log_warning "cppcheck not found. Skipping static analysis."
    fi
}

# Install system-wide
install_system() {
    log_info "Installing Kuyil system-wide..."
    make install
    log_success "Kuyil installed to /usr/local/bin/kuyil"
}

# Create distribution package
create_distribution() {
    log_info "Creating distribution package..."
    make dist
    log_success "Distribution package created!"
}

# Show usage
show_usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -h, --help        Show this help message"
    echo "  -d, --deps        Install dependencies"
    echo "  -b, --build       Build Kuyil"
    echo "  -t, --test        Run tests"
    echo "  -p, --perf        Run performance benchmark"
    echo "  -m, --memory      Run memory leak test"
    echo "  -a, --analyze     Run code analysis"
    echo "  -i, --install     Install system-wide"
    echo "  -D, --dist        Create distribution package"
    echo "  --full           Run full build pipeline (deps, build, test, analysis)"
    echo ""
    echo "Examples:"
    echo "  $0 --full         # Complete build and test"
    echo "  $0 -d -b -t       # Install deps, build, and test"
    echo "  $0 --build --install  # Build and install"
}

# Main execution
main() {
    local do_deps=false
    local do_build=false
    local do_test=false
    local do_perf=false
    local do_memory=false
    local do_analyze=false
    local do_install=false
    local do_dist=false
    local do_full=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -d|--deps)
                do_deps=true
                shift
                ;;
            -b|--build)
                do_build=true
                shift
                ;;
            -t|--test)
                do_test=true
                shift
                ;;
            -p|--perf)
                do_perf=true
                shift
                ;;
            -m|--memory)
                do_memory=true
                shift
                ;;
            -a|--analyze)
                do_analyze=true
                shift
                ;;
            -i|--install)
                do_install=true
                shift
                ;;
            -D|--dist)
                do_dist=true
                shift
                ;;
            --full)
                do_full=true
                shift
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # If no options specified, show usage
    if [ "$do_deps" = false ] && [ "$do_build" = false ] && [ "$do_test" = false ] && \
       [ "$do_perf" = false ] && [ "$do_memory" = false ] && [ "$do_analyze" = false ] && \
       [ "$do_install" = false ] && [ "$do_dist" = false ] && [ "$do_full" = false ]; then
        show_usage
        exit 1
    fi
    
    # Full pipeline
    if [ "$do_full" = true ]; then
        do_deps=true
        do_build=true
        do_test=true
        do_analyze=true
        do_memory=true
        do_perf=true
    fi
    
    # Execute requested actions
    if [ "$do_deps" = true ]; then
        install_dependencies
    fi
    
    if [ "$do_build" = true ]; then
        build_kuyil
    fi
    
    if [ "$do_test" = true ]; then
        run_tests
    fi
    
    if [ "$do_analyze" = true ]; then
        run_code_analysis
    fi
    
    if [ "$do_memory" = true ]; then
        run_memory_test
    fi
    
    if [ "$do_perf" = true ]; then
        run_benchmark
    fi
    
    if [ "$do_install" = true ]; then
        install_system
    fi
    
    if [ "$do_dist" = true ]; then
        create_distribution
    fi
    
    log_success "Build script completed successfully!"
}

# Run main function with all arguments
main "$@"