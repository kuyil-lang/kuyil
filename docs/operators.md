# Operators in Kuyil

This document covers all operators available in the Kuyil programming language.

## Table of Contents
- [Arithmetic Operators](#arithmetic-operators)
- [Assignment Operators](#assignment-operators)
- [Comparison Operators](#comparison-operators)
- [Logical Operators](#logical-operators)
- [Increment/Decrement Operators](#incrementdecrement-operators)
- [Operator Precedence](#operator-precedence)

## Arithmetic Operators

Perform mathematical operations on numbers.

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `+` | Addition | `5 + 3` | `8` |
| `-` | Subtraction | `5 - 3` | `2` |
| `*` | Multiplication | `5 * 3` | `15` |
| `/` | Division | `10 / 2` | `5` |
| `%` | Modulo (remainder) | `10 % 3` | `1` |

### Examples

```kuyil
let a = 10
let b = 3

print(a + b)  // 13
print(a - b)  // 7
print(a * b)  // 30
print(a / b)  // 3.33333
print(a % b)  // 1

// String concatenation with +
let name = "Hello" + " " + "World"
print(name)  // "Hello World"
```

## Assignment Operators

Assign values to variables.

| Operator | Description | Example | Equivalent |
|----------|-------------|---------|------------|
| `=` | Simple assignment | `x = 5` | - |
| `+=` | Add and assign | `x += 3` | `x = x + 3` |
| `-=` | Subtract and assign | `x -= 3` | `x = x - 3` |

### Examples

```kuyil
let x = 10

x += 5   // x is now 15
x -= 3   // x is now 12

// Works with strings too
let msg = "Hello"
msg += " World"  // msg is now "Hello World"
```

## Comparison Operators

Compare two values and return a boolean (`true` or `false`).

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `==` | Equal to | `5 == 5` | `true` |
| `!=` | Not equal to | `5 != 3` | `true` |
| `<` | Less than | `3 < 5` | `true` |
| `<=` | Less than or equal | `5 <= 5` | `true` |
| `>` | Greater than | `5 > 3` | `true` |
| `>=` | Greater than or equal | `5 >= 5` | `true` |

### Examples

```kuyil
let age = 25

if age >= 18 {
    print("Adult")
}

if age == 25 {
    print("Quarter century!")
}

if age != 30 {
    print("Not 30 yet")
}

// String comparison
let name1 = "Alice"
let name2 = "Bob"
print(name1 < name2)  // true (lexicographic comparison)
```

## Logical Operators

Perform logical operations on boolean values.

| Operator | Alternative | Description | Example | Result |
|----------|-------------|-------------|---------|--------|
| `&&` | `and` | Logical AND | `true && false` | `false` |
| `\|\|` | `or` | Logical OR | `true \|\| false` | `true` |
| `!` | `not` | Logical NOT | `!true` | `false` |

### Examples

```kuyil
let x = 10
let y = 20

// Both syntaxes work!
if x > 5 && y < 30 {
    print("Condition 1 met")
}

if x > 5 and y < 30 {
    print("Condition 2 met")
}

if x < 5 || y > 15 {
    print("At least one condition is true")
}

if x < 5 or y > 15 {
    print("Using 'or' keyword")
}

// NOT operator
let is_active = true
if !is_active {
    print("Not active")
} else {
    print("Active")
}

// Complex conditions
if (x > 5 and y < 30) or (x == 10) {
    print("Complex condition met")
}
```

### Truth Table

**AND (`&&` or `and`)**
| A | B | A && B |
|---|---|--------|
| true | true | true |
| true | false | false |
| false | true | false |
| false | false | false |

**OR (`||` or `or`)**
| A | B | A \|\| B |
|---|---|----------|
| true | true | true |
| true | false | true |
| false | true | true |
| false | false | false |

**NOT (`!`)**
| A | !A |
|---|-----|
| true | false |
| false | true |

## Increment/Decrement Operators

Modify a variable's value by 1.

| Operator | Description | Example | Effect |
|----------|-------------|---------|--------|
| `++` | Increment | `x++` | `x = x + 1` |
| `--` | Decrement | `x--` | `x = x - 1` |

### Examples

```kuyil
let counter = 0

counter++  // counter is now 1
counter++  // counter is now 2
print(counter)  // 2

counter--  // counter is now 1
print(counter)  // 1

// Useful in loops
for i = 0; i < 10; i++ {
    print(i)
}
```

## Operator Precedence

Operators are evaluated in the following order (highest to lowest):

1. **Parentheses**: `()`
2. **Unary**: `!`, `-` (negation), `++`, `--`
3. **Multiplicative**: `*`, `/`, `%`
4. **Additive**: `+`, `-`
5. **Comparison**: `<`, `<=`, `>`, `>=`
6. **Equality**: `==`, `!=`
7. **Logical AND**: `&&`, `and`
8. **Logical OR**: `||`, `or`
9. **Assignment**: `=`, `+=`, `-=`

### Examples

```kuyil
// Precedence examples
let result = 2 + 3 * 4      // 14 (multiplication first)
let result2 = (2 + 3) * 4   // 20 (parentheses first)

let x = 10
let y = 20
let z = x > 5 and y < 30  // true (comparison first, then AND)

// Always use parentheses for clarity
if (x > 5) and (y < 30) {
    print("Clear and readable")
}
```

## Type Coercion

Kuyil performs automatic type conversion in certain operations:

```kuyil
// Number to string in concatenation
let result = "Value: " + 42  // "Value: 42"

// String to number in arithmetic (if possible)
let x = "10"
let y = 5
// Note: Explicit conversion may be needed in some cases
```

## Best Practices

1. **Use parentheses** for complex expressions to make precedence clear
2. **Choose consistent style**: Use either `&&`/`||` or `and`/`or` consistently
3. **Avoid deeply nested conditions**: Break into smaller, named boolean variables
4. **Use comparison chaining carefully**: `a < b < c` may not work as expected

```kuyil
// Good: Clear and readable
let is_adult = age >= 18
let has_permission = true
if is_adult and has_permission {
    print("Allowed")
}

// Less clear: Complex condition
if age >= 18 && permission == true && status == "active" {
    print("Allowed")
}
```

## See Also

- [Conditionals](conditionals.md) - Using operators in if/else statements
- [Loops](loops.md) - Using operators in loops
- [Functions](functions.md) - Functions and expressions
