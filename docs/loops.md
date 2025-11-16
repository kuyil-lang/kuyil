# Loops in Kuyil

Iterate and repeat code execution using loops.

## Table of Contents
- [for Loop](#for-loop)
- [while Loop](#while-loop)
- [break Statement](#break-statement)
- [continue Statement](#continue-statement)
- [Loop Patterns](#loop-patterns)

## for Loop

Execute code a specific number of times.

### Syntax

```kuyil
for initialization; condition; increment {
    // code to repeat
}
```

### Examples

```kuyil
// Basic for loop - count from 0 to 9
for i = 0; i < 10; i++ {
    print(i)
}

// Count from 1 to 10
for i = 1; i <= 10; i++ {
    print(i)
}

// Count backwards
for i = 10; i > 0; i-- {
    print(i)
}

// Count by 2
for i = 0; i < 20; i += 2 {
    print(i)  // 0, 2, 4, 6, 8, 10, 12, 14, 16, 18
}

// Nested loops
for i = 1; i <= 3; i++ {
    for j = 1; j <= 3; j++ {
        print("i=" + i + ", j=" + j)
    }
}

// Loop through array indices
let numbers = [10, 20, 30, 40, 50]
for i = 0; i < 5; i++ {
    print("numbers[" + i + "] = " + numbers[i])
}
```

### Loop Variables

```kuyil
// Loop variable is scoped to the loop
for i = 0; i < 5; i++ {
    print(i)
}
// i is not accessible here

// Multiple operations in increment
for i = 0, j = 10; i < 10; i++, j-- {
    print("i=" + i + ", j=" + j)
}
```

## while Loop

Execute code as long as a condition is true.

### Syntax

```kuyil
while condition {
    // code to repeat
}
```

### Examples

```kuyil
// Basic while loop
let count = 0
while count < 5 {
    print(count)
    count++
}

// User input validation
let input = -1
while input < 0 or input > 100 {
    input = getInput()  // hypothetical function
}

// Process until condition
let x = 100
while x > 1 {
    x = x / 2
    print(x)
}

// Infinite loop (be careful!)
while true {
    print("Running forever")
    // Use break to exit
    break
}
```

### while vs for

**Use for when:**
- You know how many iterations you need
- You need a counter variable
- Iterating through arrays with indices

**Use while when:**
- Condition is based on complex logic
- Number of iterations is unknown
- Waiting for a state change

```kuyil
// Good use of for
for i = 0; i < 100; i++ {
    process(i)
}

// Good use of while
while !isComplete() {
    doWork()
}
```

## break Statement

Exit a loop immediately.

### Syntax

```kuyil
break
```

### Examples

```kuyil
// Exit loop when condition is met
for i = 0; i < 100; i++ {
    if i == 50 {
        break  // Exit when i reaches 50
    }
    print(i)
}

// Find first match
let numbers = [1, 3, 5, 8, 10, 12]
let target = 8
let found = false

for i = 0; i < 6; i++ {
    if numbers[i] == target {
        print("Found at index " + i)
        found = true
        break
    }
}

// Exit infinite loop
let retry_count = 0
while true {
    if tryOperation() {
        break  // Success, exit loop
    }
    
    retry_count++
    if retry_count > 10 {
        print("Max retries reached")
        break
    }
}

// Break in nested loops (only exits inner loop)
for i = 0; i < 5; i++ {
    for j = 0; j < 5; j++ {
        if j == 3 {
            break  // Only exits inner loop
        }
        print("i=" + i + ", j=" + j)
    }
}
```

## continue Statement

Skip the rest of the current iteration and continue with the next.

### Syntax

```kuyil
continue
```

### Examples

```kuyil
// Skip even numbers
for i = 0; i < 10; i++ {
    if i % 2 == 0 {
        continue  // Skip even numbers
    }
    print(i)  // Only prints odd numbers: 1, 3, 5, 7, 9
}

// Skip specific values
let values = [1, -2, 3, -4, 5, -6]
for i = 0; i < 6; i++ {
    if values[i] < 0 {
        continue  // Skip negative values
    }
    print(values[i])  // Only prints: 1, 3, 5
}

// Skip based on condition
for i = 0; i < 20; i++ {
    if i % 3 == 0 {
        continue  // Skip multiples of 3
    }
    print(i)
}

// In while loop
let count = 0
while count < 10 {
    count++
    
    if count == 5 {
        continue  // Skip printing 5
    }
    
    print(count)
}
```

## Loop Patterns

Common patterns and best practices.

### Counting Patterns

```kuyil
// Count up
for i = 0; i < 10; i++ {
    print(i)
}

// Count down
for i = 10; i > 0; i-- {
    print(i)
}

// Count by steps
for i = 0; i < 100; i += 5 {
    print(i)  // 0, 5, 10, 15, ...
}

// Count until condition
let sum = 0
let n = 0
while sum < 100 {
    n++
    sum += n
}
print("Sum reached 100 at n=" + n)
```

### Array Iteration

```kuyil
let fruits = ["apple", "banana", "cherry", "date"]

// Iterate with index
for i = 0; i < 4; i++ {
    print(i + ": " + fruits[i])
}

// Find element
let searchFor = "cherry"
let found_index = -1

for i = 0; i < 4; i++ {
    if fruits[i] == searchFor {
        found_index = i
        break
    }
}

if found_index >= 0 {
    print("Found at index " + found_index)
}
```

### Accumulation Patterns

```kuyil
// Sum of numbers
let sum = 0
for i = 1; i <= 100; i++ {
    sum += i
}
print("Sum: " + sum)  // 5050

// Product of numbers
let product = 1
for i = 1; i <= 10; i++ {
    product *= i
}
print("Product: " + product)  // Factorial of 10

// Build string
let result = ""
for i = 1; i <= 5; i++ {
    result += i + " "
}
print(result)  // "1 2 3 4 5 "
```

### Input Processing

```kuyil
// Process until specific input
while true {
    let input = getUserInput()
    
    if input == "quit" {
        break
    }
    
    if input == "" {
        continue  // Skip empty input
    }
    
    processInput(input)
}

// Retry pattern
let max_retries = 3
let retry = 0

while retry < max_retries {
    if tryConnect() {
        print("Connected!")
        break
    }
    
    retry++
    print("Retry " + retry + "/" + max_retries)
    
    if retry < max_retries {
        sleep(1000)  // Wait before retry
    }
}
```

### Nested Loop Patterns

```kuyil
// Multiplication table
for i = 1; i <= 10; i++ {
    for j = 1; j <= 10; j++ {
        print(i + " x " + j + " = " + (i * j))
    }
}

// 2D grid processing
for row = 0; row < 5; row++ {
    for col = 0; col < 5; col++ {
        print("(" + row + "," + col + ")")
    }
}

// Finding pairs
let nums = [1, 2, 3, 4, 5]
for i = 0; i < 5; i++ {
    for j = i + 1; j < 5; j++ {
        print("Pair: " + nums[i] + ", " + nums[j])
    }
}
```

## Performance Tips

1. **Minimize work inside loops**: Move calculations outside if possible
2. **Use break early**: Exit as soon as you find what you need
3. **Cache array length**: Don't call length functions repeatedly

```kuyil
// Bad: Repeated calculation
for i = 0; i < 100; i++ {
    let expensive = calculateExpensive()  // Calculated 100 times!
    print(expensive + i)
}

// Good: Calculate once
let expensive = calculateExpensive()  // Calculated once
for i = 0; i < 100; i++ {
    print(expensive + i)
}

// Bad: No early exit
let found = false
for i = 0; i < 1000000; i++ {
    if array[i] == target {
        found = true
    }
}

// Good: Early exit with break
let found = false
for i = 0; i < 1000000; i++ {
    if array[i] == target {
        found = true
        break  // Stop immediately
    }
}
```

## Best Practices

1. **Always ensure loop termination**: Avoid infinite loops
2. **Use meaningful variable names**: `i`, `j`, `k` are fine for simple loops
3. **Keep loop bodies short**: Extract complex logic into functions
4. **Be careful with nested loops**: They can be slow (O(n²) or worse)

```kuyil
// Good: Clear purpose
for student_index = 0; student_index < num_students; student_index++ {
    processStudent(students[student_index])
}

// Good: Short body
for i = 0; i < count; i++ {
    if shouldProcess(items[i]) {
        process(items[i])
    }
}

// Be careful: O(n²) complexity
for i = 0; i < n; i++ {
    for j = 0; j < n; j++ {
        // This runs n*n times!
    }
}
```

## See Also

- [Conditionals](conditionals.md) - Using conditions in loops
- [Functions](functions.md) - Extracting loop logic into functions
- [Arrays](arrays.md) - Iterating over arrays
