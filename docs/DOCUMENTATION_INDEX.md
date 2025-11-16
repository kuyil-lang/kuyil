# Kuyil Language Documentation Index

Complete guide to the Kuyil programming language.

## 📚 Core Language Features

### Language Basics
- **[Operators](operators.md)** - All operators including `and`/`or` keywords, arithmetic, logical, comparison, and assignment operators
- **[Conditionals](conditionals.md)** - if/else statements, switch/case, truthy/falsy values
- **[Loops](loops.md)** - for loops, while loops, break, continue, and loop patterns
- **Functions** *(Coming soon)* - Function syntax, parameters, return values, closures

### Advanced Features
- **[Structs and Methods](structs-and-methods.md)** - Object-oriented programming with structs, methods, and the `this` keyword
- **[Avatars](avatars.md)** - Async/await concurrency model with avatar functions
- **[Import System](import-system.md)** - Module imports with both `import "path"` and `import("path")` syntax

## 🔧 Integration & Extension

### FFI and Native Libraries
- **[FFI Guide](ffi-guide.md)** - Creating and using C shared libraries (.so files)
- **[Shared Library Guide](SHARED_LIBRARY_GUIDE.md)** - Building Kuyil-compatible libraries
- **Interface System** - High-level library interfaces *(see FFI_SYSTEM_GUIDE.md)*

## 📦 Standard Library

### Core Libraries
Documentation for built-in libraries:

- **math** - Mathematical operations *(docs/libraries/math.md - pending)*
- **str** - String manipulation *(docs/libraries/str.md - pending)*
- **http** - HTTP client/server *(docs/libraries/http.md - pending)*
- **datetime** - Date and time operations *(docs/libraries/datetime.md - pending)*
- **fileio** - File input/output *(docs/libraries/fileio.md - pending)*
- **eventloop** - Event loop for async operations *(docs/libraries/eventloop.md - pending)*
- **system** - System operations *(docs/libraries/system.md - pending)*
- **asyncio** - Asynchronous I/O *(docs/libraries/asyncio.md - pending)*

### Additional Libraries
Extended functionality:

- **crypto** - Cryptographic operations *(docs/libraries/crypto.md - pending)*
- **ffmpeg** - Video/audio processing *(docs/libraries/ffmpeg.md - pending)*
- **rpc** - Remote procedure calls *(docs/libraries/rpc.md - pending)*
- **sqlite** - SQLite database *(docs/libraries/sqlite.md - pending)*
- **transcoder** - Media transcoding *(docs/libraries/transcoder.md - pending)*
- **webview** - GUI with webview *(docs/libraries/webview.md - pending)*

## 🚀 Getting Started

### Quick Start
1. Read the [README](../README.md) for installation and basic usage
2. Review [Quick Start Guide](quick-start.md)
3. Explore [Examples](../examples/)

### Learning Path

**Beginners:**
1. Operators and basic syntax
2. Conditionals and loops
3. Functions (when available)
4. Structs and methods

**Intermediate:**
1. Import system and modules
2. Avatars and async programming
3. Standard library usage
4. File I/O and HTTP

**Advanced:**
1. FFI and native libraries
2. Creating shared libraries
3. Performance optimization
4. Reactive programming

## 📖 Language Reference

### Syntax Summary

```kuyil
// Variables
let x = 10
const PI = 3.14159

// Operators - both styles work
if x > 5 && y < 10 { }      // C-style
if x > 5 and y < 10 { }      // Keyword-style

// Functions
fn greet(name) {
    return "Hello, " + name
}

// Structs
struct Person {
    name: "",
    age: 0
}

fn Person.greet(this) {
    print("Hi, I'm " + this.name)
}

// Avatars (async)
avatar fetch_data(url) {
    let response = await http.get(url)
    return response.body
}

// Imports - both styles work
import "module.kyl"              // New style
import("module.kyl")             // Function-call style
import "utils.kyl" as utils      // Namespace import
```

## 🔍 Documentation Status

### ✅ Completed
- README.md (200+ lines)
- operators.md (400+ lines)
- conditionals.md (450+ lines)
- loops.md (500+ lines)
- structs-and-methods.md (600+ lines)
- avatars.md (500+ lines)
- import-system.md (450+ lines)
- ffi-guide.md (500+ lines)

**Total: ~3,600 lines of core documentation**

### ⏳ Pending
- functions.md - Basic function documentation
- 14 library documentation files (8 core + 6 additional)
- Cleanup of existing documentation
- Update IMPLEMENTATION_SUMMARY.md

## 🎯 Recent Language Enhancements

### New in Current Version

**Logical Operator Keywords:**
- `and` keyword as alternative to `&&`
- `or` keyword as alternative to `||`
- Both styles can be used interchangeably

**Flexible Import Syntax:**
- `import "module.kyl"` - Simplified import syntax
- `import("module.kyl")` - Function-call style (backward compatible)
- `import "module.kyl" as name` - Namespace imports

**Example:**
```kuyil
// All valid syntax
let x = true and false
let y = true && false

import "utils.kyl"
import("helpers.kyl")
```

## 📝 Contributing to Documentation

When adding documentation:

1. **Format**: Use Markdown with code examples
2. **Structure**: Include table of contents, examples, best practices
3. **Examples**: Provide working code samples
4. **Cross-reference**: Link to related documentation
5. **Test**: Verify all code examples work

### Documentation Template

```markdown
# Feature Name

Brief description of the feature.

## Table of Contents
- [Overview](#overview)
- [Syntax](#syntax)
- [Examples](#examples)
- [Best Practices](#best-practices)
- [See Also](#see-also)

## Overview
Detailed explanation...

## Syntax
\`\`\`kuyil
// Syntax examples
\`\`\`

## Examples
Working code examples...

## Best Practices
Recommended patterns...

## See Also
- [Related Doc 1](link1.md)
- [Related Doc 2](link2.md)
```

## 🔗 External Resources

- **Repository**: [GitHub](https://github.com/YourOrg/kuyil)
- **Examples**: [examples/](../examples/)
- **Tests**: [tests/](../tests/)
- **Build System**: [Makefile](../Makefile)

## 📧 Get Help

- Read the documentation thoroughly
- Check the [examples/](../examples/) directory
- Review existing [test files](../tests/)
- Open an issue on GitHub

---

**Last Updated**: January 2025  
**Documentation Version**: 1.0  
**Language Version**: Target v1.0.0

