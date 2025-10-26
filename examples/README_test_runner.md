# Examples Test Runner

## Overview
`run_all_examples.sh` is a shell script that automatically tests all Kuyil example files in the `examples/` directory and reports which ones work correctly vs which ones have issues.

## Usage

### Basic Usage
```bash
# Run all examples and get a summary
./run_all_examples.sh
```

### Verbose Mode
```bash
# Run all examples and show their output
./run_all_examples.sh -v
```

### Help
```bash
# Show usage information
./run_all_examples.sh -h
```

## Features

- **Automatic Discovery**: Finds all `.kyl` files in the examples directory
- **Timeout Protection**: Each example has a 30-second timeout to prevent hanging
- **Error Detection**: Detects runtime errors, segfaults, and other failures
- **Color-Coded Output**: Green for working examples, red for broken ones
- **Summary Report**: Shows statistics and lists broken examples with error details
- **Verbose Mode**: Optionally displays the output of each example

## Sample Output

```
=== Kuyil Examples Test Runner ===
Testing all examples in examples/ directory

Found 10 example files to test

Testing core_supports.kyl... ✓ PASSED
Testing file_operations_demo.kyl... ✓ PASSED
Testing logging_demo.kyl... ✓ PASSED
...

=== TEST RESULTS SUMMARY ===

✓ Working Examples (10):
  ✓ core_supports.kyl
  ✓ file_operations_demo.kyl
  ✓ logging_demo.kyl
  ...

=== STATISTICS ===
Total examples: 10
Working: 10
Broken: 0

🎉 All examples are working correctly!
```

## Exit Codes
- `0`: All examples passed
- `1`: One or more examples failed

## Requirements
- Kuyil executable must be built (`make` in the root directory)
- Script must be run from the examples directory or have proper paths set