# Dynamic Script Execution in Kuyil

## Overview

Kuyil supports dynamic compilation and execution of scripts at runtime with granular library access control. This enables secure sandboxed execution, plugin systems, and runtime code evaluation.

## Function Signature

```kuyil
execute_script(source_code, library_flags, param0, param1, ...)
```

## Parameters

- **`source_code`** (string): The Kuyil script source code to compile and execute
- **`library_flags`** (number): Bitwise flags controlling which libraries are available
- **`param0, param1, ...`** (any): Input parameters accessible as `param0`, `param1`, etc. in the script

## Library Flags

| Flag | Value | Available Functions |
|------|-------|-------------------|
| `LIBRARY_CORE` | 1 | Basic operations: `print`, variables, arithmetic |
| `LIBRARY_STRING` | 2 | `str_length`, `str_upper`, `str_lower`, `str_trim`, `str_contains`, `str_replace`, `to_string` |
| `LIBRARY_MATH` | 4 | `to_number`, `math_abs`, `math_floor`, `math_ceil`, `math_round` |
| `LIBRARY_DATE` | 8 | `date_now`, `datetime_now`, `date_add`, `date_sub`, `date_diff`, `date_unix`, `date_from_unix`, `date_iso`, `date_format`, `date_parse` |
| `LIBRARY_LOG` | 16 | `log_fatal`, `log_error`, `log_warning`, `log_info`, `log_debug` |
| `LIBRARY_ENV` | 32 | `getenv`, `setenv` |
| `LIBRARY_ALL` | 255 | All available functions |

Combine flags using addition: `LIBRARY_CORE + LIBRARY_STRING + LIBRARY_MATH`

## Special Variables

- **`param0, param1, ...`**: Input parameters passed to the script
- **`param_count`**: Number of input parameters provided

## Return Value

The function returns the result of the last expression in the script, or `nil` if:
- The script fails to compile or execute
- No value is left on the execution stack
- An error occurs during execution

## Examples

### Basic Calculator

```kuyil
let calc_script = `
    let sum = param0 + param1
    let product = param0 * param1
    print("Sum: " + to_string(sum))
    print("Product: " + to_string(product))
`

let libraries = LIBRARY_CORE + LIBRARY_STRING
execute_script(calc_script, libraries, 10, 5)
```

### String Processing

```kuyil
let text_script = `
    let input = param0
    let upper = str_upper(input)
    let length = str_length(input)
    print("Input: " + input)
    print("Upper: " + upper)
    print("Length: " + to_string(length))
`

execute_script(text_script, LIBRARY_CORE + LIBRARY_STRING, "Hello World")
```

### Secure Sandboxing

```kuyil
let untrusted_script = `
    // This will fail - no environment access
    let user = getenv("USER")
    print("User: " + user)
`

// Execute with limited permissions - will fail safely
execute_script(untrusted_script, LIBRARY_CORE)  // ❌ Fails

// Execute with proper permissions
execute_script(untrusted_script, LIBRARY_CORE + LIBRARY_ENV)  // ✅ Works
```

### Date Processing

```kuyil
let date_script = `
    let today = date_now()
    let timestamp = date_unix()
    let future = date_add(today, 30, "days")
    print("Today: " + today)
    print("Timestamp: " + to_string(timestamp))
    print("Future: " + future)
`

execute_script(date_script, LIBRARY_CORE + LIBRARY_STRING + LIBRARY_DATE)
```

## Security Features

1. **Library Isolation**: Scripts can only access explicitly allowed function libraries
2. **Memory Isolation**: Each script runs in its own VM context
3. **Error Containment**: Script failures don't crash the host program
4. **Resource Limits**: Compilation limits (1000 tokens max) prevent resource exhaustion

## Use Cases

- **Plugin Systems**: Allow user plugins with restricted API access
- **Configuration Engines**: Dynamic rule evaluation with sandboxing
- **User Script Evaluation**: Safe execution of user-provided code
- **Educational Environments**: Programming tutorials with limited function access
- **Microservices**: Dynamic request processing with security boundaries
- **A/B Testing**: Runtime feature toggles and experiment logic

## Implementation Details

- Scripts are fully compiled to bytecode before execution
- Each execution creates an isolated VM instance
- Library restrictions are enforced at the VM initialization level
- Parameters are injected as global variables (`param0`, `param1`, etc.)
- Memory management is handled automatically

## Error Handling

Dynamic scripts can fail at three stages:

1. **Lexical Error**: Invalid syntax in source code
2. **Parse Error**: Valid tokens but invalid grammar
3. **Runtime Error**: Valid code but execution failure (undefined variables, etc.)

All errors are contained within the dynamic execution and return `nil` to the caller.

## Performance Characteristics

- **Compilation Overhead**: Each script is compiled from source each time
- **Memory Usage**: Isolated VM instances use separate memory
- **Execution Speed**: Comparable to normal Kuyil script execution
- **Security Cost**: Library filtering adds minimal overhead

For high-performance scenarios, consider pre-compiling scripts or caching compiled bytecode.