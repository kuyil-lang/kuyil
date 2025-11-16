# Conditionals in Kuyil

Control flow using conditional statements in Kuyil.

## Table of Contents
- [if Statement](#if-statement)
- [if-else Statement](#if-else-statement)
- [if-else if-else Statement](#if-else-if-else-statement)
- [switch Statement](#switch-statement)
- [Ternary Operator](#ternary-operator)
- [Truthy and Falsy Values](#truthy-and-falsy-values)

## if Statement

Execute code only if a condition is true.

### Syntax

```kuyil
if condition {
    // code to execute if condition is true
}
```

### Examples

```kuyil
let age = 25

if age >= 18 {
    print("You are an adult")
}

let score = 95
if score > 90 {
    print("Excellent!")
}

// Multiple statements
if age >= 21 {
    print("You can drink")
    print("In the US")
}
```

## if-else Statement

Execute one block if condition is true, another if false.

### Syntax

```kuyil
if condition {
    // code if true
} else {
    // code if false
}
```

### Examples

```kuyil
let age = 15

if age >= 18 {
    print("Adult")
} else {
    print("Minor")
}

let temperature = 30
if temperature > 25 {
    print("Hot day")
} else {
    print("Cool day")
}
```

## if-else if-else Statement

Test multiple conditions in sequence.

### Syntax

```kuyil
if condition1 {
    // code if condition1 is true
} else if condition2 {
    // code if condition2 is true
} else if condition3 {
    // code if condition3 is true
} else {
    // code if all conditions are false
}
```

### Examples

```kuyil
let score = 85

if score >= 90 {
    print("Grade: A")
} else if score >= 80 {
    print("Grade: B")
} else if score >= 70 {
    print("Grade: C")
} else if score >= 60 {
    print("Grade: D")
} else {
    print("Grade: F")
}

// Temperature ranges
let temp = 22
if temp < 0 {
    print("Freezing")
} else if temp < 10 {
    print("Cold")
} else if temp < 20 {
    print("Cool")
} else if temp < 30 {
    print("Warm")
} else {
    print("Hot")
}
```

## switch Statement

Select one of many code blocks to execute based on a value.

### Syntax

```kuyil
switch expression {
    case value1:
        // code for value1
    case value2:
        // code for value2
    case value3:
        // code for value3
    default:
        // code if no case matches
}
```

### Examples

```kuyil
let day = 3

switch day {
    case 1:
        print("Monday")
    case 2:
        print("Tuesday")
    case 3:
        print("Wednesday")
    case 4:
        print("Thursday")
    case 5:
        print("Friday")
    case 6:
        print("Saturday")
    case 7:
        print("Sunday")
    default:
        print("Invalid day")
}

// String matching
let command = "start"

switch command {
    case "start":
        print("Starting...")
    case "stop":
        print("Stopping...")
    case "restart":
        print("Restarting...")
    case "status":
        print("Running")
    default:
        print("Unknown command")
}

// With break statements
let option = 2

switch option {
    case 1:
        print("Option 1")
        break
    case 2:
        print("Option 2")
        break
    default:
        print("Default option")
}
```

### Switch vs if-else if

**Use switch when:**
- Comparing a single variable against multiple constant values
- You have many possible values to check
- Values are discrete (numbers, strings, enums)

**Use if-else if when:**
- Checking different conditions
- Using range comparisons (`>`, `<`, etc.)
- Conditions are more complex

```kuyil
// Better with switch
let status = "active"
switch status {
    case "active": print("Active")
    case "inactive": print("Inactive")
    case "pending": print("Pending")
}

// Better with if-else
let age = 25
if age < 18 {
    print("Minor")
} else if age < 65 {
    print("Adult")
} else {
    print("Senior")
}
```

## Ternary Operator

**Note:** Kuyil currently does not have a traditional ternary operator (`condition ? value1 : value2`). Use if-else expressions instead.

### Alternative Patterns

```kuyil
// Using if-else statements
let age = 20
let status = nil

if age >= 18 {
    status = "adult"
} else {
    status = "minor"
}
print(status)

// Using function
fn getStatus(age) {
    if age >= 18 {
        return "adult"
    } else {
        return "minor"
    }
}

let status2 = getStatus(20)
print(status2)
```

## Truthy and Falsy Values

In Kuyil, values are evaluated as true or false in conditional contexts.

### Falsy Values

The following values are considered false:
- `false` - The boolean false value
- `nil` - The nil/null value
- `0` - The number zero (in some contexts)

### Truthy Values

All other values are considered true:
- `true` - The boolean true value
- Any non-zero number
- Any non-empty string
- Any object, array, or function

### Examples

```kuyil
// Boolean values
if true {
    print("This will print")
}

if false {
    print("This won't print")
}

// nil check
let value = nil
if value {
    print("Has value")
} else {
    print("Is nil")  // This prints
}

// Number checks
if 0 {
    print("Zero")
}

if 42 {
    print("Non-zero number is truthy")
}

// String checks
let name = "Alice"
if name {
    print("Name is set")
}

let empty = ""
if empty {
    print("Empty string")
} else {
    print("String might be empty or nil")
}
```

## Nested Conditionals

You can nest conditionals inside each other.

```kuyil
let age = 25
let has_license = true

if age >= 18 {
    if has_license {
        print("Can drive")
    } else {
        print("Too young, but no license")
    }
} else {
    print("Too young to drive")
}

// Better: Flatten with logical operators
if age >= 18 and has_license {
    print("Can drive")
} else if age >= 18 {
    print("Can drive but no license")
} else {
    print("Too young to drive")
}
```

## Best Practices

1. **Keep conditions simple**: Break complex conditions into named variables
2. **Avoid deep nesting**: Use early returns or logical operators
3. **Use switch for multiple discrete values**: More readable than many if-else
4. **Always handle the else case**: Make your code's behavior explicit

### Good Examples

```kuyil
// Good: Clear and readable
let is_adult = age >= 18
let has_permission = true

if is_adult and has_permission {
    print("Allowed")
}

// Good: Early return pattern
fn checkAccess(age, permission) {
    if age < 18 {
        return "Too young"
    }
    
    if !permission {
        return "No permission"
    }
    
    return "Access granted"
}

// Good: Switch for discrete values
switch status {
    case "pending":
        handlePending()
    case "approved":
        handleApproved()
    case "rejected":
        handleRejected()
    default:
        handleUnknown()
}
```

### Bad Examples

```kuyil
// Bad: Complex nested conditions
if x > 0 {
    if y > 0 {
        if z > 0 {
            print("All positive")
        }
    }
}

// Better: Flatten with logical operators
if x > 0 and y > 0 and z > 0 {
    print("All positive")
}

// Bad: Long if-else chain for discrete values
if day == 1 {
    print("Monday")
} else if day == 2 {
    print("Tuesday")
} else if day == 3 {
    print("Wednesday")
}
// ...should use switch instead
```

## See Also

- [Operators](operators.md) - Comparison and logical operators
- [Loops](loops.md) - Loop control flow
- [Functions](functions.md) - Functions and return values
