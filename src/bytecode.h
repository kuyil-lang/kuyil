#ifndef KUYIL_BYTECODE_H
#define KUYIL_BYTECODE_H

#include "ast.h"
#include <stdint.h>

typedef enum {
    OP_CONSTANT,      // Push constant from constant pool (1 byte index)
    OP_CONSTANT_LONG, // Push constant from constant pool (2 byte index)
    OP_NIL,           // Push nil
    OP_TRUE,          // Push true
    OP_FALSE,         // Push false
    OP_POP,           // Pop from stack
    OP_DUP,           // Duplicate top of stack
    
    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_TO_STRING,      // Convert value to string
    
    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    
    // Logic
    OP_NOT,
    OP_AND,
    OP_OR,
    
    // Variables
    OP_DEFINE_GLOBAL,      // Define global variable (1 byte index)
    OP_GET_GLOBAL,         // Get global variable (1 byte index)
    OP_SET_GLOBAL,         // Set global variable (1 byte index)
    OP_DEFINE_GLOBAL_LONG, // Define global variable (2 byte index)
    OP_GET_GLOBAL_LONG,    // Get global variable (2 byte index)
    OP_SET_GLOBAL_LONG,    // Set global variable (2 byte index)
    OP_GET_LOCAL,          // Get local variable
    OP_SET_LOCAL,          // Set local variable
    
    // Functions
    OP_CALL,           // Call function
    OP_RETURN,         // Return from function
    OP_CLOSURE,        // Create closure/anonymous function
    OP_AVATAR,         // Launch avatar (green thread)
    OP_AWAIT,          // Wait for avatar to complete
    OP_DEFER,          // Register function for deferred execution at scope exit
    
    // Control flow
    OP_JUMP,           // Unconditional jump
    OP_JUMP_IF_FALSE,  // Jump if top of stack is falsy
    OP_LOOP,           // Jump backwards for loops
    
    // Built-in functions
    OP_PRINT,          // Print value
    OP_HTTP_GET,       // HTTP GET request
    OP_HTTP_POST,      // HTTP POST request
    OP_HTTP_SERVER,    // Create HTTP server
    
    // Array operations
    OP_ARRAY,          // Create array with N elements from stack
    OP_ARRAY_GET,      // Get array element by index
    OP_ARRAY_SET,      // Set array element by index
    
    // Object operations
    OP_OBJECT_NEW,     // Create a new empty object
    OP_OBJECT_GET,     // Get object property by name (name on stack)
    OP_OBJECT_SET,     // Set object property by name (name on stack)
    
    // Logging operations
    OP_LOG_FATAL,      // Log fatal message
    OP_LOG_ERROR,      // Log error message
    OP_LOG_WARNING,    // Log warning message
    OP_LOG_INFO,       // Log info message
    OP_LOG_DEBUG,      // Log debug message
    OP_LOG_PUSH_CTX,   // Push function context
    OP_LOG_POP_CTX,    // Pop function context
    
    OP_HALT            // Stop execution
} OpCode;

typedef struct {
    uint8_t* code;
    int count;
    int capacity;
    Value* constants;
    int constant_count;
    int constant_capacity;
    int* lines;  // Line numbers for each instruction
} Chunk;

typedef struct {
    char* name;
    int arity;
    int local_count;  // Number of local variables (including parameters)
    Chunk chunk;
    bool is_native;
    const char* source_path;  // Source file path for error reporting
} Function;

typedef struct {
    Chunk* chunk;
    bool had_error;
    
    // Local variables
    struct {
        char* name;
        int depth;
    } locals[256];
    int local_count;
    int max_local_count;  // Track peak local count for runtime stack protection
    int scope_depth;
    
    // Function compilation
    Function* function;
    struct Compiler* enclosing;
} Compiler;

// Chunk functions
void chunk_init(Chunk* chunk);
void chunk_free(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, int line);
int chunk_add_constant(Chunk* chunk, Value value);

// Compiler functions
void compiler_init(Compiler* compiler, const char* name);
Function* compiler_compile(ASTNode* ast);
void compiler_free(Compiler* compiler);

// Export tracking functions
void compiler_clear_exports(void);

// Decorator metadata collection
typedef struct {
    char* entity_name;
    DecoratorList decorators;
    DecoratorList param_decorators;
} DecoratorMetadata;

void compiler_collect_decorator_metadata(const char* entity_name, DecoratorList* decorators, DecoratorList* param_decorators);
void compiler_register_all_decorators(void* vm);
void compiler_clear_decorator_metadata(void);
int compiler_get_all_decorated_entities(const char*** out_names);
int compiler_find_entities_by_decorator(const char* decorator_name, const char*** out_names);
const char** compiler_get_exports(int* count);

// Disassembly for debugging
void disassemble_chunk(Chunk* chunk, const char* name);
int disassemble_instruction(Chunk* chunk, int offset);

// Function object functions
Function* function_new();
void function_free(Function* function);

#endif