# Structs and Methods in Kuyil

Object-oriented programming with structs, methods, and interfaces.

## Table of Contents
- [Defining Structs](#defining-structs)
- [Creating Instances](#creating-instances)
- [Accessing Fields](#accessing-fields)
- [Methods](#methods)
- [The `this` Keyword](#the-this-keyword)
- [Interfaces](#interfaces)
- [Best Practices](#best-practices)

## Defining Structs

Structs are user-defined types that group related data together.

### Syntax

```kuyil
struct StructName {
    field1
    field2
    field3
}
```

### Examples

```kuyil
// Simple struct
struct Person {
    name
    age
}

// Struct with multiple fields
struct Rectangle {
    width
    height
    color
}

// Struct for data modeling
struct User {
    id
    username
    email
    created_at
}

// Nested structs
struct Address {
    street
    city
    country
}

struct Employee {
    name
    age
    address  // Can contain another struct
}
```

## Creating Instances

Create instances of structs using the struct name and curly braces.

### Syntax

```kuyil
let instance = StructName{
    field1: value1,
    field2: value2
}
```

### Examples

```kuyil
// Create a Person instance
let person = Person{
    name: "Alice",
    age: 30
}

// Create a Rectangle
let rect = Rectangle{
    width: 100,
    height: 50,
    color: "blue"
}

// Create with variables
let user_name = "john_doe"
let user_email = "john@example.com"

let user = User{
    id: 12345,
    username: user_name,
    email: user_email,
    created_at: getCurrentTime()
}

// Nested struct creation
let address = Address{
    street: "123 Main St",
    city: "New York",
    country: "USA"
}

let employee = Employee{
    name: "Bob Smith",
    age: 45,
    address: address
}
```

## Accessing Fields

Access struct fields using dot notation.

### Syntax

```kuyil
instance.fieldName
```

### Examples

```kuyil
let person = Person{name: "Alice", age: 30}

// Read fields
print(person.name)  // "Alice"
print(person.age)   // 30

// Modify fields
person.age = 31
print(person.age)   // 31

// Use in expressions
if person.age >= 18 {
    print(person.name + " is an adult")
}

// Nested field access
let employee = Employee{
    name: "Bob",
    age: 45,
    address: Address{
        street: "123 Main St",
        city: "NYC",
        country: "USA"
    }
}

print(employee.address.city)     // "NYC"
print(employee.address.country)  // "USA"

// Field assignment
employee.address.city = "Los Angeles"
```

## Methods

Methods are functions associated with a struct type.

### Syntax

```kuyil
fn StructName.methodName(parameters) {
    // method body
    // use 'this' to access instance fields
}
```

### Examples

```kuyil
// Define struct
struct Person {
    name
    age
}

// Define methods
fn Person.greet() {
    print("Hello, I'm " + this.name)
}

fn Person.isAdult() {
    return this.age >= 18
}

fn Person.haveBirthday() {
    this.age = this.age + 1
    print(this.name + " is now " + this.age)
}

fn Person.introduce(greeting) {
    print(greeting + ", I'm " + this.name + " and I'm " + this.age + " years old")
}

// Use methods
let person = Person{name: "Alice", age: 30}

person.greet()                    // "Hello, I'm Alice"
person.introduce("Hi there")      // "Hi there, I'm Alice and I'm 30 years old"
person.haveBirthday()             // "Alice is now 31"

if person.isAdult() {
    print("Alice is an adult")
}
```

### Methods with Return Values

```kuyil
struct Rectangle {
    width
    height
}

fn Rectangle.area() {
    return this.width * this.height
}

fn Rectangle.perimeter() {
    return 2 * (this.width + this.height)
}

fn Rectangle.isSquare() {
    return this.width == this.height
}

fn Rectangle.scale(factor) {
    this.width = this.width * factor
    this.height = this.height * factor
}

// Usage
let rect = Rectangle{width: 10, height: 20}

print("Area:", rect.area())           // 200
print("Perimeter:", rect.perimeter()) // 60
print("Is square?", rect.isSquare())  // false

rect.scale(2)
print("New area:", rect.area())       // 800
```

### Static-like Methods

Methods can also be defined to work without instance data.

```kuyil
struct Math {
    dummy  // Placeholder field
}

fn Math.add(a, b) {
    return a + b
}

fn Math.max(a, b) {
    if a > b {
        return a
    } else {
        return b
    }
}

// Note: You still need an instance, but can ignore the field
let math = Math{dummy: 0}
print(math.add(5, 3))      // 8
print(math.max(10, 20))    // 20
```

## The `this` Keyword

`this` refers to the current instance of the struct within a method.

### Examples

```kuyil
struct BankAccount {
    owner
    balance
}

fn BankAccount.deposit(amount) {
    this.balance = this.balance + amount
    print(this.owner + " deposited " + amount)
    print("New balance: " + this.balance)
}

fn BankAccount.withdraw(amount) {
    if amount > this.balance {
        print("Insufficient funds")
        return false
    }
    
    this.balance = this.balance - amount
    print(this.owner + " withdrew " + amount)
    return true
}

fn BankAccount.getInfo() {
    return this.owner + "'s account: $" + this.balance
}

// Usage
let account = BankAccount{
    owner: "Alice",
    balance: 1000
}

account.deposit(500)          // Balance becomes 1500
account.withdraw(200)         // Balance becomes 1300
print(account.getInfo())      // "Alice's account: $1300"
```

## Interfaces

Interfaces define a contract that structs can implement.

### Defining Interfaces

```kuyil
interface Drawable {
    fn draw()
}

interface Movable {
    fn move(x, y)
}
```

### Implementing Interfaces

```kuyil
struct Circle {
    x
    y
    radius
}

// Implement Drawable interface
fn Circle.draw() {
    print("Drawing circle at (" + this.x + "," + this.y + ") with radius " + this.radius)
}

// Implement Movable interface
fn Circle.move(dx, dy) {
    this.x = this.x + dx
    this.y = this.y + dy
}

// Usage
let circle = Circle{x: 0, y: 0, radius: 10}
circle.draw()      // "Drawing circle at (0,0) with radius 10"
circle.move(5, 3)
circle.draw()      // "Drawing circle at (5,3) with radius 10"
```

### Multiple Interfaces

A struct can implement multiple interfaces.

```kuyil
interface Named {
    fn getName()
}

interface Describable {
    fn describe()
}

struct Product {
    name
    price
    category
}

fn Product.getName() {
    return this.name
}

fn Product.describe() {
    return this.name + " - $" + this.price + " (" + this.category + ")"
}

let product = Product{
    name: "Laptop",
    price: 999,
    category: "Electronics"
}

print(product.getName())     // "Laptop"
print(product.describe())    // "Laptop - $999 (Electronics)"
```

## Best Practices

### 1. Group Related Data

```kuyil
// Good: Related data in struct
struct Contact {
    name
    email
    phone
}

// Bad: Separate variables
let contact_name = "Alice"
let contact_email = "alice@example.com"
let contact_phone = "555-1234"
```

### 2. Use Meaningful Names

```kuyil
// Good: Clear names
struct UserProfile {
    username
    email
    registration_date
}

// Bad: Unclear names
struct UP {
    un
    em
    rd
}
```

### 3. Keep Structs Focused

```kuyil
// Good: Single responsibility
struct User {
    id
    username
    email
}

struct UserSettings {
    user_id
    theme
    language
    notifications
}

// Bad: Too many responsibilities
struct User {
    id
    username
    email
    theme
    language
    notifications
    last_login
    preferences
    // ...
}
```

### 4. Use Methods for Behavior

```kuyil
// Good: Behavior in methods
struct Temperature {
    celsius
}

fn Temperature.toFahrenheit() {
    return this.celsius * 9 / 5 + 32
}

fn Temperature.toKelvin() {
    return this.celsius + 273.15
}

// Usage is clean
let temp = Temperature{celsius: 25}
print(temp.toFahrenheit())  // 77

// Bad: External functions
fn celsiusToFahrenheit(temp_struct) {
    return temp_struct.celsius * 9 / 5 + 32
}
```

### 5. Validate Data in Methods

```kuyil
struct Age {
    value
}

fn Age.set(new_value) {
    if new_value < 0 or new_value > 150 {
        print("Invalid age")
        return false
    }
    this.value = new_value
    return true
}

fn Age.isAdult() {
    return this.value >= 18
}

let age = Age{value: 25}
age.set(30)    // OK
age.set(-5)    // Prints "Invalid age"
```

## Common Patterns

### Builder Pattern

```kuyil
struct HttpRequest {
    url
    method
    headers
    body
}

fn HttpRequest.setMethod(method) {
    this.method = method
    return this  // Return this for chaining
}

fn HttpRequest.setHeader(key, value) {
    // Assume headers is an object
    this.headers[key] = value
    return this
}

fn HttpRequest.setBody(body) {
    this.body = body
    return this
}

// Usage (if chaining is supported)
let request = HttpRequest{
    url: "https://api.example.com",
    method: "GET",
    headers: {},
    body: nil
}

request.setMethod("POST")
request.setHeader("Content-Type", "application/json")
request.setBody("{\"key\": \"value\"}")
```

### Entity Pattern

```kuyil
struct Task {
    id
    title
    completed
}

fn Task.complete() {
    this.completed = true
    print("Task '" + this.title + "' completed")
}

fn Task.reopen() {
    this.completed = false
}

fn Task.toggleStatus() {
    this.completed = !this.completed
}

let task = Task{
    id: 1,
    title: "Write documentation",
    completed: false
}

task.complete()
task.toggleStatus()
```

## See Also

- [Functions](functions.md) - Function basics
- [Interfaces](INTERFACE_SYSTEM_GUIDE.md) - Complete interface system guide
- [Operators](operators.md) - Using operators with structs
