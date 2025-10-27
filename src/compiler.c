#define _POSIX_C_SOURCE 200809L
#include "bytecode.h"
#include "logging.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Chunk functions
void chunk_init(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->constants = NULL;
    chunk->constant_count = 0;
    chunk->constant_capacity = 0;
}

void chunk_free(Chunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    
    // Free constants
    for (int i = 0; i < chunk->constant_count; i++) {
        if (chunk->constants[i].type == VALUE_STRING) {
            free(chunk->constants[i].as.string);
        }
    }
    free(chunk->constants);
    
    chunk_init(chunk);
}

void chunk_write(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int old_capacity = chunk->capacity;
        chunk->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        chunk->code = realloc(chunk->code, sizeof(uint8_t) * chunk->capacity);
        chunk->lines = realloc(chunk->lines, sizeof(int) * chunk->capacity);
    }
    
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int chunk_add_constant(Chunk* chunk, Value value) {
    if (chunk->constant_capacity < chunk->constant_count + 1) {
        int old_capacity = chunk->constant_capacity;
        chunk->constant_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        chunk->constants = realloc(chunk->constants, sizeof(Value) * chunk->constant_capacity);
    }
    
    // CRITICAL FIX: Make deep copy of strings to survive AST cleanup
    if (value.type == VALUE_STRING) {
        Value string_copy = value;
        string_copy.as.string = strdup(value.as.string); // Deep copy!
        chunk->constants[chunk->constant_count] = string_copy;
    } else {
        chunk->constants[chunk->constant_count] = value;
    }
    
    return chunk->constant_count++;
}

// Compiler state
static Compiler* current = NULL;

static void error_at_node(ASTNode* node, const char* message) {
    fprintf(stderr, "[line %d] Error: %s\n", node->line, message);
    current->had_error = true;
}

static void emit_byte(uint8_t byte) {
    chunk_write(current->chunk, byte, 0); // TODO: proper line tracking
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current->chunk->count - 2;
}

static void patch_jump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = current->chunk->count - offset - 2;
    
    if (jump > UINT16_MAX) {
        error_at_node(NULL, "Too much code to jump over.");
    }
    
    current->chunk->code[offset] = (jump >> 8) & 0xff;
    current->chunk->code[offset + 1] = jump & 0xff;
}

static void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);
    
    int offset = current->chunk->count - loop_start + 2;
    if (offset > UINT16_MAX) error_at_node(NULL, "Loop body too large.");
    
    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static uint8_t make_constant(Value value) {
    int constant = chunk_add_constant(current->chunk, value);
    if (constant > UINT8_MAX) {
        error_at_node(NULL, "Too many constants in one chunk.");
        return 0;
    }
    
    return (uint8_t)constant;
}

static void emit_constant(Value value) {
    emit_bytes(OP_CONSTANT, make_constant(value));
}

// Variable management
static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;
    
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        emit_byte(OP_POP);
        current->local_count--;
    }
}

static int add_local(const char* name) {
    if (current->local_count == 256) {
        error_at_node(NULL, "Too many local variables in function.");
        return -1;
    }
    
    current->locals[current->local_count].name = strdup(name);
    current->locals[current->local_count].depth = current->scope_depth;
    return current->local_count++;
}

static int resolve_local(const char* name) {
    for (int i = current->local_count - 1; i >= 0; i--) {
        if (strcmp(current->locals[i].name, name) == 0) {
            return i;
        }
    }
    
    return -1;
}

static uint8_t identifier_constant(const char* name) {
    Value value;
    value.type = VALUE_STRING;
    value.as.string = strdup(name);
    return make_constant(value);
}

// Forward declarations
static void compile_expression(ASTNode* node);
static void compile_statement(ASTNode* node);
static void compile_anonymous_function(ASTNode* node);

static void compile_literal(ASTNode* node) {
    switch (node->as.literal.type) {
        case VALUE_NIL:
            emit_byte(OP_NIL);
            break;
        case VALUE_BOOL:
            emit_byte(node->as.literal.as.boolean ? OP_TRUE : OP_FALSE);
            break;
        case VALUE_NUMBER:
        case VALUE_STRING:
            emit_constant(node->as.literal);
            break;
        case VALUE_ARRAY:
        case VALUE_OBJECT:
            // TODO: Implement complex literal types
            emit_byte(OP_NIL);
            break;
        case VALUE_FUNCTION:
            // Function literals should not appear in literal nodes
            // They are handled in compile_anonymous_function
            emit_byte(OP_NIL);
            break;
    }
}

static void compile_identifier(ASTNode* node) {
    int local = resolve_local(node->as.identifier);
    if (local != -1) {
        emit_bytes(OP_GET_LOCAL, (uint8_t)local);
    } else {
        uint8_t name = identifier_constant(node->as.identifier);
        emit_bytes(OP_GET_GLOBAL, name);
    }
}

static void compile_interpolated_string(ASTNode* node) {
    // Compile interpolated string by concatenating parts and expressions
    InterpolatedString* str = &node->as.interpolated_string;
    
    // Start with the first string part
    if (str->part_count > 0) {
        Value first_part;
        first_part.type = VALUE_STRING;
        first_part.as.string = str->string_parts[0];
        emit_constant(first_part);
    }
    
    // For each expression, compile it and concatenate
    for (int i = 0; i < str->part_count - 1; i++) {
        // Compile the expression
        compile_expression(str->expressions[i]);
        
        // Convert to string if needed (emit string conversion bytecode)
        emit_byte(OP_TO_STRING);
        
        // Concatenate with previous result
        emit_byte(OP_ADD);
        
        // Add the next string part
        if (i + 1 < str->part_count) {
            Value next_part;
            next_part.type = VALUE_STRING;
            next_part.as.string = str->string_parts[i + 1];
            emit_constant(next_part);
            emit_byte(OP_ADD);
        }
    }
}

static void compile_binary_op(ASTNode* node) {
    // Compile operands
    compile_expression(node->as.binary.left);
    compile_expression(node->as.binary.right);
    
    // Emit operator instruction
    switch (node->as.binary.operator) {
        case TOKEN_PLUS: emit_byte(OP_ADD); break;
        case TOKEN_MINUS: emit_byte(OP_SUBTRACT); break;
        case TOKEN_MULTIPLY: emit_byte(OP_MULTIPLY); break;
        case TOKEN_DIVIDE: emit_byte(OP_DIVIDE); break;
        case TOKEN_MODULO: emit_byte(OP_MODULO); break;
        case TOKEN_EQUAL: emit_byte(OP_EQUAL); break;
        case TOKEN_NOT_EQUAL: emit_byte(OP_NOT_EQUAL); break;
        case TOKEN_GREATER: emit_byte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emit_byte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS: emit_byte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emit_byte(OP_LESS_EQUAL); break;
        case TOKEN_AND: emit_byte(OP_AND); break;
        case TOKEN_OR: emit_byte(OP_OR); break;
        default:
            error_at_node(node, "Unknown binary operator.");
    }
}

static void compile_unary_op(ASTNode* node) {
    compile_expression(node->as.unary.operand);
    
    switch (node->as.unary.operator) {
        case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
        case TOKEN_NOT: emit_byte(OP_NOT); break;
        default:
            error_at_node(node, "Unknown unary operator.");
    }
}

static void compile_call(ASTNode* node) {
    // Check if this is a logging function call
    if (node->as.call.function->type == AST_IDENTIFIER) {
        const char* func_name = node->as.call.function->as.identifier;
        
        if (strcmp(func_name, "log_fatal") == 0) {
            compile_expression(node->as.call.args[0]);
            emit_byte(OP_LOG_FATAL);
            return;
        } else if (strcmp(func_name, "log_error") == 0) {
            compile_expression(node->as.call.args[0]);
            emit_byte(OP_LOG_ERROR);
            return;
        } else if (strcmp(func_name, "log_warning") == 0) {
            compile_expression(node->as.call.args[0]);
            emit_byte(OP_LOG_WARNING);
            return;
        } else if (strcmp(func_name, "log_info") == 0) {
            compile_expression(node->as.call.args[0]);
            emit_byte(OP_LOG_INFO);
            return;
        } else if (strcmp(func_name, "log_debug") == 0) {
            compile_expression(node->as.call.args[0]);
            emit_byte(OP_LOG_DEBUG);
            return;
        }
    }

    // Compile arguments
    for (int i = 0; i < node->as.call.arg_count; i++) {
        compile_expression(node->as.call.args[i]);
    }

    // Compile function expression
    compile_expression(node->as.call.function);

    emit_bytes(OP_CALL, node->as.call.arg_count);
}

static void compile_assignment(ASTNode* node) {
    compile_expression(node->as.assignment.value);
    
    if (node->as.assignment.target->type == AST_IDENTIFIER) {
        const char* name = node->as.assignment.target->as.identifier;
        int local = resolve_local(name);
        
        if (local != -1) {
            emit_bytes(OP_SET_LOCAL, (uint8_t)local);
        } else {
            uint8_t name_constant = identifier_constant(name);
            emit_bytes(OP_SET_GLOBAL, name_constant);
        }
    } else if (node->as.assignment.target->type == AST_ARRAY_ACCESS) {
        // For array[index] = value, we need: array, index, value
        compile_expression(node->as.assignment.target->as.array_access.array);
        compile_expression(node->as.assignment.target->as.array_access.index);
        emit_byte(OP_ARRAY_SET);
    } else {
        error_at_node(node, "Invalid assignment target.");
    }
}

static void compile_array_literal(ASTNode* node) {
    // Compile each element and push onto stack
    for (int i = 0; i < node->as.array_literal.count; i++) {
        compile_expression(node->as.array_literal.elements[i]);
    }
    
    // Emit OP_ARRAY with element count
    emit_bytes(OP_ARRAY, (uint8_t)node->as.array_literal.count);
}

static void compile_array_access(ASTNode* node) {
    // Compile array expression
    compile_expression(node->as.array_access.array);
    
    // Compile index expression
    compile_expression(node->as.array_access.index);
    
    // Emit OP_ARRAY_GET
    emit_byte(OP_ARRAY_GET);
}

static void compile_expression(ASTNode* node) {
    if (node == NULL) return;
    
    switch (node->type) {
        case AST_LITERAL:
            compile_literal(node);
            break;
        case AST_IDENTIFIER:
            compile_identifier(node);
            break;
        case AST_BINARY_OP:
            compile_binary_op(node);
            break;
        case AST_UNARY_OP:
            compile_unary_op(node);
            break;
        case AST_CALL:
            compile_call(node);
            break;
        case AST_ASSIGNMENT:
            compile_assignment(node);
            break;
        case AST_INTERPOLATED_STRING:
            compile_interpolated_string(node);
            break;
        case AST_ANONYMOUS_FUNCTION:
            compile_anonymous_function(node);
            break;
        case AST_ARRAY_LITERAL:
            compile_array_literal(node);
            break;
        case AST_ARRAY_ACCESS:
            compile_array_access(node);
            break;
        default:
            error_at_node(node, "Unknown expression type.");
    }
}

static void compile_anonymous_function(ASTNode* node) {
    // Save current compiler state
    Compiler* enclosing = current;
    Compiler function_compiler;
    compiler_init(&function_compiler, "<anonymous>");
    
    // Set function arity after compiler_init creates the function
    function_compiler.function->arity = node->as.anonymous_function.param_count;
    
    begin_scope();
    
    // Add parameters as local variables
    for (int i = 0; i < node->as.anonymous_function.param_count; i++) {
        add_local(node->as.anonymous_function.params[i]);
    }
    
    // Compile function body
    if (node->as.anonymous_function.body) {
        compile_statement(node->as.anonymous_function.body);
    }
    
    // Emit return nil if no explicit return
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
    
    end_scope();
    
    // Get the compiled function
    Function* function = function_compiler.function;
    
    // Restore compiler state
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL; // No closure for now
    function_value.as.function.capture_count = 0;
    
    uint8_t constant_index = make_constant(function_value);
    emit_byte(OP_CONSTANT);
    emit_byte(constant_index);
}

static void compile_var_decl(ASTNode* node) {
    if (current->scope_depth > 0) {
        // Local variable
        add_local(node->as.var_decl.name);
        if (node->as.var_decl.value) {
            compile_expression(node->as.var_decl.value);
        } else {
            emit_byte(OP_NIL);
        }
        // Local variables are implicitly on the stack
    } else {
        // Global variable
        uint8_t global = identifier_constant(node->as.var_decl.name);
        
        if (node->as.var_decl.value) {
            compile_expression(node->as.var_decl.value);
        } else {
            emit_byte(OP_NIL);
        }
        
        emit_bytes(OP_DEFINE_GLOBAL, global);
    }
}

static void compile_function_decl(ASTNode* node) {
    // Push function context for logging
    uint8_t name_constant = identifier_constant(node->as.function_decl.name);
    emit_bytes(OP_LOG_PUSH_CTX, name_constant);
    
    // Save current compiler state
    Compiler* enclosing = current;
    Compiler function_compiler;
    compiler_init(&function_compiler, node->as.function_decl.name);
    
    // Set function arity after compiler_init creates the function
    function_compiler.function->arity = node->as.function_decl.param_count;
    
    begin_scope();
    
    // Add parameters as local variables
    for (int i = 0; i < node->as.function_decl.param_count; i++) {
        add_local(node->as.function_decl.params[i]);
    }
    
    // Compile function body
    if (node->as.function_decl.body) {
        // Function body is a Block*, need to compile each statement
        for (int i = 0; i < node->as.function_decl.body->count; i++) {
            compile_statement(node->as.function_decl.body->statements[i]);
        }
    }
    
    // Emit return nil if no explicit return
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
    
    end_scope();
    
    // Get the compiled function
    Function* function = function_compiler.function;
    
    // Restore compiler state
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL; // No closure for now
    function_value.as.function.capture_count = 0;
    
    uint8_t constant_index = make_constant(function_value);
    emit_byte(OP_CONSTANT);
    emit_byte(constant_index);
    
    // Define the function as a global variable
    emit_bytes(OP_DEFINE_GLOBAL, name_constant);
    
    // Pop context when function exits
    emit_byte(OP_LOG_POP_CTX);
}

static void compile_if_stmt(ASTNode* node) {
    compile_expression(node->as.if_stmt.condition);
    
    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP); // Pop condition
    
    compile_statement(node->as.if_stmt.then_branch);
    
    int else_jump = emit_jump(OP_JUMP);
    
    patch_jump(then_jump);
    emit_byte(OP_POP); // Pop condition
    
    if (node->as.if_stmt.else_branch != NULL) {
        compile_statement(node->as.if_stmt.else_branch);
    }
    
    patch_jump(else_jump);
}

static void compile_while_stmt(ASTNode* node) {
    int loop_start = current->chunk->count;
    
    // Compile condition
    compile_expression(node->as.while_stmt.condition);
    
    // Jump out if false, but leave condition on stack for now
    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    // Pop the condition result since we're entering the body
    emit_byte(OP_POP);
    
    // Compile body
    compile_statement(node->as.while_stmt.body);
    
    // Jump back to start of loop (before condition evaluation)
    emit_loop(loop_start);
    
    // Patch the exit jump to come here
    patch_jump(exit_jump);
    // Pop the condition result when exiting
    emit_byte(OP_POP);
}

static void compile_for_stmt(ASTNode* node) {
    // Compile initializer (if present)
    if (node->as.for_stmt.init) {
        if (node->as.for_stmt.init->type == AST_VAR_DECL) {
            compile_var_decl(node->as.for_stmt.init);
        } else {
            compile_expression(node->as.for_stmt.init);
            emit_byte(OP_POP);  // Pop the result since it's not used
        }
    }
    
    int loop_start = current->chunk->count;
    
    // Compile condition (if present, otherwise infinite loop)
    int exit_jump = -1;
    if (node->as.for_stmt.condition) {
        compile_expression(node->as.for_stmt.condition);
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP);
    }
    
    // Compile body
    compile_statement(node->as.for_stmt.body);
    
    // Compile update (if present)  
    if (node->as.for_stmt.update) {
        compile_expression(node->as.for_stmt.update);
        emit_byte(OP_POP);  // Pop the result since it's not used
    }
    
    // Jump back to condition
    emit_loop(loop_start);
    
    // Patch exit jump (if we had a condition)
    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }
}

static void compile_return_stmt(ASTNode* node) {
    if (node->as.return_stmt.value) {
        compile_expression(node->as.return_stmt.value);
    } else {
        emit_byte(OP_NIL);
    }
    emit_byte(OP_RETURN);
}

static void compile_block(ASTNode* node) {
    begin_scope();
    
    for (int i = 0; i < node->as.block.count; i++) {
        compile_statement(node->as.block.statements[i]);
    }
    
    end_scope();
}

static void compile_expression_stmt(ASTNode* node) {
    compile_expression(node->as.expression);
    emit_byte(OP_POP); // Pop expression result
}

static void compile_statement(ASTNode* node) {
    if (node == NULL) return;
    
    switch (node->type) {
        case AST_VAR_DECL:
            compile_var_decl(node);
            break;
        case AST_FUNCTION_DECL:
            compile_function_decl(node);
            break;
        case AST_IF_STMT:
            compile_if_stmt(node);
            break;
        case AST_WHILE_STMT:
            compile_while_stmt(node);
            break;
        case AST_FOR_STMT:
            compile_for_stmt(node);
            break;
        case AST_RETURN_STMT:
            compile_return_stmt(node);
            break;
        case AST_BLOCK:
            compile_block(node);
            break;
        case AST_EXPRESSION_STMT:
            compile_expression_stmt(node);
            break;
        default:
            error_at_node(node, "Unknown statement type.");
    }
}

void compiler_init(Compiler* compiler, const char* name) {
    compiler->had_error = false;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->enclosing = (struct Compiler*)current;
    
    compiler->function = malloc(sizeof(Function));
    compiler->function->name = strdup(name);
    compiler->function->arity = 0;
    chunk_init(&compiler->function->chunk);
    compiler->function->is_native = false;
    
    compiler->chunk = &compiler->function->chunk;
    
    current = compiler;
}

Function* compiler_compile(ASTNode* ast) {
    Compiler compiler;
    compiler_init(&compiler, "script");
    
    compile_statement(ast);
    
    emit_byte(OP_HALT);
    
    current = (Compiler*)compiler.enclosing;
    
    if (compiler.had_error) {
        chunk_free(&compiler.function->chunk);
        free(compiler.function->name);
        free(compiler.function);
        return NULL;
    }
    
    return compiler.function;
}

void compiler_free(Compiler* compiler) {
    if (compiler->function) {
        chunk_free(&compiler->function->chunk);
        free(compiler->function->name);
        free(compiler->function);
    }
}

// Disassembly
static int simple_instruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int constant_instruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    
    Value value = chunk->constants[constant];
    switch (value.type) {
        case VALUE_NIL: printf("nil"); break;
        case VALUE_BOOL: printf(value.as.boolean ? "true" : "false"); break;
        case VALUE_NUMBER: printf("%g", value.as.number); break;
        case VALUE_STRING: printf("%s", value.as.string); break;
        case VALUE_ARRAY: printf("[Array]"); break;
        case VALUE_OBJECT: printf("{Object}"); break;
        case VALUE_FUNCTION: printf("<function>"); break;
    }
    
    printf("'\n");
    return offset + 2;
}

static int byte_instruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int jump_instruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int disassemble_instruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }
    
    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constant_instruction("OP_CONSTANT", chunk, offset);
        case OP_NIL:
            return simple_instruction("OP_NIL", offset);
        case OP_TRUE:
            return simple_instruction("OP_TRUE", offset);
        case OP_FALSE:
            return simple_instruction("OP_FALSE", offset);
        case OP_POP:
            return simple_instruction("OP_POP", offset);
        case OP_ADD:
            return simple_instruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simple_instruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simple_instruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simple_instruction("OP_DIVIDE", offset);
        case OP_NEGATE:
            return simple_instruction("OP_NEGATE", offset);
        case OP_EQUAL:
            return simple_instruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simple_instruction("OP_GREATER", offset);
        case OP_LESS:
            return simple_instruction("OP_LESS", offset);
        case OP_NOT:
            return simple_instruction("OP_NOT", offset);
        case OP_DEFINE_GLOBAL:
            return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_LOCAL:
            return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_JUMP:
            return jump_instruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:
            return jump_instruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return byte_instruction("OP_CALL", chunk, offset);
        case OP_RETURN:
            return simple_instruction("OP_RETURN", offset);
        case OP_HALT:
            return simple_instruction("OP_HALT", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void disassemble_chunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    
    for (int offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction(chunk, offset);
    }
}

// Function object functions
Function* function_new() {
    Function* function = malloc(sizeof(Function));
    function->name = NULL;
    function->arity = 0;
    chunk_init(&function->chunk);
    function->is_native = false;
    return function;
}

void function_free(Function* function) {
    if (function == NULL) return;
    
    free(function->name);
    chunk_free(&function->chunk);
    free(function);
}