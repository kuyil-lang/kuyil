#define _POSIX_C_SOURCE 200809L
#include "bytecode.h"
#include "logging.h"
#include "vm_library_integration.h" // for get_current_source_path()
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
        
        // Prevent integer overflow
        if (chunk->capacity < old_capacity) {
            LOG_ERROR("Chunk capacity overflow");
            return;
        }
        
        uint8_t* new_code = realloc(chunk->code, sizeof(uint8_t) * chunk->capacity);
        int* new_lines = realloc(chunk->lines, sizeof(int) * chunk->capacity);
        
        if (!new_code || !new_lines) {
            LOG_ERROR("Out of memory expanding chunk (size: %d)", chunk->capacity);
            // Restore old capacity and abort write
            chunk->capacity = old_capacity;
            if (new_code) chunk->code = new_code;
            if (new_lines) chunk->lines = new_lines;
            return;
        }
        
        chunk->code = new_code;
        chunk->lines = new_lines;
    }
    
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int chunk_add_constant(Chunk* chunk, Value value) {
    if (chunk->constant_capacity < chunk->constant_count + 1) {
        int old_capacity = chunk->constant_capacity;
        chunk->constant_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        
        // Prevent integer overflow
        if (chunk->constant_capacity < old_capacity) {
            LOG_ERROR("Constant pool capacity overflow");
            return -1;
        }
        
        Value* new_constants = realloc(chunk->constants, sizeof(Value) * chunk->constant_capacity);
        if (!new_constants) {
            LOG_ERROR("Out of memory expanding constant pool (size: %d)", chunk->constant_capacity);
            chunk->constant_capacity = old_capacity;
            return -1;
        }
        chunk->constants = new_constants;
    }
    
    // CRITICAL FIX: Make deep copy of strings to survive AST cleanup
    if (value.type == VALUE_STRING) {
        Value string_copy = value;
        char* copied_str = strdup(value.as.string);
        if (!copied_str) {
            LOG_ERROR("Out of memory duplicating string constant");
            return -1;
        }
        string_copy.as.string = copied_str; // Deep copy!
        chunk->constants[chunk->constant_count] = string_copy;
    } else {
        chunk->constants[chunk->constant_count] = value;
    }
    
    return chunk->constant_count++;
}

// Compiler state
static Compiler* current = NULL;
static int current_line = 0;  // Track current source line for bytecode emission

// Track exported functions during compilation
static char** g_exported_functions = NULL;
static int g_exported_function_count = 0;
static int g_exported_function_capacity = 0;

// Track interface namespaces during compilation
static char** g_interface_namespaces = NULL;
static int g_interface_namespace_count = 0;
static int g_interface_namespace_capacity = 0;

static void add_interface_namespace(const char* name) {
    // Check if already registered
    for (int i = 0; i < g_interface_namespace_count; i++) {
        if (strcmp(g_interface_namespaces[i], name) == 0) return;
    }
    
    if (g_interface_namespace_count >= g_interface_namespace_capacity) {
        int old_capacity = g_interface_namespace_capacity;
        g_interface_namespace_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        g_interface_namespaces = realloc(g_interface_namespaces, 
                                        sizeof(char*) * g_interface_namespace_capacity);
    }
    g_interface_namespaces[g_interface_namespace_count++] = strdup(name);
}

static bool is_interface_namespace(const char* name) {
    for (int i = 0; i < g_interface_namespace_count; i++) {
        if (strcmp(g_interface_namespaces[i], name) == 0) return true;
    }
    return false;
}

static void add_exported_function(const char* name) {
    if (g_exported_function_count >= g_exported_function_capacity) {
        int old_capacity = g_exported_function_capacity;
        g_exported_function_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        g_exported_functions = realloc(g_exported_functions, 
                                      sizeof(char*) * g_exported_function_capacity);
    }
    g_exported_functions[g_exported_function_count++] = strdup(name);
}

void compiler_clear_exports() {
    for (int i = 0; i < g_exported_function_count; i++) {
        free(g_exported_functions[i]);
    }
    g_exported_function_count = 0;
}

const char** compiler_get_exports(int* count) {
    *count = g_exported_function_count;
    return (const char**)g_exported_functions;
}

static void error_at_node(ASTNode* node, const char* message) {
    LOG_ERROR("[line %d] %s", node->line, message);
    current->had_error = true;
}

static void emit_byte(uint8_t byte) {
    chunk_write(current->chunk, byte, current_line);
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

static int make_constant(Value value) {
    int constant = chunk_add_constant(current->chunk, value);
    if (constant > UINT16_MAX) {
        error_at_node(NULL, "Too many constants in one chunk.");
        return 0;
    }
    
    return constant;
}

static void emit_constant(Value value) {
    int constant = chunk_add_constant(current->chunk, value);
    if (constant > UINT16_MAX) {
        error_at_node(NULL, "Too many constants in one chunk.");
        return;
    }
    
    if (constant <= UINT8_MAX) {
        emit_bytes(OP_CONSTANT, (uint8_t)constant);
    } else {
        // Use 16-bit constant index
        emit_byte(OP_CONSTANT_LONG);
        emit_byte((constant >> 8) & 0xff);
        emit_byte(constant & 0xff);
    }
}

// Variable management
static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;
    
    // With proper SET_LOCAL/GET_LOCAL implementation:
    // - Locals are stored in frame->slots[], NOT on the expression stack
    // - When a local goes out of scope, we just mark its slot as reusable
    // - We do NOT emit OP_POP because locals aren't on the expression stack
    // - OP_POP is only for cleaning up temporary expression values
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        // Do NOT emit OP_POP - locals are in slots, not on expression stack
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
    int index = current->local_count++;
    
    // Track peak local count
    if (current->local_count > current->max_local_count) {
        current->max_local_count = current->local_count;
    }
    
    return index;
}

static int resolve_local(const char* name) {
    for (int i = current->local_count - 1; i >= 0; i--) {
        if (strcmp(current->locals[i].name, name) == 0) {
            return i;
        }
    }
    
    return -1;
}

static int identifier_constant(const char* name) {
    Value value;
    value.type = VALUE_STRING;
    value.as.string = strdup(name);
    return make_constant(value);
}

// Forward declarations
static void compile_expression(ASTNode* node);
static void compile_statement(ASTNode* node);
static void compile_anonymous_function(ASTNode* node);
static void compile_block(ASTNode* node);
static void compile_var_decl(ASTNode* node);
static void compile_function_decl(ASTNode* node);
static void compile_struct_decl(ASTNode* node);
static void compile_interface_decl(ASTNode* node);
static void compile_method_decl(ASTNode* node);
static void compile_if_stmt(ASTNode* node);
static void compile_while_stmt(ASTNode* node);
static void compile_for_stmt(ASTNode* node);
static void compile_switch_stmt(ASTNode* node);
static void compile_return_stmt(ASTNode* node);
static void compile_avatar_stmt(ASTNode* node);
static void compile_await_expr(ASTNode* node);

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
        int name = identifier_constant(node->as.identifier);
        if (name <= UINT8_MAX) {
            emit_bytes(OP_GET_GLOBAL, (uint8_t)name);
        } else {
            emit_byte(OP_GET_GLOBAL_LONG);
            emit_byte((name >> 8) & 0xff);
            emit_byte(name & 0xff);
        }
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
    // Method call support: obj.method(a, b)
    if (node->as.call.function->type == AST_MEMBER_ACCESS) {
        // When object is a simple identifier, this could be either:
        // 1. An interface namespace call: file.readText() where file is NOT a variable
        // 2. A regular method call: strVar.replace() where strVar IS a variable
        // Check if it's a local variable first
        if (node->as.call.function->as.member.object->type == AST_IDENTIFIER) {
            const char* obj_name = node->as.call.function->as.member.object->as.identifier;
            const char* method_name = node->as.call.function->as.member.property;
            
            // Determine if this is a namespace call or method call
            // Check if obj_name is a registered interface namespace
            bool is_namespace = is_interface_namespace(obj_name);
            
            if (is_namespace) {
                // Compile as qualified namespace call
                // Build dotted name: "obj.method"
                size_t dotted_len = strlen(obj_name) + 1 + strlen(method_name) + 1;
                char* dotted_name = malloc(dotted_len);
                snprintf(dotted_name, dotted_len, "%s.%s", obj_name, method_name);
                
                // Compile arguments
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    compile_expression(node->as.call.args[i]);
                }
                
                // Push the dotted function name as a constant
                Value func_value;
                func_value.type = VALUE_STRING;
                func_value.as.string = dotted_name;
                emit_constant(func_value);
                emit_bytes(OP_CALL, node->as.call.arg_count);
                // Don't free dotted_name - it's stored in the constant pool
                return;
            }
            
            // Not a namespace - compile as method call
            // Regular method call: receiver first
            compile_expression(node->as.call.function->as.member.object);
            // Then other arguments
            for (int i = 0; i < node->as.call.arg_count; i++) {
                compile_expression(node->as.call.args[i]);
            }
            // Callee as string name
            Value method_name_val;
            method_name_val.type = VALUE_STRING;
            method_name_val.as.string = method_name;
            emit_constant(method_name_val);
            emit_bytes(OP_CALL, node->as.call.arg_count + 1);
            return;
        }
        
        // Regular method call with complex object expression: (expr).method(a, b)
        // Receiver first
        compile_expression(node->as.call.function->as.member.object);
        // Then other arguments
        for (int i = 0; i < node->as.call.arg_count; i++) {
            compile_expression(node->as.call.args[i]);
        }
        // Callee as string name (dispatch via dynamic or global by name)
        Value method_name;
        method_name.type = VALUE_STRING;
        method_name.as.string = node->as.call.function->as.member.property;
        emit_constant(method_name);
        emit_bytes(OP_CALL, node->as.call.arg_count + 1);
        return;
    }
    // Check if this is a logging function call or compiler directive
    if (node->as.call.function->type == AST_IDENTIFIER) {
        const char* func_name = node->as.call.function->as.identifier;
        
        // Compiler directive: __register_namespace("name")
        if (strcmp(func_name, "__register_namespace") == 0) {
            if (node->as.call.arg_count == 1 && 
                node->as.call.args[0]->type == AST_LITERAL &&
                node->as.call.args[0]->as.literal.type == VALUE_STRING) {
                const char* namespace_name = node->as.call.args[0]->as.literal.as.string;
                add_interface_namespace(namespace_name);
                // Don't emit any bytecode - this is compile-time only
                return;
            }
        }
        
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
    // Destructuring assignment: [a, b, c] = expr
    if (node->as.assignment.target->type == AST_ARRAY_LITERAL && node->as.assignment.operator == TOKEN_ASSIGN) {
        ArrayLiteral* lhs = &node->as.assignment.target->as.array_literal;
        // Compile RHS once (should evaluate to an array)
        compile_expression(node->as.assignment.value);
        // For each identifier on LHS, extract element and assign
        for (int i = 0; i < lhs->count; i++) {
            ASTNode* el = lhs->elements[i];
            if (el->type != AST_IDENTIFIER) {
                error_at_node(el, "Destructuring target must be identifiers.");
                return;
            }
            // Duplicate RHS array on stack
            emit_byte(OP_DUP);
            // Push index constant
            Value idxv; idxv.type = VALUE_NUMBER; idxv.as.number = (double)i;
            emit_constant(idxv);
            // Get element
            emit_byte(OP_ARRAY_GET);
            // Assign to identifier
            const char* name = el->as.identifier;
            int local = resolve_local(name);
            if (local != -1) {
                emit_bytes(OP_SET_LOCAL, (uint8_t)local);
            } else {
                int name_constant = identifier_constant(name);
                if (name_constant <= UINT8_MAX) {
                    emit_bytes(OP_SET_GLOBAL, (uint8_t)name_constant);
                } else {
                    emit_byte(OP_SET_GLOBAL_LONG);
                    emit_byte((name_constant >> 8) & 0xff);
                    emit_byte(name_constant & 0xff);
                }
            }
            // Pop assigned value, leave RHS array for next iteration
            emit_byte(OP_POP);
        }
        // After destructuring, leave RHS array on stack as the assignment expression result
        return;
    }

    // Normal assignment (supports identifiers, array[index], and object.member)
    if (node->as.assignment.target->type == AST_IDENTIFIER) {
        const char* name = node->as.assignment.target->as.identifier;
        int local = resolve_local(name);
        
        // Handle compound assignments (+=, -=)
        if (node->as.assignment.operator == TOKEN_PLUS_ASSIGN || 
            node->as.assignment.operator == TOKEN_MINUS_ASSIGN) {
            // First get the current value
            if (local != -1) {
                emit_bytes(OP_GET_LOCAL, (uint8_t)local);
            } else {
                int name_constant = identifier_constant(name);
                if (name_constant <= UINT8_MAX) {
                    emit_bytes(OP_GET_GLOBAL, (uint8_t)name_constant);
                } else {
                    emit_byte(OP_GET_GLOBAL_LONG);
                    emit_byte((name_constant >> 8) & 0xff);
                    emit_byte(name_constant & 0xff);
                }
            }
            // Then compile the RHS value
            compile_expression(node->as.assignment.value);
            // Emit the operation
            if (node->as.assignment.operator == TOKEN_PLUS_ASSIGN) {
                emit_byte(OP_ADD);
            } else {
                emit_byte(OP_SUBTRACT);
            }
        } else {
            // Regular assignment: <name> = <value>
            compile_expression(node->as.assignment.value);
        }
        
        // Now store the value
        if (local != -1) {
            emit_bytes(OP_SET_LOCAL, (uint8_t)local);
        } else {
            int name_constant = identifier_constant(name);
            if (name_constant <= UINT8_MAX) {
                emit_bytes(OP_SET_GLOBAL, (uint8_t)name_constant);
            } else {
                emit_byte(OP_SET_GLOBAL_LONG);
                emit_byte((name_constant >> 8) & 0xff);
                emit_byte(name_constant & 0xff);
            }
        }
    } else if (node->as.assignment.target->type == AST_ARRAY_ACCESS) {
        // For array[index] = value, we need stack: ..., value, array, index
        compile_expression(node->as.assignment.value);
        compile_expression(node->as.assignment.target->as.array_access.array);
        compile_expression(node->as.assignment.target->as.array_access.index);
        emit_byte(OP_ARRAY_SET);
    } else if (node->as.assignment.target->type == AST_MEMBER_ACCESS) {
        // For object.prop = value, we need stack: ..., object, key, value
        ASTNode* obj = node->as.assignment.target->as.member.object;
        const char* prop = node->as.assignment.target->as.member.property;
        // Push object
        compile_expression(obj);
        // Push key (as string constant)
        Value keyv; keyv.type = VALUE_STRING; keyv.as.string = (char*)prop;
        emit_constant(keyv);
        // Now push value
        compile_expression(node->as.assignment.value);
        // Set property
        emit_byte(OP_OBJECT_SET);
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

static void compile_object_literal(ASTNode* node) {
    // Create empty object
    emit_byte(OP_OBJECT_NEW);
    
    // For each property, set it on the object
    for (int i = 0; i < node->as.object_literal.count; i++) {
        // Duplicate the object reference on stack for each property set
        if (i < node->as.object_literal.count - 1) {
            emit_byte(OP_DUP);
        }
        
        // Push property name as constant
        Value name_val;
        name_val.type = VALUE_STRING;
        name_val.as.string = node->as.object_literal.properties[i].key;
        int name_constant = make_constant(name_val);
        emit_bytes(OP_CONSTANT, (uint8_t)name_constant);
        
        // Compile property value
        compile_expression(node->as.object_literal.properties[i].value);
        
        // Set the property (consumes object, name, value from stack)
        emit_byte(OP_OBJECT_SET);
    }
    
    // At the end, the object reference is still on the stack
}

static void compile_array_access(ASTNode* node) {
    // Compile array expression
    compile_expression(node->as.array_access.array);
    
    // Compile index expression
    compile_expression(node->as.array_access.index);
    
    // Emit OP_ARRAY_GET
    emit_byte(OP_ARRAY_GET);
}

static void compile_member_access(ASTNode* node) {
    // DEBUG: Log member access compilation
    fprintf(stderr, "[COMPILE] Member access: %s.%s\n", 
            node->as.member.object->type == AST_IDENTIFIER ? "identifier" : "other",
            node->as.member.property);
    
    // Compile object expression
    compile_expression(node->as.member.object);
    
    // Push property name as a constant string
    Value property_name;
    property_name.type = VALUE_STRING;
    property_name.as.string = node->as.member.property;
    emit_constant(property_name);
    
    // Emit OP_OBJECT_GET
    emit_byte(OP_OBJECT_GET);
}

static void compile_struct_literal(ASTNode* node) {
    // Create a new empty object and leave it on the stack
    emit_byte(OP_OBJECT_NEW);

    // Attach hidden type tag for method dispatch: __type = "StructName"
    if (node->as.struct_literal.name) {
        // Push key "__type"
        Value type_key; type_key.type = VALUE_STRING; type_key.as.string = "__type";
        emit_constant(type_key);
        // Push value struct name as string
        Value type_val; type_val.type = VALUE_STRING; type_val.as.string = node->as.struct_literal.name;
        emit_constant(type_val);
        // Set property; leaves the mutated object on top
        emit_byte(OP_OBJECT_SET);
    }

    // For each field, set the property on the object.
    // VM expects for OP_OBJECT_SET to see (from bottom to top): object, key, value
    // and will pop value, key, object (in that order), mutate, and push the object back.
    for (int i = 0; i < node->as.struct_literal.field_count; i++) {
        // Push key (a string literal node created by the parser)
        compile_expression(node->as.struct_literal.field_values[i * 2]);

        // Push value expression
        compile_expression(node->as.struct_literal.field_values[i * 2 + 1]);

        // Set property; leaves the mutated object on top for the next iteration
        emit_byte(OP_OBJECT_SET);
    }
}

static void compile_expression(ASTNode* node) {
    if (node == NULL) return;
    
    // Update current line from node for accurate bytecode line tracking
    current_line = node->line;
    
    switch (node->type) {
        case AST_ERROR:
            // Parse error - compiler should bail out
            // Error already reported by parser
            break;
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
            // Debug: fprintf(stderr, "[COMPILE_EXPR] AST_CALL node detected!\n"); fflush(stderr);
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
        case AST_OBJECT_LITERAL:
            compile_object_literal(node);
            break;
        case AST_STRUCT_LITERAL:
            compile_struct_literal(node);
            break;
        case AST_ARRAY_ACCESS:
            compile_array_access(node);
            break;
        case AST_MEMBER_ACCESS:
            compile_member_access(node);
            break;
        case AST_AWAIT_EXPR:
            compile_await_expr(node);
            break;
        case AST_AVATAR_STMT:
            compile_avatar_stmt(node);
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
    
    // Store local count for runtime stack management
    function->local_count = function_compiler.max_local_count;
    
    // Restore compiler state
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL; // No closure for now
    function_value.as.function.capture_count = 0;
    
    // Use emit_constant to handle both 8-bit and 16-bit indices
    emit_constant(function_value);
}

static void compile_var_decl(ASTNode* node) {
    if (current->scope_depth > 0) {
        // Local variable - Standard VM semantics:
        // 1. Locals are stored in frame->slots[], not on the expression stack
        // 2. Must emit SET_LOCAL to store value in the slot
        // 3. SET_LOCAL peeks (doesn't pop), so emit POP to clean up expression stack
        
        // First, add the local to get its slot number
        int slot = current->local_count;  // Slot number before add_local increments it
        add_local(node->as.var_decl.name);
        
        // Compile the initializer expression (pushes value onto expression stack)
        if (node->as.var_decl.value) {
            compile_expression(node->as.var_decl.value);
        } else {
            emit_byte(OP_NIL);
        }
        
        // Store the value from expression stack into the local slot
        emit_bytes(OP_SET_LOCAL, (uint8_t)slot);
        
        // Pop the value from expression stack (SET_LOCAL only peeks, doesn't pop)
        emit_byte(OP_POP);
    } else {
        // Global variable
        int global = identifier_constant(node->as.var_decl.name);
        
        if (node->as.var_decl.value) {
            compile_expression(node->as.var_decl.value);
        } else {
            emit_byte(OP_NIL);
        }
        
        if (global <= UINT8_MAX) {
            emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)global);
        } else {
            emit_byte(OP_DEFINE_GLOBAL_LONG);
            emit_byte((global >> 8) & 0xff);
            emit_byte(global & 0xff);
        }
    }
}

static void compile_function_decl(ASTNode* node) {
    // Push function context for logging
    int name_constant = identifier_constant(node->as.function_decl.name);
    // For OP_LOG_PUSH_CTX, we'll keep it as 8-bit for now (logging contexts usually don't exceed 255)
    if (name_constant > UINT8_MAX) {
        error_at_node(node, "Too many constants for function name in logging context");
        return;
    }
    emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    
    // Track if this function is exported
    if (node->as.function_decl.is_exported) {
        add_exported_function(node->as.function_decl.name);
    }
    
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
    
    // Store local count for runtime stack management
    function->local_count = function_compiler.max_local_count;
    
    // Restore compiler state
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL; // No closure for now
    function_value.as.function.capture_count = 0;
    
    // Use emit_constant to handle both 8-bit and 16-bit indices
    emit_constant(function_value);
    
    // Define the function as a global variable
    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }
    
    // Pop context when function exits
    emit_byte(OP_LOG_POP_CTX);
}

// Helper to generate toJSON method for a struct
static void generate_toJSON_method(const char* struct_name, char** field_names, int field_count) {
    char method_name[256];
    snprintf(method_name, sizeof(method_name), "%s_toJSON", struct_name);
    
    int name_constant = identifier_constant(method_name);
    bool push_log_ctx = name_constant <= UINT8_MAX;
    if (push_log_ctx) {
        emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    }
    
    Compiler* enclosing = current;
    Compiler method_compiler;
    compiler_init(&method_compiler, method_name);
    
    method_compiler.function->arity = 1; // Just 'this'
    
    begin_scope();
    add_local("this");
    
    // Build JSON string: result = "{" + "field1": " + this.field1 + ", " + ... + "}"
    // Start with empty string
    emit_byte(OP_NIL);
    int result_local = current->local_count;
    add_local("__json_result");
    emit_byte(OP_POP);
    
    // Push opening brace "{"
    Value open_brace;
    open_brace.type = VALUE_STRING;
    open_brace.as.string = strdup("{");
    emit_constant(open_brace);
    emit_byte(OP_SET_LOCAL);
    emit_byte(result_local);
    emit_byte(OP_POP);
    
    // For each field, append: "fieldName": value,
    for (int i = 0; i < field_count; i++) {
        // Get current result
        emit_byte(OP_GET_LOCAL);
        emit_byte(result_local);
        
        // Add field name with quotes and colon
        Value field_prefix;
        field_prefix.type = VALUE_STRING;
        char prefix_buf[128];
        snprintf(prefix_buf, sizeof(prefix_buf), "\"%s\": ", field_names[i]);
        field_prefix.as.string = strdup(prefix_buf);
        emit_constant(field_prefix);
        emit_byte(OP_ADD);
        
        // Get field value from this.field
        emit_byte(OP_GET_LOCAL);
        emit_byte(0); // 'this' is always local 0
        
        Value field_name_val;
        field_name_val.type = VALUE_STRING;
        field_name_val.as.string = strdup(field_names[i]);
        emit_constant(field_name_val);
        emit_byte(OP_OBJECT_GET);
        
        // Convert to string and wrap in quotes if needed
        emit_byte(OP_TO_STRING);
        
        // Add to result
        emit_byte(OP_ADD);
        
        // Add comma if not last field
        if (i < field_count - 1) {
            Value comma;
            comma.type = VALUE_STRING;
            comma.as.string = strdup(", ");
            emit_constant(comma);
            emit_byte(OP_ADD);
        }
        
        // Store back to result
        emit_byte(OP_SET_LOCAL);
        emit_byte(result_local);
        emit_byte(OP_POP);
    }
    
    // Get result and append closing brace
    emit_byte(OP_GET_LOCAL);
    emit_byte(result_local);
    
    Value close_brace;
    close_brace.type = VALUE_STRING;
    close_brace.as.string = strdup("}");
    emit_constant(close_brace);
    emit_byte(OP_ADD);
    
    emit_byte(OP_RETURN);
    end_scope();
    
    Function* function = method_compiler.function;
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL;
    function_value.as.function.capture_count = 0;
    
    emit_constant(function_value);
    
    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }
    
    if (push_log_ctx) emit_byte(OP_LOG_POP_CTX);
}

// Helper to generate fromJSON method for a struct
// Signature: <Struct>_fromJSON(data: object) -> object (new instance with __type set and fields copied)
static void generate_fromJSON_method(const char* struct_name, char** field_names, int field_count) {
    char method_name[256];
    snprintf(method_name, sizeof(method_name), "%s_fromJSON", struct_name);

    int name_constant = identifier_constant(method_name);
    bool push_log_ctx = name_constant <= UINT8_MAX;
    if (push_log_ctx) {
        emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    }

    Compiler* enclosing = current;
    Compiler method_compiler;
    compiler_init(&method_compiler, method_name);

    // fromJSON takes one parameter: data (the plain object/map parsed from JSON)
    method_compiler.function->arity = 1;

    begin_scope();
    add_local("data"); // local 0

    // Construct a new instance object
    emit_byte(OP_OBJECT_NEW);

    // __type = "StructName"
    {
        Value type_key; type_key.type = VALUE_STRING; type_key.as.string = "__type";
        emit_constant(type_key);
        Value type_val; type_val.type = VALUE_STRING; type_val.as.string = (char*)struct_name;
        emit_constant(type_val);
        emit_byte(OP_OBJECT_SET);
    }

    // For each field: obj[field] = data[field]
    for (int i = 0; i < field_count; i++) {
        // Push destination key
        Value keyv; keyv.type = VALUE_STRING; keyv.as.string = field_names[i];
        emit_constant(keyv);

        // Compute value: data[field]
        emit_byte(OP_GET_LOCAL); // push 'data'
        emit_byte(0);
        Value keyv2; keyv2.type = VALUE_STRING; keyv2.as.string = field_names[i];
        emit_constant(keyv2);
        emit_byte(OP_OBJECT_GET); // leaves value on stack

        // Set: consumes value, key, object; pushes object back
        emit_byte(OP_OBJECT_SET);
    }

    // Return the constructed object (on top of the stack)
    emit_byte(OP_RETURN);

    end_scope();

    Function* function = method_compiler.function;
    current = enclosing;

    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL;
    function_value.as.function.capture_count = 0;

    emit_constant(function_value);

    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }

    if (push_log_ctx) emit_byte(OP_LOG_POP_CTX);
}

// Helper to generate toYAML method for a struct
// Signature: <Struct>_toYAML(this) -> string
static void generate_toYAML_method(const char* struct_name, char** field_names, int field_count) {
    char method_name[256];
    snprintf(method_name, sizeof(method_name), "%s_toYAML", struct_name);

    int name_constant = identifier_constant(method_name);
    bool push_log_ctx = name_constant <= UINT8_MAX;
    if (push_log_ctx) {
        emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    }

    Compiler* enclosing = current;
    Compiler method_compiler;
    compiler_init(&method_compiler, method_name);

    method_compiler.function->arity = 1; // 'this'

    begin_scope();
    add_local("this");

    // Build YAML by concatenating per-field lines on the stack
    bool first_done = false; // compile-time flag to structure emission
    for (int i = 0; i < field_count; i++) {
        // Push "key: "
        Value prefix; prefix.type = VALUE_STRING;
        char buf[128]; snprintf(buf, sizeof(buf), "%s: ", field_names[i]);
        prefix.as.string = strdup(buf);
        emit_constant(prefix);
        
        // Append value: this.field
        emit_byte(OP_GET_LOCAL); emit_byte(0);
        Value key; key.type = VALUE_STRING; key.as.string = strdup(field_names[i]);
        emit_constant(key);
        emit_byte(OP_OBJECT_GET);
        emit_byte(OP_TO_STRING);
        emit_byte(OP_ADD);
        
        // Append newline
        Value nl; nl.type = VALUE_STRING; nl.as.string = strdup("\n");
        emit_constant(nl);
        emit_byte(OP_ADD);
        
        if (i > 0) {
            // Combine: accumulator (below) + current line (top)
            emit_byte(OP_ADD);
        }
    }

    // Return the accumulated string (top of stack)
    emit_byte(OP_RETURN);

    end_scope();

    Function* function = method_compiler.function;
    current = enclosing;

    Value function_value; function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL;
    function_value.as.function.capture_count = 0;
    emit_constant(function_value);

    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }

    if (push_log_ctx) emit_byte(OP_LOG_POP_CTX);
}

// Helper to generate fromYAML (same as fromJSON: from a plain object/map)
static void generate_fromYAML_method(const char* struct_name, char** field_names, int field_count) {
    char method_name[256];
    snprintf(method_name, sizeof(method_name), "%s_fromYAML", struct_name);

    int name_constant = identifier_constant(method_name);
    bool push_log_ctx = name_constant <= UINT8_MAX;
    if (push_log_ctx) {
        emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    }

    Compiler* enclosing = current;
    Compiler method_compiler;
    compiler_init(&method_compiler, method_name);

    method_compiler.function->arity = 1; // data

    begin_scope();
    add_local("data"); // 0

    emit_byte(OP_OBJECT_NEW);
    // __type
    {
        Value type_key; type_key.type = VALUE_STRING; type_key.as.string = "__type";
        emit_constant(type_key);
        Value type_val; type_val.type = VALUE_STRING; type_val.as.string = (char*)struct_name;
        emit_constant(type_val);
        emit_byte(OP_OBJECT_SET);
    }
    for (int i = 0; i < field_count; i++) {
        Value k; k.type = VALUE_STRING; k.as.string = field_names[i];
        emit_constant(k);
        emit_byte(OP_GET_LOCAL); emit_byte(0);
        Value k2; k2.type = VALUE_STRING; k2.as.string = field_names[i];
        emit_constant(k2);
        emit_byte(OP_OBJECT_GET);
        emit_byte(OP_OBJECT_SET);
    }
    emit_byte(OP_RETURN);
    end_scope();

    Function* function = method_compiler.function;
    current = enclosing;
    Value function_value; function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL;
    function_value.as.function.capture_count = 0;
    emit_constant(function_value);
    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }
    if (push_log_ctx) emit_byte(OP_LOG_POP_CTX);
}

static void compile_struct_decl(ASTNode* node) {
    // Automatically generate toJSON method for the struct
    generate_toJSON_method(
        node->as.struct_decl.name,
        node->as.struct_decl.fields,
        node->as.struct_decl.field_count
    );
    
    // Also generate fromJSON method to construct instances from plain objects
    generate_fromJSON_method(
        node->as.struct_decl.name,
        node->as.struct_decl.fields,
        node->as.struct_decl.field_count
    );
    
    // Generate YAML helpers (toYAML stringifier and fromYAML constructor)
    generate_toYAML_method(
        node->as.struct_decl.name,
        node->as.struct_decl.fields,
        node->as.struct_decl.field_count
    );
    generate_fromYAML_method(
        node->as.struct_decl.name,
        node->as.struct_decl.fields,
        node->as.struct_decl.field_count
    );
    
    // TODO: Store struct definition in type registry for runtime reflection
}

static void compile_interface_decl(ASTNode* node) {
    // Interfaces are compile-time only for type checking
    // No runtime bytecode is needed
    
    (void)node; // Suppress unused parameter warning
    // No bytecode emission - interface definitions are compile-time only
}

static void compile_method_decl(ASTNode* node) {
    // Methods are compiled like functions but stored with type association
    // For now, we'll name them as "TypeName_methodName" to simulate method binding
    
    char method_full_name[256];
    snprintf(method_full_name, sizeof(method_full_name), "%s_%s",
             node->as.method_decl.receiver_type,
             node->as.method_decl.method_name);
    
    int name_constant = identifier_constant(method_full_name);
    if (name_constant > UINT8_MAX) {
        error_at_node(node, "Too many constants for method name");
        return;
    }
    emit_bytes(OP_LOG_PUSH_CTX, (uint8_t)name_constant);
    
    Compiler* enclosing = current;
    Compiler method_compiler;
    compiler_init(&method_compiler, method_full_name);
    
    method_compiler.function->arity = node->as.method_decl.param_count + 1; // +1 for 'this'
    
    begin_scope();
    
    // Add 'this' as first parameter
    add_local("this");
    
    // Add regular parameters
    for (int i = 0; i < node->as.method_decl.param_count; i++) {
        add_local(node->as.method_decl.params[i]);
    }
    
    // Compile method body
    if (node->as.method_decl.body) {
        for (int i = 0; i < node->as.method_decl.body->count; i++) {
            compile_statement(node->as.method_decl.body->statements[i]);
        }
    }
    
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
    
    end_scope();
    
    Function* function = method_compiler.function;
    current = enclosing;
    
    Value function_value;
    function_value.type = VALUE_FUNCTION;
    function_value.as.function.function = function;
    function_value.as.function.captured_vars = NULL;
    function_value.as.function.capture_count = 0;
    
    emit_constant(function_value);
    
    if (name_constant <= UINT8_MAX) {
        emit_bytes(OP_DEFINE_GLOBAL, (uint8_t)name_constant);
    } else {
        emit_byte(OP_DEFINE_GLOBAL_LONG);
        emit_byte((name_constant >> 8) & 0xff);
        emit_byte(name_constant & 0xff);
    }
    
    emit_byte(OP_LOG_POP_CTX);
}

static void compile_switch_stmt(ASTNode* node) {
    // Compile the switch value once
    compile_expression(node->as.switch_stmt.value);
    
    int case_count = node->as.switch_stmt.case_count;
    
    // Bounds check case_count
    if (case_count < 0 || case_count > 10000) {
        LOG_ERROR("Invalid switch case count: %d", case_count);
        return;
    }
    
    int* end_jumps = malloc(sizeof(int) * (case_count + 1));
    if (!end_jumps) {
        LOG_ERROR("Out of memory allocating switch jumps");
        return;
    }
    int end_jump_count = 0;
    
    // For each case, test equality and skip to next test if not equal
    for (int i = 0; i < case_count; i++) {
        // Duplicate switch value for comparison
        emit_byte(OP_DUP);
        
        // Compile case value
        compile_expression(node->as.switch_stmt.case_values[i]);
        
        // Compare
        emit_byte(OP_EQUAL);
        
        // If not equal, jump to next case test
        int skip_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP); // Pop comparison result (true)
        emit_byte(OP_POP); // Pop original switch value
        
        // Compile case body
        compile_statement(node->as.switch_stmt.case_bodies[i]);
        
        // Jump to end (no fallthrough)
        end_jumps[end_jump_count++] = emit_jump(OP_JUMP);
        
        // Patch skip to next case
        patch_jump(skip_jump);
        emit_byte(OP_POP); // Pop comparison result (false)
    }
    
    // If no cases matched and we have a default, execute it
    emit_byte(OP_POP); // Pop original switch value
    if (node->as.switch_stmt.default_body != NULL) {
        compile_statement(node->as.switch_stmt.default_body);
    }
    
    // Patch all end jumps to here
    for (int i = 0; i < end_jump_count; i++) {
        patch_jump(end_jumps[i]);
    }
    
    free(end_jumps);
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

static void compile_avatar_stmt(ASTNode* node) {
    // Avatar statement: launches a function in a green thread
    // Two forms:
    // 1. avatar functionName(args) - call existing function
    // 2. avatar func() { ... } - anonymous function
    
    if (node->as.avatar_stmt.is_anonymous) {
        // Compile the anonymous function
        compile_expression(node->as.avatar_stmt.call_expr);
        // The function is now on the stack
        // Push 0 args (the function itself has no call-time args)
        emit_byte(OP_AVATAR);
        emit_byte(0); // arg count
    } else {
        // It's a call expression - compile it to get function + args on stack
        ASTNode* call_expr = node->as.avatar_stmt.call_expr;
        
        // Push the function
        compile_expression(call_expr->as.call.function);
        
        // Push all arguments
        for (int i = 0; i < call_expr->as.call.arg_count; i++) {
            compile_expression(call_expr->as.call.args[i]);
        }
        
        // Emit avatar launch with arg count
        emit_byte(OP_AVATAR);
        emit_byte(call_expr->as.call.arg_count);
    }
}

static void compile_await_expr(ASTNode* node) {
    // Await expression: waits for avatar to complete
    // If avatar_handle is NULL, waits for all avatars
    
    if (node->as.await_expr.avatar_handle != NULL) {
        // Wait for specific avatar
        compile_expression(node->as.await_expr.avatar_handle);
        emit_byte(OP_AWAIT);
    } else {
        // Wait for all avatars - push nil as marker
        emit_byte(OP_NIL);
        emit_byte(OP_AWAIT);
    }
}

// Top-level statement dispatcher
static void compile_statement(ASTNode* node) {
    if (node == NULL) return;
    current_line = node->line;
    switch (node->type) {
        case AST_ERROR:
            // Parse error - compiler should bail out
            // Error already reported by parser
            break;
        case AST_BLOCK:
            compile_block(node);
            break;
        case AST_VAR_DECL:
            compile_var_decl(node);
            break;
        case AST_FUNCTION_DECL:
            compile_function_decl(node);
            break;
        case AST_STRUCT_DECL:
            compile_struct_decl(node);
            break;
        case AST_INTERFACE_DECL:
            compile_interface_decl(node);
            break;
        case AST_METHOD_DECL:
            compile_method_decl(node);
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
        case AST_SWITCH_STMT:
            compile_switch_stmt(node);
            break;
        case AST_RETURN_STMT:
            compile_return_stmt(node);
            break;
        case AST_AVATAR_STMT:
            compile_avatar_stmt(node);
            break;
        case AST_EXPRESSION_STMT:
            compile_expression(node->as.expression);
            emit_byte(OP_POP); // discard expression result in statement context
            break;
        default:
            // Fallback: treat as expression statement
            compile_expression(node);
            emit_byte(OP_POP);
            break;
    }
}

static void compile_block(ASTNode* node) {
    // At script level (scope_depth == 0), blocks don't create new scopes
    // This ensures while loops and blocks at script level work correctly
    // Inside functions (scope_depth > 0), blocks create proper local scopes
    bool is_script_level = (current->scope_depth == 0);
    
    if (!is_script_level) {
        begin_scope();
    }
    
    for (int i = 0; i < node->as.block.count; i++) {
        compile_statement(node->as.block.statements[i]);
    }
    
    if (!is_script_level) {
        end_scope();
    }
}


void compiler_init(Compiler* compiler, const char* name) {
    compiler->had_error = false;
    compiler->local_count = 0;
    compiler->max_local_count = 0;  // Track peak local count
    compiler->scope_depth = 0;
    compiler->enclosing = (struct Compiler*)current;
    
    compiler->function = malloc(sizeof(Function));
    compiler->function->name = strdup(name);
    compiler->function->arity = 0;
    compiler->function->local_count = 0;  // Will be set when function compilation completes
    chunk_init(&compiler->function->chunk);
    compiler->function->is_native = false;
    // Propagate source file path into every function object at creation time
    // so nested functions also carry their origin for error reporting.
    compiler->function->source_path = get_current_source_path();
    
    compiler->chunk = &compiler->function->chunk;
    
    current = compiler;
}

Function* compiler_compile(ASTNode* ast) {
    Compiler compiler;
    compiler_init(&compiler, "script");
    
    // Compile the module body WITHOUT creating a new scope
    // Module-level variables should be globals (scope_depth = 0)
    // If ast is a BLOCK, compile its statements directly without begin_scope/end_scope
    if (ast && ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->as.block.count; i++) {
            compile_statement(ast->as.block.statements[i]);
        }
    } else {
        compile_statement(ast);
    }
    
    emit_byte(OP_HALT);
    
    // Store local count for runtime stack management
    compiler.function->local_count = compiler.max_local_count;
    
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

static int long_constant_instruction(const char* name, Chunk* chunk, int offset) {
    uint16_t constant = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    printf("%-16s %4d '", name, constant);
    
    if (constant >= chunk->constant_count) {
        printf("<OUT OF BOUNDS>");
    } else {
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
    }
    
    printf("'\n");
    return offset + 3;
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
        case OP_CONSTANT_LONG: {
            uint16_t constant = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s %4d\n", "OP_CONSTANT_LONG", constant);
            return offset + 3;
        }
        case OP_NIL:
            return simple_instruction("OP_NIL", offset);
        case OP_TRUE:
            return simple_instruction("OP_TRUE", offset);
        case OP_FALSE:
            return simple_instruction("OP_FALSE", offset);
        case OP_POP:
            return simple_instruction("OP_POP", offset);
        case OP_DUP:
            return simple_instruction("OP_DUP", offset);
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
        case OP_DEFINE_GLOBAL_LONG:
            return long_constant_instruction("OP_DEFINE_GLOBAL_LONG", chunk, offset);
        case OP_GET_GLOBAL:
            return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL_LONG:
            return long_constant_instruction("OP_GET_GLOBAL_LONG", chunk, offset);
        case OP_SET_GLOBAL:
            return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL_LONG:
            return long_constant_instruction("OP_SET_GLOBAL_LONG", chunk, offset);
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
        case OP_AVATAR:
            return byte_instruction("OP_AVATAR", chunk, offset);
        case OP_AWAIT:
            return simple_instruction("OP_AWAIT", offset);
        case OP_RETURN:
            return simple_instruction("OP_RETURN", offset);
        case OP_OBJECT_NEW:
            return simple_instruction("OP_OBJECT_NEW", offset);
        case OP_OBJECT_GET:
            return simple_instruction("OP_OBJECT_GET", offset);
        case OP_OBJECT_SET:
            return simple_instruction("OP_OBJECT_SET", offset);
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
    function->source_path = NULL;
    return function;
}

void function_free(Function* function) {
    if (function == NULL) return;
    
    free(function->name);
    chunk_free(&function->chunk);
    free(function);
}