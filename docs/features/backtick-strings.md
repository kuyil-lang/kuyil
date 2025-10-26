# Kuyil Backtick Strings Implementation

## Overview
Implemented backtick-delimited strings in Kuyil to avoid escape character issues when defining JSON, HTML, JavaScript, and other content that contains quotes.

## Feature Implementation

### 1. Lexer Changes (`src/lexer.c`)
- Added `TOKEN_BACKTICK_STRING` token type in `src/tokens.h`
- Implemented `backtick_string()` function for parsing backtick-delimited strings
- Added case for '`' character in `lexer_scan_token()`
- Updated `token_type_string()` function to handle new token type

### 2. Parser Changes (`src/parser.c`)
- Added parsing support for `TOKEN_BACKTICK_STRING` in primary expression parsing
- Creates AST nodes with `VALUE_STRING` type (same as regular strings)
- Strips backticks during parsing (similar to quote stripping for regular strings)

### 3. Syntax Examples

#### Traditional String (with escapes)
```kuyil
let json_data = "{\"name\": \"Alice\", \"skills\": [\"Kuyil\", \"JavaScript\"]}"
```

#### Backtick String (no escapes needed)
```kuyil
let json_data = `{"name": "Alice", "skills": ["Kuyil", "JavaScript"]}`
```

## Benefits

### 1. JSON Definition
**Before:**
```kuyil
let config = "{
  \"server\": {
    \"port\": 8080,
    \"host\": \"localhost\"
  },
  \"database\": {
    \"url\": \"postgres://user:pass@localhost/db\"
  }
}"
```

**After:**
```kuyil
let config = `{
  "server": {
    "port": 8080,
    "host": "localhost"
  },
  "database": {
    "url": "postgres://user:pass@localhost/db"
  }
}`
```

### 2. HTML Templates
**Before:**
```kuyil
let html = "<div class=\"container\">
  <h1>Welcome to \"Kuyil\"</h1>
  <p>Let's code with 'ease'!</p>
</div>"
```

**After:**
```kuyil
let html = `<div class="container">
  <h1>Welcome to "Kuyil"</h1>
  <p>Let's code with 'ease'!</p>
</div>`
```

### 3. JavaScript Code
**Before:**
```kuyil
let js_code = "function greet(name) {
  console.log(\"Hello, \" + name + \"!\");
  return true;
}"
```

**After:**
```kuyil
let js_code = `function greet(name) {
  console.log("Hello, " + name + "!");
  return true;
}`
```

### 4. SQL Queries
**Before:**
```kuyil
let query = "SELECT u.name, p.bio 
FROM users u 
JOIN profiles p ON u.id = p.user_id 
WHERE u.status = 'active' 
ORDER BY u.created_at DESC"
```

**After:**
```kuyil
let query = `SELECT u.name, p.bio 
FROM users u 
JOIN profiles p ON u.id = p.user_id 
WHERE u.status = 'active' 
ORDER BY u.created_at DESC`
```

## Technical Details

### Implementation Files Modified
1. `src/tokens.h` - Added `TOKEN_BACKTICK_STRING` enum value
2. `src/lexer.c` - Added backtick parsing logic and token generation
3. `src/parser.c` - Added AST node creation for backtick strings
4. `examples/multiline_strings.kyl` - Updated with backtick examples

### Parsing Behavior
- Backtick strings support multiline content (newlines are preserved)
- No escape sequence processing (everything between backticks is literal)
- Opening and closing backticks are stripped during parsing
- Result is stored as regular `VALUE_STRING` in the AST

### Compilation Integration
- Works with existing string compilation pipeline
- No changes needed in compiler or VM for execution
- Backtick and regular strings are interchangeable at runtime

## Usage Guidelines

### When to Use Backtick Strings
✅ **Use backticks for:**
- JSON data structures
- HTML/XML content
- JavaScript/CSS code blocks  
- SQL queries
- Configuration files
- Any content with mixed quotes

✅ **Use regular strings for:**
- Simple text without quotes
- When escape sequences are needed (\\n, \\t, etc.)
- Single-line strings without special characters

### Limitations
- Currently no escape sequences inside backtick strings (all content is literal)
- Cannot include literal backtick characters inside backtick strings
- No string interpolation (template literal features)

## Future Enhancements
1. **Nested Backticks**: Support for escaped backticks inside backtick strings
2. **String Interpolation**: `Hello ${name}!` syntax with variable substitution
3. **Tagged Templates**: Custom processing functions for backtick strings
4. **Multi-delimiter**: Different delimiter pairs for different content types

## Testing
The implementation includes comprehensive test cases in:
- `examples/multiline_strings.kyl` - Demonstrates various use cases
- Shows comparison between traditional and backtick string syntax
- Includes real-world examples (JSON, HTML, JavaScript, SQL)

This feature significantly improves developer experience when working with structured data and markup languages in Kuyil programs.