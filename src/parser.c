#include "ast.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Helper functions
static int __attribute__((unused)) g_destruct_temp_counter = 0; // for generating unique temp names
static Token* current_token(Parser* parser) {
    if (parser->current >= parser->count) {
        // Return EOF token if we've reached the end
        static Token eof = {TOKEN_EOF, "", 0, 0, 0};
        return &eof;
    }
    return &parser->tokens[parser->current];
}

static Token* previous_token(Parser* parser) {
    if (!parser || !parser->tokens || parser->current == 0) {
        static Token error_tok = {TOKEN_ERROR, "No previous token", 17, 0, 0};
        return &error_tok;
    }
    return &parser->tokens[parser->current - 1];
}

static bool parser_is_at_end(Parser* parser) {
    return current_token(parser)->type == TOKEN_EOF;
}

static Token* parser_advance(Parser* parser) {
    if (!parser_is_at_end(parser)) parser->current++;
    return previous_token(parser);
}

static bool check(Parser* parser, KuyilTokenType type) {
    if (parser_is_at_end(parser)) return false;
    return current_token(parser)->type == type;
}

static bool parser_match(Parser* parser, KuyilTokenType type) {
    if (check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

static void error_at(Parser* parser, Token* token, const char* message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    
    char error_msg[512];
    int pos = snprintf(error_msg, sizeof(error_msg), "[line %d] Error", token->line);
    
    if (token->type == TOKEN_EOF) {
        pos += snprintf(error_msg + pos, sizeof(error_msg) - pos, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        pos += snprintf(error_msg + pos, sizeof(error_msg) - pos, " at '%.*s'", token->length, token->start);
    }
    
    snprintf(error_msg + pos, sizeof(error_msg) - pos, ": %s", message);
    LOG_ERROR("%s", error_msg);
    parser->had_error = true;
}

static void error(Parser* parser, const char* message) {
    error_at(parser, previous_token(parser), message);
}

static void error_at_current(Parser* parser, const char* message) {
    error_at(parser, current_token(parser), message);
}

// Set AST node source location from a token
static void set_node_location(ASTNode* node, Token* token) {
    if (!node || !token) return;
    node->line = token->line;
    node->column = token->column;
}

// Skip any newline tokens (for multiline arrays, function calls, etc.)
static void skip_newlines(Parser* parser) {
    while (check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
    }
}

static Token* consume(Parser* parser, KuyilTokenType type, const char* message) {
    if (current_token(parser)->type == type) {
        return parser_advance(parser);
    }

    error_at_current(parser, message);
    return current_token(parser);
}static void synchronize(Parser* parser) {
    parser->panic_mode = false;
    
    while (current_token(parser)->type != TOKEN_EOF) {
        if (previous_token(parser)->type == TOKEN_SEMICOLON) return;
        if (previous_token(parser)->type == TOKEN_NEWLINE) return;
        
        switch (current_token(parser)->type) {
            case TOKEN_FN:
            case TOKEN_LET:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;
            default:
                ; // Do nothing.
        }
        
        parser_advance(parser);
    }
}

// Memory management
ASTNode* ast_node_new(ASTNodeType type) {
    ASTNode* node = malloc(sizeof(ASTNode));
    if (node == NULL) {
        LOG_ERROR("Could not allocate memory for AST node");
        exit(1);
    }
    node->type = type;
    node->line = 0;
    node->column = 0;
    return node;
}

// Decorator management
void decorator_list_init(DecoratorList* list) {
    list->decorators = NULL;
    list->count = 0;
    list->capacity = 0;
}

void decorator_list_add(DecoratorList* list, const char* name, ASTNode** args, int arg_count) {
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->decorators = realloc(list->decorators, sizeof(Decorator) * new_capacity);
        list->capacity = new_capacity;
    }
    
    Decorator* dec = &list->decorators[list->count++];
    dec->name = strdup(name);
    dec->arg_count = arg_count;
    if (arg_count > 0) {
        dec->args = malloc(sizeof(ASTNode*) * arg_count);
        for (int i = 0; i < arg_count; i++) {
            dec->args[i] = args[i];
        }
    } else {
        dec->args = NULL;
    }
}

void decorator_list_free(DecoratorList* list) {
    for (int i = 0; i < list->count; i++) {
        free(list->decorators[i].name);
        if (list->decorators[i].args) {
            for (int j = 0; j < list->decorators[i].arg_count; j++) {
                ast_node_free(list->decorators[i].args[j]);
            }
            free(list->decorators[i].args);
        }
    }
    free(list->decorators);
    list->decorators = NULL;
    list->count = 0;
    list->capacity = 0;
}

Decorator* decorator_list_find(DecoratorList* list, const char* name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->decorators[i].name, name) == 0) {
            return &list->decorators[i];
        }
    }
    return NULL;
}

// Create an error node to prevent crashes after parse errors
static ASTNode* error_node(Parser* parser) {
    ASTNode* node = ast_node_new(AST_ERROR);
    set_node_location(node, current_token(parser));
    return node;
}

void ast_node_free(ASTNode* node) {
    if (node == NULL) return;
    
    switch (node->type) {
        case AST_ERROR:
            // Error nodes have no allocated data
            break;
        case AST_IDENTIFIER:
            free(node->as.identifier);
            break;
        case AST_LITERAL:
            if (node->as.literal.type == VALUE_STRING) {
                free(node->as.literal.as.string);
            }
            break;
        case AST_VAR_DECL:
            free(node->as.var_decl.name);
            ast_node_free(node->as.var_decl.value);
            break;
        case AST_FUNCTION_DECL:
            free(node->as.function_decl.name);
            for (int i = 0; i < node->as.function_decl.param_count; i++) {
                free(node->as.function_decl.params[i]);
            }
            free(node->as.function_decl.params);
            ast_node_free((ASTNode*)node->as.function_decl.body);
            break;
        case AST_BLOCK:
            for (int i = 0; i < node->as.block.count; i++) {
                ast_node_free(node->as.block.statements[i]);
            }
            free(node->as.block.statements);
            break;
        case AST_IF_STMT:
            ast_node_free(node->as.if_stmt.condition);
            ast_node_free(node->as.if_stmt.then_branch);
            ast_node_free(node->as.if_stmt.else_branch);
            break;
        case AST_WHILE_STMT:
            ast_node_free(node->as.while_stmt.condition);
            ast_node_free(node->as.while_stmt.body);
            break;
        case AST_FOR_STMT:
            ast_node_free(node->as.for_stmt.init);
            ast_node_free(node->as.for_stmt.condition);
            ast_node_free(node->as.for_stmt.update);
            ast_node_free(node->as.for_stmt.body);
            break;
        case AST_RETURN_STMT:
            ast_node_free(node->as.return_stmt.value);
            break;
        case AST_AVATAR_STMT:
            ast_node_free(node->as.avatar_stmt.call_expr);
            break;
        case AST_DEFER_STMT:
            ast_node_free(node->as.defer_stmt.call_expr);
            break;
        case AST_AWAIT_EXPR:
            ast_node_free(node->as.await_expr.avatar_handle);
            break;
        case AST_BINARY_OP:
            ast_node_free(node->as.binary.left);
            ast_node_free(node->as.binary.right);
            break;
        case AST_UNARY_OP:
            ast_node_free(node->as.unary.operand);
            break;
        case AST_CALL:
            ast_node_free(node->as.call.function);
            for (int i = 0; i < node->as.call.arg_count; i++) {
                ast_node_free(node->as.call.args[i]);
            }
            free(node->as.call.args);
            break;
        case AST_ASSIGNMENT:
            ast_node_free(node->as.assignment.target);
            ast_node_free(node->as.assignment.value);
            break;
        case AST_EXPRESSION_STMT:
            ast_node_free(node->as.expression);
            break;
        case AST_ANONYMOUS_FUNCTION:
            for (int i = 0; i < node->as.anonymous_function.param_count; i++) {
                free(node->as.anonymous_function.params[i]);
            }
            free(node->as.anonymous_function.params);
            ast_node_free(node->as.anonymous_function.body);
            break;
        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->as.array_literal.count; i++) {
                ast_node_free(node->as.array_literal.elements[i]);
            }
            free(node->as.array_literal.elements);
            break;
        case AST_ARRAY_ACCESS:
            ast_node_free(node->as.array_access.array);
            ast_node_free(node->as.array_access.index);
            break;
        // Add other cases as needed
        default:
            break;
    }
    
    free(node);
}

// Forward declarations for parsing functions
static ASTNode* expression(Parser* parser);
static ASTNode* statement(Parser* parser);
static ASTNode* declaration(Parser* parser);
static ASTNode* parse_anonymous_function(Parser* parser, bool is_arrow);
static ASTNode* struct_declaration(Parser* parser);
static ASTNode* interface_declaration(Parser* parser);
static ASTNode* interface_declaration_impl(Parser* parser, bool is_exported);
static ASTNode* method_declaration(Parser* parser);
static DecoratorList parse_decorators(Parser* parser);
static ASTNode* switch_statement(Parser* parser);
static ASTNode* avatar_statement(Parser* parser);
static ASTNode* block_statement(Parser* parser);
static ASTNode* call(Parser* parser);
static ASTNode* parse_array_literal(Parser* parser);
static ASTNode* parse_map_literal(Parser* parser);
static ASTNode* directive_statement(Parser* parser);

// Parse map literal { "key": value, key2: value2 }
static ASTNode* parse_map_literal(Parser* parser) {
    ASTNode* node = ast_node_new(AST_OBJECT_LITERAL);
    // The '{' token was just consumed by the caller
    set_node_location(node, previous_token(parser));
    node->as.object_literal.count = 0;
    node->as.object_literal.capacity = 8;
    node->as.object_literal.properties = malloc(sizeof(ObjectProperty) * node->as.object_literal.capacity);
    
    if (!node->as.object_literal.properties) {
        LOG_ERROR("Out of memory allocating map properties");
        node->as.object_literal.capacity = 0;
        return node;
    }
    
    skip_newlines(parser);  // Skip newlines after '{'
    
    // Empty map {}
    if (check(parser, TOKEN_RIGHT_BRACE)) {
        parser_advance(parser);
        return node;
    }
    
    // Parse map entries
    do {
        skip_newlines(parser);  // Skip newlines after comma or after '{'
        
        // Check for trailing comma before '}'
        if (check(parser, TOKEN_RIGHT_BRACE)) {
            break;
        }
        
        if (node->as.object_literal.count >= node->as.object_literal.capacity) {
            node->as.object_literal.capacity *= 2;
            ObjectProperty* new_props = realloc(node->as.object_literal.properties, 
                                               sizeof(ObjectProperty) * node->as.object_literal.capacity);
            if (!new_props) {
                LOG_ERROR("Out of memory reallocating map properties");
                break;
            }
            node->as.object_literal.properties = new_props;
        }
        
        // Parse key (can be string literal or identifier)
        char* key = NULL;
        if (check(parser, TOKEN_STRING)) {
            Token* key_token = consume(parser, TOKEN_STRING, "Expect key.");
            int str_len = key_token->length - 2; // Remove quotes
            if (str_len < 0) str_len = 0;
            key = malloc(str_len + 1);
            memcpy(key, key_token->start + 1, str_len);
            key[str_len] = '\0';
        } else if (check(parser, TOKEN_IDENTIFIER)) {
            Token* key_token = consume(parser, TOKEN_IDENTIFIER, "Expect key.");
            key = malloc(key_token->length + 1);
            memcpy(key, key_token->start, key_token->length);
            key[key_token->length] = '\0';
        } else {
            error_at_current(parser, "Expect map key (string or identifier).");
            break;
        }
        
        skip_newlines(parser);  // Skip newlines after key
        consume(parser, TOKEN_COLON, "Expect ':' after map key.");
        skip_newlines(parser);  // Skip newlines after ':'
        
        ASTNode* value = expression(parser);
        
        node->as.object_literal.properties[node->as.object_literal.count].key = key;
        node->as.object_literal.properties[node->as.object_literal.count].value = value;
        node->as.object_literal.count++;
        
        skip_newlines(parser);  // Skip newlines after value
        
    } while (parser_match(parser, TOKEN_COMMA));
    
    skip_newlines(parser);  // Skip newlines before '}'
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after map entries.");
    
    return node;
}

// Parse array literal [1, 2, 3]
static ASTNode* parse_array_literal(Parser* parser) {
    ASTNode* node = ast_node_new(AST_ARRAY_LITERAL);
    // The '[' token was just consumed by the caller
    set_node_location(node, previous_token(parser));
    node->as.array_literal.count = 0;
    node->as.array_literal.capacity = 8;
    node->as.array_literal.elements = malloc(sizeof(ASTNode*) * node->as.array_literal.capacity);
    
    if (!node->as.array_literal.elements) {
        LOG_ERROR("Out of memory allocating array elements");
        // Return empty array node rather than crashing
        node->as.array_literal.capacity = 0;
        return node;
    }
    
    skip_newlines(parser);  // Skip newlines after '['
    
    // Empty array []
    if (check(parser, TOKEN_RIGHT_BRACKET)) {
        parser_advance(parser);
        return node;
    }
    
    // Parse array elements
    do {
        skip_newlines(parser);  // Skip newlines after comma or after '['
        
        // Check for trailing comma before ']'
        if (check(parser, TOKEN_RIGHT_BRACKET)) {
            break;
        }
        
        if (node->as.array_literal.count >= node->as.array_literal.capacity) {
            node->as.array_literal.capacity *= 2;
            ASTNode** new_elements = realloc(node->as.array_literal.elements, 
                                             sizeof(ASTNode*) * node->as.array_literal.capacity);
            if (!new_elements) {
                LOG_ERROR("Out of memory reallocating array elements");
                break; // Stop adding elements but return what we have
            }
            node->as.array_literal.elements = new_elements;
        }
        
        node->as.array_literal.elements[node->as.array_literal.count++] = expression(parser);
        
        skip_newlines(parser);  // Skip newlines after element
        
    } while (parser_match(parser, TOKEN_COMMA));
    
    skip_newlines(parser);  // Skip newlines before ']'
    consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    return node;
}


// Parse interpolated string with ${} syntax
static ASTNode* parse_interpolated_string(Parser* parser) {
    Token* token = previous_token(parser);
    ASTNode* node = ast_node_new(AST_INTERPOLATED_STRING);
    set_node_location(node, token);
    
    // Parse the content between backticks looking for ${}
    const char* content = token->start + 1; // Skip opening backtick
    int content_len = token->length - 2;    // Remove both backticks
    
    // Count interpolations and string parts
    int interpolation_count = 0;
    int string_part_count = 0;
    
    for (int i = 0; i < content_len - 1; i++) {
        if (content[i] == '$' && content[i + 1] == '{') {
            interpolation_count++;
            string_part_count++;
        }
    }
    string_part_count++; // One more string part than interpolations
    
    // Allocate arrays
    node->as.interpolated_string.part_count = string_part_count;
    node->as.interpolated_string.string_parts = malloc(sizeof(char*) * string_part_count);
    node->as.interpolated_string.expressions = malloc(sizeof(ASTNode*) * interpolation_count);
    
    if (!node->as.interpolated_string.string_parts || 
        (interpolation_count > 0 && !node->as.interpolated_string.expressions)) {
        LOG_ERROR("Out of memory allocating interpolated string");
        // Clean up and return empty string node
        if (node->as.interpolated_string.string_parts) {
            free(node->as.interpolated_string.string_parts);
        }
        if (node->as.interpolated_string.expressions) {
            free(node->as.interpolated_string.expressions);
        }
        node->type = AST_LITERAL;
        node->as.literal.type = VALUE_STRING;
        node->as.literal.as.string = strdup("");
        return node;
    }
    
    // Parse string parts and expressions
    int part_index = 0;
    int expr_index = 0;
    int start = 0;
    
    for (int i = 0; i <= content_len; i++) {
        if ((i < content_len - 1 && content[i] == '$' && content[i + 1] == '{') || i == content_len) {
            // Extract string part
            int part_len = i - start;
            if (part_len < 0) part_len = 0; // Safety check
            char* part = malloc(part_len + 1);
            if (!part) {
                LOG_ERROR("Out of memory in interpolated string part");
                part = strdup(""); // Fallback to empty string
            } else {
                memcpy(part, content + start, part_len);
                part[part_len] = '\0';
            }
            if (part_index < string_part_count) {
                node->as.interpolated_string.string_parts[part_index++] = part;
            }
            
            if (i < content_len - 1) {
                // Find the matching }
                i += 2; // Skip ${
                int brace_start = i;
                int brace_count = 1;
                
                while (i < content_len && brace_count > 0) {
                    if (content[i] == '{') brace_count++;
                    else if (content[i] == '}') brace_count--;
                    i++;
                }
                
                // Extract expression content
                int expr_len = i - brace_start - 1;
                if (expr_len < 0) expr_len = 0; // Safety check
                char* expr_str = malloc(expr_len + 1);
                if (!expr_str) {
                    LOG_ERROR("Out of memory in interpolated string expression");
                    expr_str = strdup(""); // Fallback
                } else {
                    memcpy(expr_str, content + brace_start, expr_len);
                    expr_str[expr_len] = '\0';
                }
                
                // Create a simple identifier node for now (could be enhanced to parse full expressions)
                ASTNode* expr_node = ast_node_new(AST_IDENTIFIER);
                expr_node->as.identifier = expr_str;
                if (expr_index < interpolation_count) {
                    node->as.interpolated_string.expressions[expr_index++] = expr_node;
                }
                
                start = i;
                i--; // Adjust for the for loop increment
            }
        }
    }
    
    return node;
}

// Expression parsing (recursive descent)
static ASTNode* primary(Parser* parser) {
    if (parser_match(parser, TOKEN_TRUE)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        set_node_location(node, previous_token(parser));
        node->as.literal.type = VALUE_BOOL;
        node->as.literal.as.boolean = true;
        return node;
    }
    
    if (parser_match(parser, TOKEN_FALSE)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        set_node_location(node, previous_token(parser));
        node->as.literal.type = VALUE_BOOL;
        node->as.literal.as.boolean = false;
        return node;
    }
    
    if (parser_match(parser, TOKEN_NIL)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        set_node_location(node, previous_token(parser));
        node->as.literal.type = VALUE_NIL;
        return node;
    }
    
    if (parser_match(parser, TOKEN_NUMBER)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        node->as.literal.type = VALUE_NUMBER;
        Token* token = previous_token(parser);
        set_node_location(node, token);
        
        // Bounds check token length
        int safe_length = token->length;
        if (safe_length < 0 || safe_length > 1000) {
            safe_length = 1;
        }
        
        char* number_str = malloc(safe_length + 1);
        if (!number_str) {
            LOG_ERROR("Out of memory parsing number");
            node->as.literal.as.number = 0.0;
        } else {
            memcpy(number_str, token->start, safe_length);
            number_str[safe_length] = '\0';
            node->as.literal.as.number = strtod(number_str, NULL);
            free(number_str);
        }
        return node;
    }
    
    if (parser_match(parser, TOKEN_STRING)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        node->as.literal.type = VALUE_STRING;
        Token* token = previous_token(parser);
        set_node_location(node, token);
        // Remove quotes and copy string
        int str_len = token->length - 2; // Remove quotes
        
        // Ensure we have a valid string length
        if (str_len < 0) str_len = 0;
        
        // Allocate buffer for processed string (may be smaller after escape processing)
        char* str = malloc(str_len + 1);
        int write_pos = 0;
        
        // Process escape sequences
        for (int i = 1; i < token->length - 1; i++) {  // Skip opening and closing quotes
            if (token->start[i] == '\\' && i + 1 < token->length - 1) {
                // Escape sequence
                i++;  // Move to next character
                switch (token->start[i]) {
                    case 'n':  str[write_pos++] = '\n'; break;
                    case 't':  str[write_pos++] = '\t'; break;
                    case 'r':  str[write_pos++] = '\r'; break;
                    case '\\': str[write_pos++] = '\\'; break;
                    case '"':  str[write_pos++] = '"'; break;
                    case '\'': str[write_pos++] = '\''; break;
                    case '`':  str[write_pos++] = '`'; break;
                    case '0':  str[write_pos++] = '\0'; break;
                    case 'b':  str[write_pos++] = '\b'; break;
                    case 'f':  str[write_pos++] = '\f'; break;
                    case 'v':  str[write_pos++] = '\v'; break;
                    default:
                        // Unknown escape, keep the backslash and character
                        str[write_pos++] = '\\';
                        str[write_pos++] = token->start[i];
                        break;
                }
            } else {
                str[write_pos++] = token->start[i];
            }
        }
        str[write_pos] = '\0';
        
        node->as.literal.as.string = str;
        return node;
    }
    
    if (parser_match(parser, TOKEN_BACKTICK_STRING)) {
        ASTNode* node = ast_node_new(AST_LITERAL);
        node->as.literal.type = VALUE_STRING;
        Token* token = previous_token(parser);
        set_node_location(node, token);
        // Remove backticks and copy string (no escape processing needed)
        int str_len = token->length - 2; // Remove backticks
        char* str = malloc(str_len + 1);
        memcpy(str, token->start + 1, str_len);
        str[str_len] = '\0';
        node->as.literal.as.string = str;
        return node;
    }
    
    if (parser_match(parser, TOKEN_INTERPOLATED_STRING)) {
        return parse_interpolated_string(parser);
    }
    
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        ASTNode* node = ast_node_new(AST_IDENTIFIER);
        Token* token = previous_token(parser);
        set_node_location(node, token);
        char* name = malloc(token->length + 1);
        memcpy(name, token->start, token->length);
        name[token->length] = '\0';
        node->as.identifier = name;
        return node;
    }
    
    if (parser_match(parser, TOKEN_LEFT_PAREN)) {
        // Parse as grouped expression
        ASTNode* expr = expression(parser);
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }
    
    // Handle @get() and other decorator introspection functions
    if (parser_match(parser, TOKEN_AT)) {
        Token* at_token = previous_token(parser);
        Token* name_token = consume(parser, TOKEN_IDENTIFIER, "Expect identifier after '@' in expression.");
        
        // Create identifier for the decorator function (e.g., "@get" becomes a call to "@get")
        char* func_name = malloc(name_token->length + 2); // +1 for @ and +1 for null terminator
        func_name[0] = '@';
        memcpy(func_name + 1, name_token->start, name_token->length);
        func_name[name_token->length + 1] = '\0';
        
        ASTNode* func_node = ast_node_new(AST_IDENTIFIER);
        set_node_location(func_node, at_token);
        func_node->as.identifier = func_name;
        
        return func_node;
    }
    
    if (parser_match(parser, TOKEN_FN)) {
        return parse_anonymous_function(parser, false);
    }
    
    if (parser_match(parser, TOKEN_AVATAR)) {
        // Parse avatar expression: avatar functionName(args) or avatar func() {...}
        ASTNode* avatar_node = ast_node_new(AST_AVATAR_STMT);
        set_node_location(avatar_node, previous_token(parser));
        
        // Check if it's an anonymous function: avatar func() { ... } or avatar fn() { ... }
        if (parser_match(parser, TOKEN_FN) || check(parser, TOKEN_LEFT_PAREN)) {
            // Parse anonymous function directly
            // Back up if we consumed TOKEN_FN
            if (previous_token(parser)->type == TOKEN_FN) {
                // Already consumed TOKEN_FN, continue
            }
            
            // Parse parameters
            consume(parser, TOKEN_LEFT_PAREN, "Expect '(' for avatar function.");
            
            ASTNode* anon_func = ast_node_new(AST_ANONYMOUS_FUNCTION);
            set_node_location(anon_func, previous_token(parser));
            anon_func->as.anonymous_function.params = NULL;
            anon_func->as.anonymous_function.param_count = 0;
            anon_func->as.anonymous_function.is_arrow = false;
            
            // Parse parameters
            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                int capacity = 4;
                anon_func->as.anonymous_function.params = malloc(sizeof(char*) * capacity);
                
                do {
                    if (anon_func->as.anonymous_function.param_count >= capacity) {
                        capacity *= 2;
                        anon_func->as.anonymous_function.params = realloc(anon_func->as.anonymous_function.params, sizeof(char*) * capacity);
                    }
                    
                    Token* param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
                    char* param_name = malloc(param->length + 1);
                    memcpy(param_name, param->start, param->length);
                    param_name[param->length] = '\0';
                    anon_func->as.anonymous_function.params[anon_func->as.anonymous_function.param_count++] = param_name;
                } while (parser_match(parser, TOKEN_COMMA));
            }
            
            consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
            
            // Parse body
            consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before avatar function body.");
            anon_func->as.anonymous_function.body = block_statement(parser);
            
            avatar_node->as.avatar_stmt.call_expr = anon_func;
            avatar_node->as.avatar_stmt.is_anonymous = true;
        } else {
            // Parse named function call: avatar functionName(args)
            ASTNode* call_expr = call(parser); // This will parse function and args
            
            // Validate it's actually a call expression
            if (call_expr->type != AST_CALL) {
                error_at(parser, previous_token(parser), "Expected function call after 'avatar'.");
            }
            
            avatar_node->as.avatar_stmt.call_expr = call_expr;
            avatar_node->as.avatar_stmt.is_anonymous = false;
        }
        
        return avatar_node;
    }
    
    if (parser_match(parser, TOKEN_AWAIT)) {
        // Parse await expression: await avatar_handle or await (for all)
        ASTNode* await_node = ast_node_new(AST_AWAIT_EXPR);
        set_node_location(await_node, previous_token(parser));
        
        // Check if there's an expression after await
        if (check(parser, TOKEN_SEMICOLON) || check(parser, TOKEN_NEWLINE) || check(parser, TOKEN_RIGHT_PAREN)) {
            // await without argument - wait for all avatars
            await_node->as.await_expr.avatar_handle = NULL;
        } else {
            // await with expression - wait for specific avatar
            await_node->as.await_expr.avatar_handle = primary(parser);
        }
        
        return await_node;
    }
    
    if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
        return parse_array_literal(parser);
    }
    
    if (parser_match(parser, TOKEN_LEFT_BRACE)) {
        return parse_map_literal(parser);
    }

    error_at_current(parser, "Expect expression.");
    return error_node(parser);
}static ASTNode* call(Parser* parser) {
    ASTNode* expr = primary(parser);
    
    while (true) {
        if (parser_match(parser, TOKEN_LEFT_PAREN)) {
            ASTNode* call_node = ast_node_new(AST_CALL);
            // Location at '('
            set_node_location(call_node, previous_token(parser));
            call_node->as.call.function = expr;
            call_node->as.call.args = NULL;
            call_node->as.call.arg_count = 0;
            
            skip_newlines(parser);  // Skip newlines after '('
            
            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                int capacity = 4;
                call_node->as.call.args = malloc(sizeof(ASTNode*) * capacity);
                
                do {
                    skip_newlines(parser);  // Skip newlines after comma or after '('
                    
                    // Check for trailing comma before ')'
                    if (check(parser, TOKEN_RIGHT_PAREN)) {
                        break;
                    }
                    
                    if (call_node->as.call.arg_count >= capacity) {
                        capacity *= 2;
                        call_node->as.call.args = realloc(call_node->as.call.args, sizeof(ASTNode*) * capacity);
                    }
                    call_node->as.call.args[call_node->as.call.arg_count++] = expression(parser);
                    
                    skip_newlines(parser);  // Skip newlines after argument
                    
                } while (parser_match(parser, TOKEN_COMMA));
            }
            
            skip_newlines(parser);  // Skip newlines before ')'
            consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
            expr = call_node;
        } else if (!parser->suppress_struct_literal && expr->type == AST_IDENTIFIER && check(parser, TOKEN_LEFT_BRACE)) {
            // Peek ahead to see if this is a struct literal or should be left as identifier
            // Struct literal: StructName { field: value, ... }
            // If next token after { is } or string, it's likely a map literal being passed as argument
            int saved_pos = parser->current;
            parser_advance(parser); // consume TOKEN_LEFT_BRACE
            skip_newlines(parser);
            
            bool is_struct_literal = false;
            if (check(parser, TOKEN_RIGHT_BRACE)) {
                // Empty braces {} - this is a map literal
                is_struct_literal = false;
            } else if (check(parser, TOKEN_STRING)) {
                // Starts with string - this is a map literal like { "key": value }
                is_struct_literal = false;
            } else if (check(parser, TOKEN_IDENTIFIER)) {
                // Look for pattern: identifier followed by colon (struct field: StructName { field: value })
                int check_pos = parser->current + 1;
                if (check_pos < parser->count && parser->tokens[check_pos].type == TOKEN_COLON) {
                    is_struct_literal = true;
                } else {
                    // Identifier not followed by colon - not a struct literal
                    is_struct_literal = false;
                }
            }
            
            // Restore position
            parser->current = saved_pos;
            
            if (!is_struct_literal) {
                // Not a struct literal, return the identifier expression as-is
                break;
            }
            
            // Parse as struct literal
            parser_advance(parser); // consume TOKEN_LEFT_BRACE again
            ASTNode* struct_lit = ast_node_new(AST_STRUCT_LITERAL);
            set_node_location(struct_lit, previous_token(parser));
            struct_lit->as.struct_literal.name = expr->as.identifier;
            struct_lit->as.struct_literal.field_count = 0;
            struct_lit->as.struct_literal.field_values = NULL;
            
            // Free the identifier node since we've captured its name
            free(expr);
            
            if (!check(parser, TOKEN_RIGHT_BRACE)) {
                int capacity = 4;
                struct_lit->as.struct_literal.field_values = malloc(sizeof(ASTNode*) * capacity * 2); // pairs of key:value
                
                while (true) {
                    // Skip any newlines before a field
                    while (parser_match(parser, TOKEN_NEWLINE));
                    
                    // Allow trailing comma and break before '}'
                    if (check(parser, TOKEN_RIGHT_BRACE)) break;
                    
                    // Parse field name (identifier)
                    Token* field_name = consume(parser, TOKEN_IDENTIFIER, "Expect field name.");
                    
                    // Consume ':' possibly with newlines around
                    while (parser_match(parser, TOKEN_NEWLINE));
                    consume(parser, TOKEN_COLON, "Expect ':' after field name.");
                    while (parser_match(parser, TOKEN_NEWLINE));
                    
                    // Parse field value
                    ASTNode* value = expression(parser);
                    
                    // Store as pair: [field_name_as_string, value_expression]
                    if (struct_lit->as.struct_literal.field_count >= capacity) {
                        capacity *= 2;
                        struct_lit->as.struct_literal.field_values = realloc(
                            struct_lit->as.struct_literal.field_values,
                            sizeof(ASTNode*) * capacity * 2
                        );
                    }
                    
                    // Store field name as a string literal node
                    ASTNode* field_key = ast_node_new(AST_LITERAL);
                    set_node_location(field_key, field_name);
                    field_key->as.literal.type = VALUE_STRING;
                    char* key_str = malloc(field_name->length + 1);
                    memcpy(key_str, field_name->start, field_name->length);
                    key_str[field_name->length] = '\0';
                    field_key->as.literal.as.string = key_str;
                    
                    struct_lit->as.struct_literal.field_values[struct_lit->as.struct_literal.field_count * 2] = field_key;
                    struct_lit->as.struct_literal.field_values[struct_lit->as.struct_literal.field_count * 2 + 1] = value;
                    struct_lit->as.struct_literal.field_count++;
                    
                    // After a field, consume optional whitespace and a comma if present
                    while (parser_match(parser, TOKEN_NEWLINE));
                    if (parser_match(parser, TOKEN_COMMA)) {
                        // Allow trailing comma: skip newlines and if next is '}', break
                        while (parser_match(parser, TOKEN_NEWLINE));
                        if (check(parser, TOKEN_RIGHT_BRACE)) break;
                        // Otherwise continue to next field
                        continue;
                    }
                    
                    // No comma, end of fields
                    break;
                }
            }
            
            consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after struct fields.");
            expr = struct_lit;
        } else if (parser_match(parser, TOKEN_DOT)) {
            // Create member access node
            Token* name_token = consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'.");
            char* name = malloc(name_token->length + 1);
            memcpy(name, name_token->start, name_token->length);
            name[name_token->length] = '\0';
            
            ASTNode* member_node = ast_node_new(AST_MEMBER_ACCESS);
            set_node_location(member_node, previous_token(parser));
            member_node->as.member.object = expr;
            member_node->as.member.property = name;
            expr = member_node;
        } else if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
            ASTNode* array_access_node = ast_node_new(AST_ARRAY_ACCESS);
            set_node_location(array_access_node, previous_token(parser));
            array_access_node->as.array_access.array = expr;
            array_access_node->as.array_access.index = expression(parser);
            consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after array index.");
            expr = array_access_node;
        } else if (parser_match(parser, TOKEN_INCREMENT) || parser_match(parser, TOKEN_DECREMENT)) {
            // Handle postfix ++ and -- operators
            Token* operator = previous_token(parser);
            
            // Only support ++ and -- on simple identifiers for now
            if (expr->type != AST_IDENTIFIER) {
                error_at(parser, operator, "++ and -- operators only work on identifiers.");
                break;
            }
            
            // Convert x++ to x = x + 1 and x-- to x = x - 1
            // Create a duplicate identifier node for the RHS
            ASTNode* rhs_ident = ast_node_new(AST_IDENTIFIER);
            set_node_location(rhs_ident, operator);
            rhs_ident->as.identifier = strdup(expr->as.identifier);
            
            // Create the literal 1
            ASTNode* one_node = ast_node_new(AST_LITERAL);
            set_node_location(one_node, operator);
            one_node->as.literal.type = VALUE_NUMBER;
            one_node->as.literal.as.number = 1.0;
            
            // Create the binary operation: ident + 1 or ident - 1
            ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
            set_node_location(binary_node, operator);
            binary_node->as.binary.left = rhs_ident;
            binary_node->as.binary.operator = (operator->type == TOKEN_INCREMENT) ? TOKEN_PLUS : TOKEN_MINUS;
            binary_node->as.binary.right = one_node;
            
            // Create the assignment node
            ASTNode* assign_node = ast_node_new(AST_ASSIGNMENT);
            set_node_location(assign_node, operator);
            assign_node->as.assignment.target = expr;
            assign_node->as.assignment.value = binary_node;
            assign_node->as.assignment.operator = TOKEN_ASSIGN;
            
            expr = assign_node;
        } else {
            break;
        }
    }
    
    return expr;
}

static ASTNode* unary(Parser* parser) {
    if (parser_match(parser, TOKEN_NOT) || parser_match(parser, TOKEN_MINUS)) {
        Token* operator = previous_token(parser);
        ASTNode* right = unary(parser);
        ASTNode* node = ast_node_new(AST_UNARY_OP);
        set_node_location(node, operator);
        node->as.unary.operator = operator->type;
        node->as.unary.operand = right;
        return node;
    }
    
    return call(parser);
}

static ASTNode* factor(Parser* parser) {
    ASTNode* expr = unary(parser);
    
    while (parser_match(parser, TOKEN_DIVIDE) || parser_match(parser, TOKEN_MULTIPLY) || parser_match(parser, TOKEN_MODULO)) {
        Token* operator = previous_token(parser);
        ASTNode* right = unary(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* term(Parser* parser) {
    ASTNode* expr = factor(parser);
    
    while (parser_match(parser, TOKEN_MINUS) || parser_match(parser, TOKEN_PLUS)) {
        Token* operator = previous_token(parser);
        ASTNode* right = factor(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* range(Parser* parser) {
    ASTNode* expr = term(parser);
    
    if (parser_match(parser, TOKEN_DOT_DOT)) {
        Token* operator = previous_token(parser);
        ASTNode* right = term(parser);
        
        // Create a function call to a built-in range function
        // range(start, end) returns an array [start, start+1, ..., end-1]
        ASTNode* call_node = ast_node_new(AST_CALL);
        set_node_location(call_node, operator);
        
        ASTNode* range_fn = ast_node_new(AST_IDENTIFIER);
        set_node_location(range_fn, operator);
        range_fn->as.identifier = strdup("__range");
        
        call_node->as.call.function = range_fn;
        call_node->as.call.args = malloc(sizeof(ASTNode*) * 2);
        call_node->as.call.args[0] = expr;
        call_node->as.call.args[1] = right;
        call_node->as.call.arg_count = 2;
        
        return call_node;
    }
    
    return expr;
}

static ASTNode* comparison(Parser* parser) {
    ASTNode* expr = range(parser);
    
    while (parser_match(parser, TOKEN_GREATER) || parser_match(parser, TOKEN_GREATER_EQUAL) ||
           parser_match(parser, TOKEN_LESS) || parser_match(parser, TOKEN_LESS_EQUAL)) {
        Token* operator = previous_token(parser);
        ASTNode* right = range(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* equality(Parser* parser) {
    ASTNode* expr = comparison(parser);
    
    while (parser_match(parser, TOKEN_NOT_EQUAL) || parser_match(parser, TOKEN_EQUAL)) {
        Token* operator = previous_token(parser);
        ASTNode* right = comparison(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* logical_and(Parser* parser) {
    ASTNode* expr = equality(parser);
    
    while (parser_match(parser, TOKEN_AND)) {
        Token* operator = previous_token(parser);
        ASTNode* right = equality(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* logical_or(Parser* parser) {
    ASTNode* expr = logical_and(parser);
    
    while (parser_match(parser, TOKEN_OR)) {
        Token* operator = previous_token(parser);
        ASTNode* right = logical_and(parser);
        ASTNode* binary_node = ast_node_new(AST_BINARY_OP);
        set_node_location(binary_node, operator);
        binary_node->as.binary.left = expr;
        binary_node->as.binary.operator = operator->type;
        binary_node->as.binary.right = right;
        expr = binary_node;
    }
    
    return expr;
}

static ASTNode* assignment(Parser* parser) {
    ASTNode* expr = logical_or(parser);
    
    if (parser_match(parser, TOKEN_ASSIGN) || parser_match(parser, TOKEN_PLUS_ASSIGN) || parser_match(parser, TOKEN_MINUS_ASSIGN)) {
        Token* equals = previous_token(parser);
        ASTNode* value = assignment(parser);
        
        ASTNode* assign_node = ast_node_new(AST_ASSIGNMENT);
        set_node_location(assign_node, equals);
        assign_node->as.assignment.target = expr;
        assign_node->as.assignment.value = value;
        assign_node->as.assignment.operator = equals->type;
        return assign_node;
    }
    
    return expr;
}

static ASTNode* expression(Parser* parser) {
    return assignment(parser);
}

// Statement parsing
static ASTNode* block_statement(Parser* parser) {
    ASTNode* block_node = ast_node_new(AST_BLOCK);
    // Location at '{'
    set_node_location(block_node, previous_token(parser));
    block_node->as.block.statements = NULL;
    block_node->as.block.count = 0;
    block_node->as.block.capacity = 0;
    
    while (!check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
        // Skip newlines
        if (parser_match(parser, TOKEN_NEWLINE)) continue;
        
        ASTNode* stmt = declaration(parser);
        if (stmt != NULL) {
            if (block_node->as.block.count >= block_node->as.block.capacity) {
                int old_capacity = block_node->as.block.capacity;
                block_node->as.block.capacity = old_capacity < 8 ? 8 : old_capacity * 2;
                block_node->as.block.statements = realloc(block_node->as.block.statements,
                    sizeof(ASTNode*) * block_node->as.block.capacity);
            }
            block_node->as.block.statements[block_node->as.block.count++] = stmt;
            
            // Optionally consume semicolon or newline after statement in block
            if (parser_match(parser, TOKEN_SEMICOLON)) {
                // Semicolon consumed, continue
            } else if (check(parser, TOKEN_NEWLINE) || check(parser, TOKEN_RIGHT_BRACE) || parser_is_at_end(parser)) {
                // Newline, end of block, or EOF is fine too
            }
            
            // Optionally consume semicolon or newline after statement in block
            if (parser_match(parser, TOKEN_SEMICOLON)) {
                // Semicolon consumed, continue
            } else if (check(parser, TOKEN_NEWLINE) || check(parser, TOKEN_RIGHT_BRACE) || parser_is_at_end(parser)) {
                // Newline, end of block, or EOF is fine too
            }
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
    return block_node;
}

static ASTNode* if_statement(Parser* parser) {
    ASTNode* if_node = ast_node_new(AST_IF_STMT);
    set_node_location(if_node, previous_token(parser)); // 'if'
    
    // Suppress struct literal parsing to avoid { being interpreted as struct literal
    // Let the expression parser handle any parentheses - don't try to match them here
    bool old_suppress = parser->suppress_struct_literal;
    parser->suppress_struct_literal = true;
    
    if_node->as.if_stmt.condition = expression(parser);
    
    // Restore struct literal parsing
    parser->suppress_struct_literal = old_suppress;
    
    if_node->as.if_stmt.then_branch = statement(parser);
    if_node->as.if_stmt.else_branch = NULL;
    
    if (parser_match(parser, TOKEN_ELSE)) {
        if_node->as.if_stmt.else_branch = statement(parser);
    }
    
    return if_node;
}

static ASTNode* while_statement(Parser* parser) {
    ASTNode* while_node = ast_node_new(AST_WHILE_STMT);
    set_node_location(while_node, previous_token(parser)); // 'while'
    // Support optional parentheses around condition
    bool has_paren = parser_match(parser, TOKEN_LEFT_PAREN);
    
    // If no parentheses, suppress struct literal parsing
    bool old_suppress = parser->suppress_struct_literal;
    if (!has_paren) {
        parser->suppress_struct_literal = true;
    }
    
    while_node->as.while_stmt.condition = expression(parser);
    
    // Restore struct literal parsing
    parser->suppress_struct_literal = old_suppress;
    
    if (has_paren) {
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after while condition.");
    }
    
    while_node->as.while_stmt.body = statement(parser);
    
    return while_node;
}

// Forward declaration
static ASTNode* var_declaration(Parser* parser);

static ASTNode* for_statement(Parser* parser) {
    Token* for_token = previous_token(parser); // 'for'
    
    // Check if this is a for..in loop: `for variable in iterable {`
    // Peek ahead to see if we have: IDENTIFIER IN
    if (check(parser, TOKEN_IDENTIFIER)) {
        Token* var_token = parser_advance(parser);
        if (parser_match(parser, TOKEN_IN)) {
            // This is a for..in loop - desugar it to a while loop
            // for i in iterable { body }
            // becomes:
            // let __iter = iterable
            // let __index = 0
            // while __index < __iter.length {
            //     let i = __iter[__index]
            //     body
            //     __index = __index + 1
            // }
            
            // Parse the iterable expression
            ASTNode* iterable = expression(parser);
            
            // Parse the body
            ASTNode* body = statement(parser);
            
            // Generate unique names for iterator variables
            static int iter_counter = 0;
            char iter_name[64], index_name[64];
            snprintf(iter_name, sizeof(iter_name), "__iter%d", iter_counter);
            snprintf(index_name, sizeof(index_name), "__index%d", iter_counter);
            iter_counter++;
            
            // Create: let __iter = iterable
            ASTNode* iter_decl = ast_node_new(AST_VAR_DECL);
            set_node_location(iter_decl, var_token);
            iter_decl->as.var_decl.name = strdup(iter_name);
            iter_decl->as.var_decl.value = iterable;
            
            // Create: let __index = 0
            ASTNode* index_decl = ast_node_new(AST_VAR_DECL);
            set_node_location(index_decl, var_token);
            index_decl->as.var_decl.name = strdup(index_name);
            ASTNode* zero = ast_node_new(AST_LITERAL);
            set_node_location(zero, var_token);
            zero->as.literal.type = VALUE_NUMBER;
            zero->as.literal.as.number = 0;
            index_decl->as.var_decl.value = zero;
            
            // Create: __iter.length
            ASTNode* iter_id = ast_node_new(AST_IDENTIFIER);
            set_node_location(iter_id, var_token);
            iter_id->as.identifier = strdup(iter_name);
            ASTNode* length_access = ast_node_new(AST_MEMBER_ACCESS);
            set_node_location(length_access, var_token);
            length_access->as.member.object = iter_id;
            length_access->as.member.property = strdup("length");
            
            // Create: __index < __iter.length
            ASTNode* index_id1 = ast_node_new(AST_IDENTIFIER);
            set_node_location(index_id1, var_token);
            index_id1->as.identifier = strdup(index_name);
            ASTNode* condition = ast_node_new(AST_BINARY_OP);
            set_node_location(condition, var_token);
            condition->as.binary.left = index_id1;
            condition->as.binary.operator = TOKEN_LESS;
            condition->as.binary.right = length_access;
            
            // Create: let i = __iter[__index]
            ASTNode* iter_id2 = ast_node_new(AST_IDENTIFIER);
            set_node_location(iter_id2, var_token);
            iter_id2->as.identifier = strdup(iter_name);
            ASTNode* index_id2 = ast_node_new(AST_IDENTIFIER);
            set_node_location(index_id2, var_token);
            index_id2->as.identifier = strdup(index_name);
            ASTNode* index_access = ast_node_new(AST_ARRAY_ACCESS);
            set_node_location(index_access, var_token);
            index_access->as.array_access.array = iter_id2;
            index_access->as.array_access.index = index_id2;
            
            ASTNode* loop_var_decl = ast_node_new(AST_VAR_DECL);
            set_node_location(loop_var_decl, var_token);
            loop_var_decl->as.var_decl.name = strndup(var_token->start, var_token->length);
            loop_var_decl->as.var_decl.value = index_access;
            
            // Create: __index = __index + 1
            ASTNode* index_id3 = ast_node_new(AST_IDENTIFIER);
            set_node_location(index_id3, var_token);
            index_id3->as.identifier = strdup(index_name);
            ASTNode* index_id4 = ast_node_new(AST_IDENTIFIER);
            set_node_location(index_id4, var_token);
            index_id4->as.identifier = strdup(index_name);
            ASTNode* one = ast_node_new(AST_LITERAL);
            set_node_location(one, var_token);
            one->as.literal.type = VALUE_NUMBER;
            one->as.literal.as.number = 1;
            ASTNode* increment = ast_node_new(AST_BINARY_OP);
            set_node_location(increment, var_token);
            increment->as.binary.left = index_id4;
            increment->as.binary.operator = TOKEN_PLUS;
            increment->as.binary.right = one;
            ASTNode* index_update = ast_node_new(AST_ASSIGNMENT);
            set_node_location(index_update, var_token);
            ASTNode* index_id5 = ast_node_new(AST_IDENTIFIER);
            set_node_location(index_id5, var_token);
            index_id5->as.identifier = strdup(index_name);
            index_update->as.assignment.target = index_id5;
            index_update->as.assignment.value = increment;
            index_update->as.assignment.operator = TOKEN_ASSIGN;
            
            // Wrap the body in a block that includes the loop variable declaration
            ASTNode* block = ast_node_new(AST_BLOCK);
            set_node_location(block, var_token);
            block->as.block.statements = malloc(sizeof(ASTNode*) * 3);
            block->as.block.count = 3;
            block->as.block.statements[0] = loop_var_decl;
            block->as.block.statements[1] = body;
            block->as.block.statements[2] = index_update;
            
            // Create the while loop
            ASTNode* while_node = ast_node_new(AST_WHILE_STMT);
            set_node_location(while_node, for_token);
            while_node->as.while_stmt.condition = condition;
            while_node->as.while_stmt.body = block;
            
            // Wrap everything in an outer block
            ASTNode* outer_block = ast_node_new(AST_BLOCK);
            set_node_location(outer_block, for_token);
            outer_block->as.block.statements = malloc(sizeof(ASTNode*) * 3);
            outer_block->as.block.count = 3;
            outer_block->as.block.statements[0] = iter_decl;
            outer_block->as.block.statements[1] = index_decl;
            outer_block->as.block.statements[2] = while_node;
            
            return outer_block;
        }
        // Not a for..in loop, backtrack
        parser->current--;
    }
    
    // Classic C-style for loop: for (init; condition; update) { body }
    bool has_paren = parser_match(parser, TOKEN_LEFT_PAREN);
    
    ASTNode* for_node = ast_node_new(AST_FOR_STMT);
    set_node_location(for_node, for_token);
    
    // Parse initializer (can be variable declaration or expression)
    if (parser_match(parser, TOKEN_LET)) {
        for_node->as.for_stmt.init = var_declaration(parser);
    } else if (!check(parser, TOKEN_SEMICOLON)) {
        for_node->as.for_stmt.init = expression(parser);
    } else {
        for_node->as.for_stmt.init = NULL;
    }
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after for loop initializer.");
    
    // Parse condition
    if (!check(parser, TOKEN_SEMICOLON)) {
        for_node->as.for_stmt.condition = expression(parser);
    } else {
        for_node->as.for_stmt.condition = NULL;
    }
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after for loop condition.");
    
    // Parse update
    if (has_paren) {
        if (!check(parser, TOKEN_RIGHT_PAREN)) {
            for_node->as.for_stmt.update = expression(parser);
        } else {
            for_node->as.for_stmt.update = NULL;
        }
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
    } else {
        // Without parentheses, parse update expression if present
        // Stop naturally before the loop body (e.g., '{' or newline)
        if (!check(parser, TOKEN_LEFT_BRACE) && !check(parser, TOKEN_NEWLINE)) {
            for_node->as.for_stmt.update = expression(parser);
        } else {
            for_node->as.for_stmt.update = NULL;
        }
    }
    
    // Parse body
    for_node->as.for_stmt.body = statement(parser);
    
    return for_node;
}

static ASTNode* return_statement(Parser* parser) {
    ASTNode* return_node = ast_node_new(AST_RETURN_STMT);
    set_node_location(return_node, previous_token(parser)); // 'return'
    
    if (check(parser, TOKEN_SEMICOLON) || check(parser, TOKEN_NEWLINE)) {
        return_node->as.return_stmt.value = NULL;
    } else {
        // Parse one or more expressions: support `return a, b, c` as an array literal
        ASTNode* first = expression(parser);
        if (!check(parser, TOKEN_COMMA)) {
            return_node->as.return_stmt.value = first;
        } else {
            // Build an array literal node with collected expressions
            ASTNode* arr = ast_node_new(AST_ARRAY_LITERAL);
            set_node_location(arr, previous_token(parser));
            arr->as.array_literal.elements = NULL;
            arr->as.array_literal.count = 0;
            arr->as.array_literal.capacity = 0;
            // append first and subsequent expressions
            if (arr->as.array_literal.count >= arr->as.array_literal.capacity) {
                int old_cap = arr->as.array_literal.capacity;
                arr->as.array_literal.capacity = old_cap < 4 ? 4 : old_cap * 2;
                arr->as.array_literal.elements = realloc(arr->as.array_literal.elements, sizeof(ASTNode*) * arr->as.array_literal.capacity);
            }
            arr->as.array_literal.elements[arr->as.array_literal.count++] = first;
            while (parser_match(parser, TOKEN_COMMA)) {
                ASTNode* next = expression(parser);
                if (arr->as.array_literal.count >= arr->as.array_literal.capacity) {
                    int old_cap = arr->as.array_literal.capacity;
                    arr->as.array_literal.capacity = old_cap < 4 ? 4 : old_cap * 2;
                    arr->as.array_literal.elements = realloc(arr->as.array_literal.elements, sizeof(ASTNode*) * arr->as.array_literal.capacity);
                }
                arr->as.array_literal.elements[arr->as.array_literal.count++] = next;
            }
            return_node->as.return_stmt.value = arr;
        }
    }
    
    return return_node;
}

static ASTNode* defer_statement(Parser* parser) {
    ASTNode* defer_node = ast_node_new(AST_DEFER_STMT);
    set_node_location(defer_node, previous_token(parser)); // 'defer'
    
    // defer must be followed by a function call
    ASTNode* call_expr = expression(parser);
    
    if (call_expr->type != AST_CALL) {
        error_at(parser, previous_token(parser), "defer must be followed by a function call.");
        ast_node_free(call_expr);
        return defer_node; // Return empty defer node to avoid crash
    }
    
    defer_node->as.defer_stmt.call_expr = call_expr;
    return defer_node;
}

static ASTNode* avatar_statement(Parser* parser) {
    ASTNode* avatar_node = ast_node_new(AST_AVATAR_STMT);
    set_node_location(avatar_node, previous_token(parser)); // 'avatar'
    
    // Check if it's an anonymous function: avatar func() { ... } or avatar fn() { ... }
    if (parser_match(parser, TOKEN_FN) || check(parser, TOKEN_LEFT_PAREN)) {
        // Parse anonymous function directly
        // Back up if we consumed TOKEN_FN
        if (previous_token(parser)->type == TOKEN_FN) {
            // Already consumed TOKEN_FN, continue
        }
        
        // Parse parameters
        consume(parser, TOKEN_LEFT_PAREN, "Expect '(' for avatar function.");
        
        ASTNode* anon_func = ast_node_new(AST_ANONYMOUS_FUNCTION);
        set_node_location(anon_func, previous_token(parser));
        anon_func->as.anonymous_function.params = NULL;
        anon_func->as.anonymous_function.param_count = 0;
        anon_func->as.anonymous_function.is_arrow = false;
        
        // Parse parameters
        if (!check(parser, TOKEN_RIGHT_PAREN)) {
            int capacity = 4;
            anon_func->as.anonymous_function.params = malloc(sizeof(char*) * capacity);
            
            do {
                if (anon_func->as.anonymous_function.param_count >= capacity) {
                    capacity *= 2;
                    anon_func->as.anonymous_function.params = realloc(anon_func->as.anonymous_function.params, sizeof(char*) * capacity);
                }
                
                Token* param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
                char* param_name = malloc(param->length + 1);
                memcpy(param_name, param->start, param->length);
                param_name[param->length] = '\0';
                anon_func->as.anonymous_function.params[anon_func->as.anonymous_function.param_count++] = param_name;
            } while (parser_match(parser, TOKEN_COMMA));
        }
        
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
        
        // Parse body
        consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before avatar function body.");
        anon_func->as.anonymous_function.body = block_statement(parser);
        
        avatar_node->as.avatar_stmt.call_expr = anon_func;
        avatar_node->as.avatar_stmt.is_anonymous = true;
    } else {
        // Parse named function call: avatar functionName(args)
        ASTNode* call_expr = expression(parser); // This should be a call expression
        
        // Validate it's actually a call expression
        if (call_expr->type != AST_CALL) {
            error_at(parser, previous_token(parser), "Expected function call after 'avatar'.");
        }
        
        avatar_node->as.avatar_stmt.call_expr = call_expr;
        avatar_node->as.avatar_stmt.is_anonymous = false;
    }
    
    return avatar_node;
}

static ASTNode* expression_statement(Parser* parser) {
    // Check for import "string" syntax (without parentheses)
    if (check(parser, TOKEN_IDENTIFIER)) {
        Token* id_token = current_token(parser);
        if (id_token->length == 6 && memcmp(id_token->start, "import", 6) == 0) {
            int lookahead_pos = parser->current + 1;
            if (lookahead_pos < parser->count && parser->tokens[lookahead_pos].type == TOKEN_STRING) {
                // It's import "path" statement
                parser_advance(parser); // consume 'import' identifier
                Token* path_token = consume(parser, TOKEN_STRING, "Expect string path after 'import'.");
                
                // Extract the path string (remove quotes)
                int path_len = path_token->length - 2; // Remove opening and closing quotes
                char* path = malloc(path_len + 1);     // +1 for null terminator
                memcpy(path, path_token->start + 1, path_len);
                path[path_len] = '\0';
                
                // Check for 'as namespace' syntax
                if (parser_match(parser, TOKEN_AS)) {
                    Token* ns_token = consume(parser, TOKEN_IDENTIFIER, "Expect namespace identifier after 'as'.");
                    char* ns_name = malloc(ns_token->length + 1);
                    memcpy(ns_name, ns_token->start, ns_token->length);
                    ns_name[ns_token->length] = '\0';
                    
                    // Create import_as() call: import_as("path", "namespace")
                    ASTNode* import_call = ast_node_new(AST_CALL);
                    set_node_location(import_call, path_token);
                    
                    ASTNode* func_name = ast_node_new(AST_IDENTIFIER);
                    func_name->as.identifier = strdup("import_as");
                    import_call->as.call.function = func_name;
                    
                    import_call->as.call.arg_count = 2;
                    import_call->as.call.args = malloc(sizeof(ASTNode*) * 2);
                    
                    // First arg: path string
                    ASTNode* path_lit = ast_node_new(AST_LITERAL);
                    path_lit->as.literal.type = VALUE_STRING;
                    path_lit->as.literal.as.string = path;
                    import_call->as.call.args[0] = path_lit;
                    
                    // Second arg: namespace string
                    ASTNode* ns_lit = ast_node_new(AST_LITERAL);
                    ns_lit->as.literal.type = VALUE_STRING;
                    ns_lit->as.literal.as.string = ns_name;
                    import_call->as.call.args[1] = ns_lit;
                    
                    // Wrap in variable declaration: let namespace = import_as(...)
                    ASTNode* var_decl = ast_node_new(AST_VAR_DECL);
                    set_node_location(var_decl, ns_token);
                    var_decl->as.var_decl.name = strdup(ns_name);
                    var_decl->as.var_decl.value = import_call;
                    
                    return var_decl;
                } else {
                    // Simple import("path") as expression statement
                    ASTNode* import_call = ast_node_new(AST_CALL);
                    set_node_location(import_call, path_token);
                    
                    ASTNode* func_name = ast_node_new(AST_IDENTIFIER);
                    func_name->as.identifier = strdup("import");
                    import_call->as.call.function = func_name;
                    
                    import_call->as.call.arg_count = 1;
                    import_call->as.call.args = malloc(sizeof(ASTNode*));
                    
                    ASTNode* path_lit = ast_node_new(AST_LITERAL);
                    path_lit->as.literal.type = VALUE_STRING;
                    path_lit->as.literal.as.string = path;
                    import_call->as.call.args[0] = path_lit;
                    
                    ASTNode* stmt = ast_node_new(AST_EXPRESSION_STMT);
                    stmt->as.expression = import_call;
                    return stmt;
                }
            }
        }
    }
    
    // Handle empty expression statements (just semicolons)
    if (check(parser, TOKEN_SEMICOLON)) {
        // Create a nil expression for empty statements
        ASTNode* nil_expr = ast_node_new(AST_LITERAL);
        set_node_location(nil_expr, current_token(parser));
        nil_expr->as.literal.type = VALUE_NIL;
        ASTNode* stmt = ast_node_new(AST_EXPRESSION_STMT);
        // Best-effort: put statement at current token position
        set_node_location(stmt, current_token(parser));
        stmt->as.expression = nil_expr;
        return stmt;
    }
    
    ASTNode* expr = expression(parser);
    
    // Check for "import(...) as namespace" pattern
    if (parser_match(parser, TOKEN_AS)) {
        // expr should be a call to import()
        if (expr && expr->type == AST_CALL) {
            // Get the function being called
            ASTNode* func = expr->as.call.function;
            if (func && func->type == AST_IDENTIFIER) {
                // Check if it's "import"
                if (strcmp(func->as.identifier, "import") == 0) {
                    // Expect namespace identifier
                    Token* ns_token = consume(parser, TOKEN_IDENTIFIER, "Expect namespace identifier after 'as'.");
                    char* ns_name = malloc(ns_token->length + 1);
                    memcpy(ns_name, ns_token->start, ns_token->length);
                    ns_name[ns_token->length] = '\0';
                    
                    // Transform into: let namespace = import_as(path, "namespace")
                    // 1. Change function name from "import" to "import_as"
                    free(func->as.identifier);
                    func->as.identifier = strdup("import_as");
                    
                    // 2. Add namespace name as second argument
                    expr->as.call.args = realloc(expr->as.call.args, sizeof(ASTNode*) * (expr->as.call.arg_count + 1));
                    ASTNode* ns_lit = ast_node_new(AST_LITERAL);
                    set_node_location(ns_lit, ns_token);
                    ns_lit->as.literal.type = VALUE_STRING;
                    ns_lit->as.literal.as.string = ns_name;
                    expr->as.call.args[expr->as.call.arg_count] = ns_lit;
                    expr->as.call.arg_count++;
                    
                    // 3. Wrap in variable declaration: let namespace = import_as(...)
                    ASTNode* var_decl = ast_node_new(AST_VAR_DECL);
                    set_node_location(var_decl, ns_token);
                    var_decl->as.var_decl.name = strdup(ns_name);
                    var_decl->as.var_decl.value = expr;
                    
                    return var_decl;
                }
            }
        }
        // If not import, it's an error
        error(parser, "'as' can only be used with import()");
    }
    
    ASTNode* stmt = ast_node_new(AST_EXPRESSION_STMT);
    // Propagate the expression location to the statement for better error reporting
    stmt->line = expr ? expr->line : stmt->line;
    stmt->column = expr ? expr->column : stmt->column;
    stmt->as.expression = expr;
    return stmt;
}

static ASTNode* switch_statement(Parser* parser) {
    Token* switch_token = previous_token(parser); // 'switch'
    
    ASTNode* switch_node = ast_node_new(AST_SWITCH_STMT);
    set_node_location(switch_node, switch_token);
    
    // Prevent struct literal parsing in the switch value expression
    bool old_suppress = parser->suppress_struct_literal;
    parser->suppress_struct_literal = true;
    
    // Parse the value to switch on
    switch_node->as.switch_stmt.value = expression(parser);
    
    // Restore context
    parser->suppress_struct_literal = old_suppress;
    
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after switch expression.");
    
    // Parse cases
    switch_node->as.switch_stmt.case_values = NULL;
    switch_node->as.switch_stmt.case_bodies = NULL;
    switch_node->as.switch_stmt.case_count = 0;
    switch_node->as.switch_stmt.default_body = NULL;
    
    int capacity = 4;
    switch_node->as.switch_stmt.case_values = malloc(sizeof(ASTNode*) * capacity);
    switch_node->as.switch_stmt.case_bodies = malloc(sizeof(ASTNode*) * capacity);
    
    while (!check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
        // Skip newlines
        while (parser_match(parser, TOKEN_NEWLINE));
        
        if (check(parser, TOKEN_RIGHT_BRACE)) break;
        
        if (parser_match(parser, TOKEN_CASE)) {
            // Parse case value
            ASTNode* case_value = expression(parser);
            
            consume(parser, TOKEN_COLON, "Expect ':' after case value.");
            
            // Skip newlines after colon
            while (parser_match(parser, TOKEN_NEWLINE));
            
            // Parse case body (statements until next case/default/})
            ASTNode* case_body = ast_node_new(AST_BLOCK);
            case_body->as.block.statements = NULL;
            case_body->as.block.count = 0;
            case_body->as.block.capacity = 0;
            
            while (!check(parser, TOKEN_CASE) && !check(parser, TOKEN_DEFAULT) && !check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
                // Skip newlines
                while (parser_match(parser, TOKEN_NEWLINE));
                
                if (check(parser, TOKEN_CASE) || check(parser, TOKEN_DEFAULT) || check(parser, TOKEN_RIGHT_BRACE)) break;
                
                ASTNode* stmt = statement(parser);
                if (stmt != NULL) {
                    if (case_body->as.block.count >= case_body->as.block.capacity) {
                        int old_cap = case_body->as.block.capacity;
                        case_body->as.block.capacity = old_cap < 4 ? 4 : old_cap * 2;
                        case_body->as.block.statements = realloc(case_body->as.block.statements, sizeof(ASTNode*) * case_body->as.block.capacity);
                    }
                    case_body->as.block.statements[case_body->as.block.count++] = stmt;
                }
            }
            
            // Add case to switch
            if (switch_node->as.switch_stmt.case_count >= capacity) {
                capacity *= 2;
                switch_node->as.switch_stmt.case_values = realloc(switch_node->as.switch_stmt.case_values, sizeof(ASTNode*) * capacity);
                switch_node->as.switch_stmt.case_bodies = realloc(switch_node->as.switch_stmt.case_bodies, sizeof(ASTNode*) * capacity);
            }
            
            switch_node->as.switch_stmt.case_values[switch_node->as.switch_stmt.case_count] = case_value;
            switch_node->as.switch_stmt.case_bodies[switch_node->as.switch_stmt.case_count] = case_body;
            switch_node->as.switch_stmt.case_count++;
            
        } else if (parser_match(parser, TOKEN_DEFAULT)) {
            consume(parser, TOKEN_COLON, "Expect ':' after default.");
            
            // Skip newlines after colon
            while (parser_match(parser, TOKEN_NEWLINE));
            
            // Parse default body
            ASTNode* default_body = ast_node_new(AST_BLOCK);
            default_body->as.block.statements = NULL;
            default_body->as.block.count = 0;
            default_body->as.block.capacity = 0;
            
            while (!check(parser, TOKEN_CASE) && !check(parser, TOKEN_DEFAULT) && !check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
                // Skip newlines
                while (parser_match(parser, TOKEN_NEWLINE));
                
                if (check(parser, TOKEN_CASE) || check(parser, TOKEN_DEFAULT) || check(parser, TOKEN_RIGHT_BRACE)) break;
                
                ASTNode* stmt = statement(parser);
                if (stmt != NULL) {
                    if (default_body->as.block.count >= default_body->as.block.capacity) {
                        int old_cap = default_body->as.block.capacity;
                        default_body->as.block.capacity = old_cap < 4 ? 4 : old_cap * 2;
                        default_body->as.block.statements = realloc(default_body->as.block.statements, sizeof(ASTNode*) * default_body->as.block.capacity);
                    }
                    default_body->as.block.statements[default_body->as.block.count++] = stmt;
                }
            }
            
            switch_node->as.switch_stmt.default_body = default_body;
        } else {
            error_at(parser, current_token(parser), "Expect 'case' or 'default' in switch statement.");
            break;
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after switch body.");
    
    return switch_node;
}

static ASTNode* statement(Parser* parser) {
    // Destructuring assignment at statement start: a, b, c = expr
    // Lookahead to detect pattern IDENT (',' IDENT)+ '='
    int save_pos = parser->current;
    if (check(parser, TOKEN_IDENTIFIER)) {
        int la_pos = parser->current;
        int names_seen = 0;
        bool has_comma = false;
        // Consume first ident
        la_pos++;
        names_seen++;
        // Repeated (',' IDENT)
        while (la_pos < parser->count && parser->tokens[la_pos].type == TOKEN_COMMA) {
            has_comma = true;
            la_pos++; // skip comma
            if (la_pos < parser->count && parser->tokens[la_pos].type == TOKEN_IDENTIFIER) {
                la_pos++; // skip identifier
                names_seen++;
            } else {
                break;
            }
        }
        if (has_comma && la_pos < parser->count && parser->tokens[la_pos].type == TOKEN_ASSIGN) {
            // Parse destructuring assignment into a single assignment with array-literal LHS
            // Collect identifiers into an array-literal node
            ASTNode* lhs_array = ast_node_new(AST_ARRAY_LITERAL);
            set_node_location(lhs_array, current_token(parser));
            lhs_array->as.array_literal.elements = NULL;
            lhs_array->as.array_literal.count = 0;
            lhs_array->as.array_literal.capacity = 0;
            
            // helper to append identifier into lhs_array
            #define APPEND_LHS_ID(nodePtr) \
                do { \
                    if (lhs_array->as.array_literal.count >= lhs_array->as.array_literal.capacity) { \
                        int old_cap = lhs_array->as.array_literal.capacity; \
                        lhs_array->as.array_literal.capacity = old_cap < 4 ? 4 : old_cap * 2; \
                        lhs_array->as.array_literal.elements = realloc(lhs_array->as.array_literal.elements, sizeof(ASTNode*) * lhs_array->as.array_literal.capacity); \
                    } \
                    lhs_array->as.array_literal.elements[lhs_array->as.array_literal.count++] = (nodePtr); \
                } while (0)
            
            do {
                Token* ident = consume(parser, TOKEN_IDENTIFIER, "Expect identifier in destructuring assignment.");
                ASTNode* id_node = ast_node_new(AST_IDENTIFIER);
                set_node_location(id_node, ident);
                char* n = malloc(ident->length + 1);
                memcpy(n, ident->start, ident->length);
                n[ident->length] = '\0';
                id_node->as.identifier = n;
                APPEND_LHS_ID(id_node);
            } while (parser_match(parser, TOKEN_COMMA));
            consume(parser, TOKEN_ASSIGN, "Expect '=' in destructuring assignment.");
            ASTNode* rhs = expression(parser);

            ASTNode* assign_node = ast_node_new(AST_ASSIGNMENT);
            set_node_location(assign_node, previous_token(parser));
            assign_node->as.assignment.target = lhs_array;
            assign_node->as.assignment.value = rhs;
            assign_node->as.assignment.operator = TOKEN_ASSIGN;
            // Wrap as expression statement for consistency
            ASTNode* stmt = ast_node_new(AST_EXPRESSION_STMT);
            set_node_location(stmt, previous_token(parser));
            stmt->as.expression = assign_node;
            return stmt;
        }
        // not destructuring; reset
        parser->current = save_pos;
    }
    if (parser_match(parser, TOKEN_IF)) return if_statement(parser);
    if (parser_match(parser, TOKEN_SWITCH)) return switch_statement(parser);
    if (parser_match(parser, TOKEN_WHILE)) return while_statement(parser);
    if (parser_match(parser, TOKEN_FOR)) return for_statement(parser);
    if (parser_match(parser, TOKEN_AVATAR)) return avatar_statement(parser);
    if (parser_match(parser, TOKEN_DEFER)) return defer_statement(parser);
    if (parser_match(parser, TOKEN_RETURN)) return return_statement(parser);
    if (parser_match(parser, TOKEN_LEFT_BRACE)) return block_statement(parser);
    
    return expression_statement(parser);
}

static ASTNode* var_declaration(Parser* parser) {
    Token* let_token = previous_token(parser); // 'let'
    Token* name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
    
    // Normal single variable declaration
    ASTNode* var_node = ast_node_new(AST_VAR_DECL);
    set_node_location(var_node, let_token);
    char* var_name = malloc(name->length + 1);
    memcpy(var_name, name->start, name->length);
    var_name[name->length] = '\0';
    var_node->as.var_decl.name = var_name;
    
    if (parser_match(parser, TOKEN_ASSIGN)) {
        var_node->as.var_decl.value = expression(parser);
    } else {
        // Default to nil if no initializer
        ASTNode* nil_node = ast_node_new(AST_LITERAL);
        nil_node->as.literal.type = VALUE_NIL;
        var_node->as.var_decl.value = nil_node;
    }
    
    return var_node;
}

static ASTNode* function_declaration(Parser* parser) {
    Token* fn_token = previous_token(parser); // 'fn'
    Token* name = consume(parser, TOKEN_IDENTIFIER, "Expect function name.");
    
    ASTNode* fn_node = ast_node_new(AST_FUNCTION_DECL);
    set_node_location(fn_node, fn_token);
    char* fn_name = malloc(name->length + 1);
    memcpy(fn_name, name->start, name->length);
    fn_name[name->length] = '\0';
    fn_node->as.function_decl.name = fn_name;
    fn_node->as.function_decl.is_exported = false;  // Default to not exported
    
    // Initialize decorators (will be set by caller from declaration())
    decorator_list_init(&fn_node->as.function_decl.decorators);
    fn_node->as.function_decl.param_decorators = NULL;
    
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    
    // Parse parameters (with optional decorators)
    fn_node->as.function_decl.params = NULL;
    fn_node->as.function_decl.param_count = 0;
    
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        int capacity = 4;
        fn_node->as.function_decl.params = malloc(sizeof(char*) * capacity);
        fn_node->as.function_decl.param_decorators = malloc(sizeof(DecoratorList) * capacity);
        
        do {
            if (fn_node->as.function_decl.param_count >= capacity) {
                capacity *= 2;
                fn_node->as.function_decl.params = realloc(fn_node->as.function_decl.params, sizeof(char*) * capacity);
                fn_node->as.function_decl.param_decorators = realloc(fn_node->as.function_decl.param_decorators, sizeof(DecoratorList) * capacity);
            }
            
            // Parse parameter decorators (@inject, etc.)
            DecoratorList param_decs = parse_decorators(parser);
            
            Token* param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
            char* param_name = malloc(param->length + 1);
            memcpy(param_name, param->start, param->length);
            param_name[param->length] = '\0';
            fn_node->as.function_decl.params[fn_node->as.function_decl.param_count] = param_name;
            fn_node->as.function_decl.param_decorators[fn_node->as.function_decl.param_count] = param_decs;
            fn_node->as.function_decl.param_count++;
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    
    // Parse the block and extract the Block data
    ASTNode* block_node = block_statement(parser);
    fn_node->as.function_decl.body = malloc(sizeof(Block));
    fn_node->as.function_decl.body->statements = block_node->as.block.statements;
    fn_node->as.function_decl.body->count = block_node->as.block.count;
    fn_node->as.function_decl.body->capacity = block_node->as.block.capacity;
    
    return fn_node;
}

// Parse anonymous function: fn(params) { body }
static ASTNode* parse_anonymous_function(Parser* parser, bool is_arrow) {
    ASTNode* fn_node = ast_node_new(AST_ANONYMOUS_FUNCTION);
    set_node_location(fn_node, previous_token(parser)); // 'fn'
    fn_node->as.anonymous_function.is_arrow = is_arrow;
    
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'fn'.");
    
    // Parse parameters
    fn_node->as.anonymous_function.params = NULL;
    fn_node->as.anonymous_function.param_count = 0;
    
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        int capacity = 4;
        fn_node->as.anonymous_function.params = malloc(sizeof(char*) * capacity);
        
        do {
            if (fn_node->as.anonymous_function.param_count >= capacity) {
                capacity *= 2;
                fn_node->as.anonymous_function.params = realloc(fn_node->as.anonymous_function.params, sizeof(char*) * capacity);
            }
            
            Token* param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
            char* param_name = malloc(param->length + 1);
            memcpy(param_name, param->start, param->length);
            param_name[param->length] = '\0';
            fn_node->as.anonymous_function.params[fn_node->as.anonymous_function.param_count++] = param_name;
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    
    if (is_arrow) {
        consume(parser, TOKEN_ARROW, "Expect '=>' in arrow function.");
        // For arrow functions, body can be expression or block
        if (check(parser, TOKEN_LEFT_BRACE)) {
            consume(parser, TOKEN_LEFT_BRACE, "Expect '{' for block.");
            fn_node->as.anonymous_function.body = block_statement(parser);
        } else {
            fn_node->as.anonymous_function.body = expression(parser);
        }
    } else {
        consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
        fn_node->as.anonymous_function.body = block_statement(parser);
    }
    
    return fn_node;
}

static ASTNode* struct_declaration(Parser* parser) {
    Token* struct_token = previous_token(parser); // 'struct'
    Token* name = consume(parser, TOKEN_IDENTIFIER, "Expect struct name.");
    
    ASTNode* struct_node = ast_node_new(AST_STRUCT_DECL);
    set_node_location(struct_node, struct_token);
    
    char* struct_name = malloc(name->length + 1);
    memcpy(struct_name, name->start, name->length);
    struct_name[name->length] = '\0';
    struct_node->as.struct_decl.name = struct_name;
    
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after struct name.");
    
    // Initialize decorators (will be set by caller from declaration())
    decorator_list_init(&struct_node->as.struct_decl.decorators);
    struct_node->as.struct_decl.field_decorators = NULL;
    
    // Parse field names
    struct_node->as.struct_decl.fields = NULL;
    struct_node->as.struct_decl.field_count = 0;
    
    if (!check(parser, TOKEN_RIGHT_BRACE)) {
        int capacity = 4;
        struct_node->as.struct_decl.fields = malloc(sizeof(char*) * capacity);
        struct_node->as.struct_decl.field_decorators = malloc(sizeof(DecoratorList) * capacity);
        
        do {
            // Skip optional newlines
            while (parser_match(parser, TOKEN_NEWLINE));
            
            if (check(parser, TOKEN_RIGHT_BRACE)) break;
            
            if (struct_node->as.struct_decl.field_count >= capacity) {
                capacity *= 2;
                struct_node->as.struct_decl.fields = realloc(struct_node->as.struct_decl.fields, sizeof(char*) * capacity);
                struct_node->as.struct_decl.field_decorators = realloc(struct_node->as.struct_decl.field_decorators, sizeof(DecoratorList) * capacity);
            }
            
            // Parse field decorators
            DecoratorList field_decs = parse_decorators(parser);
            
            // Check if this is a typed field: type fieldName; (e.g., int32 status;)
            // or untyped field: fieldName (e.g., name)
            Token* first_ident = consume(parser, TOKEN_IDENTIFIER, "Expect field name or type.");
            
            char* field_name = NULL;
            
            // Check if next token is another identifier (typed field pattern)
            if (check(parser, TOKEN_IDENTIFIER)) {
                // This is a typed field: type fieldName
                // first_ident is the type, next is the field name
                Token* field = consume(parser, TOKEN_IDENTIFIER, "Expect field name.");
                field_name = malloc(field->length + 1);
                memcpy(field_name, field->start, field->length);
                field_name[field->length] = '\0';
                // We ignore the type for now (first_ident)
            } else {
                // This is an untyped field: just fieldName
                field_name = malloc(first_ident->length + 1);
                memcpy(field_name, first_ident->start, first_ident->length);
                field_name[first_ident->length] = '\0';
            }
            
            struct_node->as.struct_decl.fields[struct_node->as.struct_decl.field_count] = field_name;
            struct_node->as.struct_decl.field_decorators[struct_node->as.struct_decl.field_count] = field_decs;
            struct_node->as.struct_decl.field_count++;
            
            // Skip optional semicolon after field
            parser_match(parser, TOKEN_SEMICOLON);
            
            // Skip optional newlines after field
            while (parser_match(parser, TOKEN_NEWLINE));
            
            // Comma or newline can separate fields
            if (!check(parser, TOKEN_RIGHT_BRACE)) {
                if (!parser_match(parser, TOKEN_COMMA)) {
                    // Newline already handled above
                }
            }
        } while (!check(parser, TOKEN_RIGHT_BRACE));
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after struct fields.");
    
    return struct_node;
}

static ASTNode* interface_declaration_impl(Parser* parser, bool is_exported) {
    Token* interface_token = previous_token(parser); // 'interface'
    Token* name = consume(parser, TOKEN_IDENTIFIER, "Expect interface name.");

    // Check for optional "from" clause: interface NAME from "path" {
    char* library_path = NULL;
    if (check(parser, TOKEN_IDENTIFIER)) {
        Token* maybe_from = current_token(parser);
        if (maybe_from->length == 4 && strncmp(maybe_from->start, "from", 4) == 0) {
            parser_advance(parser); // consume 'from'
            Token* path_token = consume(parser, TOKEN_STRING, "Expect library path after 'from'.");
            // Extract path string (strip quotes)
            int path_len = path_token->length - 2;
            if (path_len < 0) path_len = 0;
            library_path = malloc(path_len + 1);
            if (path_len > 0) {
                memcpy(library_path, path_token->start + 1, path_len);
            }
            library_path[path_len] = '\0';
        }
    }

    // We'll lower the interface declaration into a block of runtime calls:
    //   loadlib("path") if from clause present
    //   bind_interface_method("<iface>", "<method>", ..., is_exported) for each method
    ASTNode* block = ast_node_new(AST_BLOCK);
    set_node_location(block, interface_token);
    block->as.block.statements = NULL;
    block->as.block.count = 0;
    block->as.block.capacity = 0;

    // If library_path was specified, inject dlopen_only() call first
    // This opens the library handle without registering all functions
    // Functions will be registered selectively via bind_interface_method
    if (library_path) {
        // Build: dlopen_only("path")
        ASTNode* dlopen_ident = ast_node_new(AST_IDENTIFIER);
        set_node_location(dlopen_ident, interface_token);
        dlopen_ident->as.identifier = strdup("dlopen_only");
        
        ASTNode* path_lit = ast_node_new(AST_LITERAL);
        set_node_location(path_lit, interface_token);
        path_lit->as.literal.type = VALUE_STRING;
        path_lit->as.literal.as.string = library_path; // ownership transferred
        
        ASTNode* dlopen_call = ast_node_new(AST_CALL);
        set_node_location(dlopen_call, interface_token);
        dlopen_call->as.call.function = dlopen_ident;
        dlopen_call->as.call.arg_count = 1;
        dlopen_call->as.call.args = malloc(sizeof(ASTNode*));
        dlopen_call->as.call.args[0] = path_lit;
        
        ASTNode* dlopen_stmt = ast_node_new(AST_EXPRESSION_STMT);
        set_node_location(dlopen_stmt, interface_token);
        dlopen_stmt->as.expression = dlopen_call;
        
        // Add to block
        if (block->as.block.count >= block->as.block.capacity) {
            block->as.block.capacity = block->as.block.capacity < 8 ? 8 : block->as.block.capacity * 2;
            block->as.block.statements = realloc(block->as.block.statements, sizeof(ASTNode*) * block->as.block.capacity);
        }
        block->as.block.statements[block->as.block.count++] = dlopen_stmt;
    }

    // Capture interface name string now
    char* iface_name = malloc(name->length + 1);
    memcpy(iface_name, name->start, name->length);
    iface_name[name->length] = '\0';

    // Add a compiler directive to register this namespace at compile time
    // Build: __register_namespace("interface_name")
    ASTNode* register_ident = ast_node_new(AST_IDENTIFIER);
    set_node_location(register_ident, interface_token);
    register_ident->as.identifier = strdup("__register_namespace");
    
    ASTNode* name_lit = ast_node_new(AST_LITERAL);
    set_node_location(name_lit, interface_token);
    name_lit->as.literal.type = VALUE_STRING;
    name_lit->as.literal.as.string = strdup(iface_name);
    
    ASTNode* register_call = ast_node_new(AST_CALL);
    set_node_location(register_call, interface_token);
    register_call->as.call.function = register_ident;
    register_call->as.call.args = malloc(sizeof(ASTNode*));
    register_call->as.call.args[0] = name_lit;
    register_call->as.call.arg_count = 1;
    
    ASTNode* register_stmt = ast_node_new(AST_EXPRESSION_STMT);
    set_node_location(register_stmt, interface_token);
    register_stmt->as.expression = register_call;
    
    // Add to block
    if (block->as.block.count >= block->as.block.capacity) {
        block->as.block.capacity = block->as.block.capacity < 8 ? 8 : block->as.block.capacity * 2;
        block->as.block.statements = realloc(block->as.block.statements, sizeof(ASTNode*) * block->as.block.capacity);
    }
    block->as.block.statements[block->as.block.count++] = register_stmt;

    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after interface name.");

    while (!check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
        // Skip newlines and stray commas
        while (parser_match(parser, TOKEN_NEWLINE) || parser_match(parser, TOKEN_COMMA));
        if (check(parser, TOKEN_RIGHT_BRACE)) break;

        // Collect optional @alias("alt1", "alt2", ... ) directives before a method
        ASTNode* alias_array = NULL;
        while (parser_match(parser, TOKEN_AT)) {
            Token* dir = consume(parser, TOKEN_IDENTIFIER, "Expect directive name after '@'.");
            // Only support alias here; if not alias, recover by skipping parens group
            bool is_alias = (dir->length == 5 && strncmp(dir->start, "alias", 5) == 0);
            consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after directive name.");
            if (is_alias) {
                // Build array of string literals
                if (!alias_array) {
                    alias_array = ast_node_new(AST_ARRAY_LITERAL);
                    set_node_location(alias_array, dir);
                    alias_array->as.array_literal.count = 0;
                    alias_array->as.array_literal.capacity = 4;
                    alias_array->as.array_literal.elements = malloc(sizeof(ASTNode*) * alias_array->as.array_literal.capacity);
                }
                if (!check(parser, TOKEN_RIGHT_PAREN)) {
                    do {
                        Token* s = consume(parser, TOKEN_STRING, "Expect string literal alias name.");
                        ASTNode* lit = ast_node_new(AST_LITERAL);
                        set_node_location(lit, s);
                        lit->as.literal.type = VALUE_STRING;
                        // Strip quotes from alias string (same as regular string literals)
                        int alias_len = s->length - 2; // Remove quotes
                        if (alias_len < 0) alias_len = 0;
                        char* an = malloc(alias_len + 1);
                        if (alias_len > 0) {
                            memcpy(an, s->start + 1, alias_len);
                        }
                        an[alias_len] = '\0';
                        lit->as.literal.as.string = an;
                        if (alias_array->as.array_literal.count >= alias_array->as.array_literal.capacity) {
                            int old = alias_array->as.array_literal.capacity;
                            alias_array->as.array_literal.capacity = old < 8 ? 8 : old * 2;
                            alias_array->as.array_literal.elements = realloc(alias_array->as.array_literal.elements, sizeof(ASTNode*) * alias_array->as.array_literal.capacity);
                        }
                        alias_array->as.array_literal.elements[alias_array->as.array_literal.count++] = lit;
                    } while (parser_match(parser, TOKEN_COMMA));
                }
            } else {
                // Skip until matching ')'
                int depth = 1;
                while (!parser_is_at_end(parser) && depth > 0) {
                    if (parser_match(parser, TOKEN_LEFT_PAREN)) depth++;
                    else if (parser_match(parser, TOKEN_RIGHT_PAREN)) depth--;
                    else parser_advance(parser);
                }
                continue;
            }
            consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after directive arguments.");
            // Allow multiple @alias directives
            while (parser_match(parser, TOKEN_NEWLINE));
        }

        // Tolerate optional leading return type or modifiers; find IDENT followed by '('
        Token* method_ident = NULL;
        Token* possible_return_type = NULL; // Capture simple single-token return type if present
        int start_pos = parser->current;
        (void)start_pos;  // May be used for error reporting
        while (!parser_is_at_end(parser)) {
            Token* t = current_token(parser);
            // If we see an identifier and next token is '(', treat it as method name
            if (t->type == TOKEN_IDENTIFIER) {
                // Lookahead for '('
                int la = parser->current + 1;
                // Ensure it's not a directive like @alias(...)
                int prevtok = parser->current - 1;
                bool preceded_by_at = (prevtok >= 0 && parser->tokens[prevtok].type == TOKEN_AT);
                if (!preceded_by_at && la < parser->count && parser->tokens[la].type == TOKEN_LEFT_PAREN) {
                    method_ident = t;
                    // Attempt to capture single-token return type: previous non-newline/comma identifier
                    int prev = parser->current - 1;
                    while (prev >= 0 && (parser->tokens[prev].type == TOKEN_NEWLINE || parser->tokens[prev].type == TOKEN_COMMA)) prev--;
                    if (prev >= 0 && parser->tokens[prev].type == TOKEN_IDENTIFIER) {
                        possible_return_type = &parser->tokens[prev];
                    }
                    // consume IDENT and '('
                    parser_advance(parser); // IDENT
                    parser_advance(parser); // '('
                    break;
                }
            }
            // If we hit a '{' or '}' unexpectedly, bail
            if (t->type == TOKEN_LEFT_BRACE || t->type == TOKEN_RIGHT_BRACE) break;
            parser_advance(parser);
        }

        if (!method_ident) {
            // Could not find a valid method signature; try to recover to next comma/newline/'}'
            while (!check(parser, TOKEN_RIGHT_BRACE) && !parser_is_at_end(parser)) {
                if (parser_match(parser, TOKEN_COMMA) || parser_match(parser, TOKEN_NEWLINE)) break;
                parser_advance(parser);
            }
            continue;
        }

        // Parse parameter specs inside (...)
        // We'll build an array literal of strings like "name:type|type"
        ASTNode* params_array = ast_node_new(AST_ARRAY_LITERAL);
        set_node_location(params_array, method_ident);
        params_array->as.array_literal.count = 0;
        params_array->as.array_literal.capacity = 4;
        params_array->as.array_literal.elements = malloc(sizeof(ASTNode*) * params_array->as.array_literal.capacity);

        int paren_depth = 1; // we already consumed one '('
        while (!parser_is_at_end(parser) && paren_depth > 0) {
            // End of parameter list
            if (check(parser, TOKEN_RIGHT_PAREN)) {
                parser_advance(parser);
                paren_depth--;
                break;
            }
            // Skip commas and newlines
            while (parser_match(parser, TOKEN_COMMA) || parser_match(parser, TOKEN_NEWLINE));
            if (check(parser, TOKEN_RIGHT_PAREN)) continue;

            // Expect param name
            if (!check(parser, TOKEN_IDENTIFIER)) {
                // Not a param; try to recover by skipping until comma or ')'
                while (!check(parser, TOKEN_RIGHT_PAREN) && !check(parser, TOKEN_COMMA) && !parser_is_at_end(parser)) parser_advance(parser);
                continue;
            }
            Token* pname = parser_advance(parser); // IDENT

            // Optional ':' typespec
            char typespec_buf[256];
            typespec_buf[0] = '\0';
            if (parser_match(parser, TOKEN_COLON)) {
                // Collect tokens until comma or ')'
                char* w = typespec_buf;
                size_t rem = sizeof(typespec_buf);
                while (!check(parser, TOKEN_RIGHT_PAREN) && !check(parser, TOKEN_COMMA) && !parser_is_at_end(parser)) {
                    Token* tt = parser_advance(parser);
                    if (tt->length > 0 && rem > (size_t)tt->length + 1) {
                        memcpy(w, tt->start, tt->length);
                        w += tt->length;
                        *w++ = ' ';
                        rem -= tt->length + 1;
                    } else break;
                }
                if (w != typespec_buf) { *(w-1) = '\0'; } else { *w = '\0'; }
            }

            // Build "name: types" string literal node
            ASTNode* spec_str = ast_node_new(AST_LITERAL);
            spec_str->as.literal.type = VALUE_STRING;
            size_t name_len = pname->length;
            size_t type_len = strlen(typespec_buf);
            size_t total = name_len + 2 + type_len; // name + ':' + ' ' + types
            char* s = malloc(total + 1);
            memcpy(s, pname->start, name_len);
            s[name_len] = ':';
            s[name_len+1] = ' ';
            memcpy(s + name_len + 2, typespec_buf, type_len);
            s[total] = '\0';
            spec_str->as.literal.as.string = s;

            // Append to params array
            if (params_array->as.array_literal.count >= params_array->as.array_literal.capacity) {
                int old_cap = params_array->as.array_literal.capacity;
                params_array->as.array_literal.capacity = old_cap < 8 ? 8 : old_cap * 2;
                params_array->as.array_literal.elements = realloc(params_array->as.array_literal.elements, sizeof(ASTNode*) * params_array->as.array_literal.capacity);
            }
            params_array->as.array_literal.elements[params_array->as.array_literal.count++] = spec_str;

            // If next is comma, consume and continue
            parser_match(parser, TOKEN_COMMA);
        }

        // Build: bind_interface_method("iface", "method") as an expression statement
        ASTNode* fn_ident = ast_node_new(AST_IDENTIFIER);
        set_node_location(fn_ident, method_ident);
        fn_ident->as.identifier = strdup("bind_interface_method");

        // Arg 1: interface name string literal
        ASTNode* iface_str = ast_node_new(AST_LITERAL);
        set_node_location(iface_str, name);
        iface_str->as.literal.type = VALUE_STRING;
        iface_str->as.literal.as.string = strdup(iface_name);

        // Arg 2: method name string literal
        ASTNode* method_str = ast_node_new(AST_LITERAL);
        set_node_location(method_str, method_ident);
        method_str->as.literal.type = VALUE_STRING;
        char* mname = malloc(method_ident->length + 1);
        memcpy(mname, method_ident->start, method_ident->length);
        mname[method_ident->length] = '\0';
        method_str->as.literal.as.string = mname;

        ASTNode* call = ast_node_new(AST_CALL);
        set_node_location(call, method_ident);
        call->as.call.function = fn_ident;
        // args: iface, method, params_array?, return_type?, aliasArray?
        int extra = 0;
        ASTNode* ret_str = NULL;
        if (possible_return_type) {
            ret_str = ast_node_new(AST_LITERAL);
            set_node_location(ret_str, possible_return_type);
            ret_str->as.literal.type = VALUE_STRING;
            char* rname = malloc(possible_return_type->length + 1);
            memcpy(rname, possible_return_type->start, possible_return_type->length);
            rname[possible_return_type->length] = '\0';
            ret_str->as.literal.as.string = rname;
            extra = 1;
        }
        int has_params = (params_array->as.array_literal.count > 0) ? 1 : 0;
        int has_aliases = (alias_array && alias_array->as.array_literal.count > 0) ? 1 : 0;
        
        // Add is_exported flag as last argument
        ASTNode* exported_flag = ast_node_new(AST_LITERAL);
        set_node_location(exported_flag, method_ident);
        exported_flag->as.literal.type = VALUE_BOOL;
        exported_flag->as.literal.as.boolean = is_exported;
        
        call->as.call.arg_count = 2 + has_params + extra + has_aliases + 1; // +1 for is_exported
        call->as.call.args = malloc(sizeof(ASTNode*) * call->as.call.arg_count);
        int ai = 0;
        call->as.call.args[ai++] = iface_str;
        call->as.call.args[ai++] = method_str;
        if (has_params) call->as.call.args[ai++] = params_array;
        if (extra) call->as.call.args[ai++] = ret_str;
        if (has_aliases) call->as.call.args[ai++] = alias_array;
        call->as.call.args[ai++] = exported_flag; // is_exported flag

        ASTNode* stmt = ast_node_new(AST_EXPRESSION_STMT);
        set_node_location(stmt, method_ident);
        stmt->as.expression = call;

        // Append to block
        if (block->as.block.count >= block->as.block.capacity) {
            int old_cap = block->as.block.capacity;
            block->as.block.capacity = old_cap < 8 ? 8 : old_cap * 2;
            block->as.block.statements = realloc(block->as.block.statements, sizeof(ASTNode*) * block->as.block.capacity);
        }
        block->as.block.statements[block->as.block.count++] = stmt;

        // Optional trailing commas/newlines between methods
        while (parser_match(parser, TOKEN_NEWLINE) || parser_match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after interface methods.");

    // Free iface_name ownership transferred into literal nodes; avoid leak by not freeing here
    return block;
}

// Wrapper for backward compatibility
static ASTNode* interface_declaration(Parser* parser) {
    return interface_declaration_impl(parser, false);
}

static ASTNode* method_declaration(Parser* parser) {
    Token* fn_token = previous_token(parser); // 'fn'
    
    // Expect (TypeName)
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'fn' for method declaration.");
    Token* receiver_type = consume(parser, TOKEN_IDENTIFIER, "Expect receiver type name.");
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after receiver type.");
    
    Token* method_name = consume(parser, TOKEN_IDENTIFIER, "Expect method name.");
    
    ASTNode* method_node = ast_node_new(AST_METHOD_DECL);
    set_node_location(method_node, fn_token);
    
    char* receiver_type_str = malloc(receiver_type->length + 1);
    memcpy(receiver_type_str, receiver_type->start, receiver_type->length);
    receiver_type_str[receiver_type->length] = '\0';
    method_node->as.method_decl.receiver_type = receiver_type_str;
    
    char* method_name_str = malloc(method_name->length + 1);
    memcpy(method_name_str, method_name->start, method_name->length);
    method_name_str[method_name->length] = '\0';
    method_node->as.method_decl.method_name = method_name_str;
    
    // Initialize decorators (will be set by caller from declaration())
    decorator_list_init(&method_node->as.method_decl.decorators);
    method_node->as.method_decl.param_decorators = NULL;
    
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after method name.");
    
    // Parse parameters (with optional decorators)
    method_node->as.method_decl.params = NULL;
    method_node->as.method_decl.param_count = 0;
    
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        int capacity = 4;
        method_node->as.method_decl.params = malloc(sizeof(char*) * capacity);
        method_node->as.method_decl.param_decorators = malloc(sizeof(DecoratorList) * capacity);
        
        do {
            if (method_node->as.method_decl.param_count >= capacity) {
                capacity *= 2;
                method_node->as.method_decl.params = realloc(method_node->as.method_decl.params, sizeof(char*) * capacity);
                method_node->as.method_decl.param_decorators = realloc(method_node->as.method_decl.param_decorators, sizeof(DecoratorList) * capacity);
            }
            
            // Parse parameter decorators
            DecoratorList param_decs = parse_decorators(parser);
            
            Token* param = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
            char* param_name = malloc(param->length + 1);
            memcpy(param_name, param->start, param->length);
            param_name[param->length] = '\0';
            method_node->as.method_decl.params[method_node->as.method_decl.param_count] = param_name;
            method_node->as.method_decl.param_decorators[method_node->as.method_decl.param_count] = param_decs;
            method_node->as.method_decl.param_count++;
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before method body.");
    
    // Parse the block
    ASTNode* block_node = block_statement(parser);
    method_node->as.method_decl.body = malloc(sizeof(Block));
    method_node->as.method_decl.body->statements = block_node->as.block.statements;
    method_node->as.method_decl.body->count = block_node->as.block.count;
    method_node->as.method_decl.body->capacity = block_node->as.block.capacity;
    
    return method_node;
}

// Parse decorators: @decorator_name or @decorator_name(arg1, arg2, ...)
// Returns a DecoratorList that needs to be freed by caller
static DecoratorList parse_decorators(Parser* parser) {
    DecoratorList list;
    decorator_list_init(&list);
    
    while (parser_match(parser, TOKEN_AT)) {
        Token* name_token = consume(parser, TOKEN_IDENTIFIER, "Expect decorator name after '@'.");
        char* dec_name = malloc(name_token->length + 1);
        memcpy(dec_name, name_token->start, name_token->length);
        dec_name[name_token->length] = '\0';
        
        // Check for arguments
        ASTNode** args = NULL;
        int arg_count = 0;
        
        if (parser_match(parser, TOKEN_LEFT_PAREN)) {
            // Parse decorator arguments
            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                int capacity = 4;
                args = malloc(sizeof(ASTNode*) * capacity);
                
                do {
                    if (arg_count >= capacity) {
                        capacity *= 2;
                        args = realloc(args, sizeof(ASTNode*) * capacity);
                    }
                    
                    // Parse argument (can be identifier, string, number, or expression)
                    // For named arguments like func=myFunc, parse as assignment-like expression
                    if (check(parser, TOKEN_IDENTIFIER)) {
                        Token* peek_ahead = parser->current + 1 < parser->count ? &parser->tokens[parser->current + 1] : NULL;
                        if (peek_ahead && peek_ahead->type == TOKEN_ASSIGN) {
                            // Named argument: name=value
                            Token* param_name = current_token(parser);
                            parser_advance(parser);
                            parser_advance(parser); // consume '='
                            
                            // Create an assignment node to represent name=value
                            ASTNode* assign_node = ast_node_new(AST_ASSIGNMENT);
                            set_node_location(assign_node, param_name);
                            
                            ASTNode* target = ast_node_new(AST_IDENTIFIER);
                            char* name = malloc(param_name->length + 1);
                            memcpy(name, param_name->start, param_name->length);
                            name[param_name->length] = '\0';
                            target->as.identifier = name;
                            assign_node->as.assignment.target = target;
                            assign_node->as.assignment.value = expression(parser);
                            assign_node->as.assignment.operator = TOKEN_ASSIGN;
                            
                            args[arg_count++] = assign_node;
                        } else {
                            // Regular positional argument
                            args[arg_count++] = expression(parser);
                        }
                    } else {
                        // Regular expression argument
                        args[arg_count++] = expression(parser);
                    }
                } while (parser_match(parser, TOKEN_COMMA));
            }
            consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after decorator arguments.");
        }
        
        decorator_list_add(&list, dec_name, args, arg_count);
        free(dec_name);
        if (args) free(args);  // decorator_list_add copies the array
        
        // Allow newlines between decorators
        while (parser_match(parser, TOKEN_NEWLINE));
    }
    
    return list;
}

static ASTNode* declaration(Parser* parser) {
    // Parse decorators (if any) before declarations
    DecoratorList decorators = parse_decorators(parser);
    
    if (parser_match(parser, TOKEN_STRUCT)) {
        ASTNode* struct_node = struct_declaration(parser);
        struct_node->as.struct_decl.decorators = decorators;
        return struct_node;
    }
    if (parser_match(parser, TOKEN_INTERFACE)) {
        // Interfaces don't support decorators yet, free the list
        decorator_list_free(&decorators);
        return interface_declaration(parser);
    }
    if (parser_match(parser, TOKEN_EXPORT)) {
        // export fn name() { ... }
        if (parser_match(parser, TOKEN_FN)) {
            if (check(parser, TOKEN_IDENTIFIER)) {
                ASTNode* fn_node = function_declaration(parser);
                fn_node->as.function_decl.is_exported = true;
                fn_node->as.function_decl.decorators = decorators;
                return fn_node;
            } else {
                error_at_current(parser, "export can only be used with named functions.");
                decorator_list_free(&decorators);
                return NULL;
            }
        } else if (parser_match(parser, TOKEN_INTERFACE)) {
            // export interface name { ... }
            decorator_list_free(&decorators);  // Interfaces don't support decorators
            ASTNode* iface_node = interface_declaration_impl(parser, true);
            return iface_node;
        } else {
            error_at_current(parser, "export can only be used with function or interface declarations.");
            decorator_list_free(&decorators);
            return NULL;
        }
    }
    if (parser_match(parser, TOKEN_FN)) {
        // Check if it's a method declaration fn (Type) method()
        if (check(parser, TOKEN_LEFT_PAREN)) {
            ASTNode* method_node = method_declaration(parser);
            method_node->as.method_decl.decorators = decorators;
            return method_node;
        }
        // Check if it's a function declaration (fn name(...)) or anonymous function expression (fn(...))
        if (check(parser, TOKEN_IDENTIFIER)) {
            ASTNode* fn_node = function_declaration(parser);
            fn_node->as.function_decl.decorators = decorators;
            return fn_node;
        } else {
            // It's an anonymous function expression - treat as expression statement
            decorator_list_free(&decorators);  // Can't decorate anonymous functions in expression position
            parser->current--; // Back up to re-parse the fn token
            return statement(parser);
        }
    }
    if (parser_match(parser, TOKEN_LET)) {
        decorator_list_free(&decorators);  // Variables don't support decorators
        return var_declaration(parser);
    }
    
    // No declaration keyword found, decorators are invalid
    if (decorators.count > 0) {
        error_at_current(parser, "Decorators can only be applied to functions, methods, or structs.");
        decorator_list_free(&decorators);
    }
    
    return statement(parser);
}

// Main parsing functions
void parser_init(Parser* parser, Token* tokens, int count) {
    parser->tokens = tokens;
    parser->current = 0;
    parser->count = count;
    parser->had_error = false;
    parser->panic_mode = false;
    parser->suppress_struct_literal = false;
}

ASTNode* parser_parse(Parser* parser) {
    ASTNode* program = ast_node_new(AST_BLOCK);
    program->as.block.statements = NULL;
    program->as.block.count = 0;
    program->as.block.capacity = 0;
    
    while (!parser_is_at_end(parser)) {
        // Skip newlines at top level
        if (parser_match(parser, TOKEN_NEWLINE)) continue;
        
        ASTNode* decl = declaration(parser);
        if (decl != NULL) {
            if (program->as.block.count >= program->as.block.capacity) {
                int old_capacity = program->as.block.capacity;
                program->as.block.capacity = old_capacity < 8 ? 8 : old_capacity * 2;
                program->as.block.statements = realloc(program->as.block.statements,
                    sizeof(ASTNode*) * program->as.block.capacity);
            }
            program->as.block.statements[program->as.block.count++] = decl;
            
            // Optionally consume semicolon after declaration/statement
            if (parser_match(parser, TOKEN_SEMICOLON)) {
                // Semicolon consumed, continue
            } else if (check(parser, TOKEN_NEWLINE) || parser_is_at_end(parser)) {
                // Newline or EOF is fine too
            }
        }
        
        if (parser->panic_mode) synchronize(parser);
    }
    
    return program;
}

// AST printing for debugging
void ast_print(ASTNode* node, int indent) {
    if (node == NULL) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (node->type) {
        case AST_LITERAL:
            printf("Literal: ");
            switch (node->as.literal.type) {
                case VALUE_NIL: printf("nil\n"); break;
                case VALUE_BOOL: printf("%s\n", node->as.literal.as.boolean ? "true" : "false"); break;
                case VALUE_NUMBER: printf("%g\n", node->as.literal.as.number); break;
                case VALUE_STRING: printf("\"%s\"\n", node->as.literal.as.string); break;
                case VALUE_ARRAY: printf("[Array]\n"); break;
                case VALUE_OBJECT: printf("{Object}\n"); break;
                case VALUE_FUNCTION: printf("<function>\n"); break;
            }
            break;
        case AST_IDENTIFIER:
            printf("Identifier: %s\n", node->as.identifier);
            break;
        case AST_VAR_DECL:
            printf("VarDecl: %s\n", node->as.var_decl.name);
            ast_print(node->as.var_decl.value, indent + 1);
            break;
        case AST_FUNCTION_DECL:
            printf("FunctionDecl: %s\n", node->as.function_decl.name);
            ast_print((ASTNode*)node->as.function_decl.body, indent + 1);
            break;
        case AST_BLOCK:
            printf("Block:\n");
            for (int i = 0; i < node->as.block.count; i++) {
                ast_print(node->as.block.statements[i], indent + 1);
            }
            break;
        case AST_BINARY_OP:
            printf("BinaryOp: %s\n", token_type_string(node->as.binary.operator));
            ast_print(node->as.binary.left, indent + 1);
            ast_print(node->as.binary.right, indent + 1);
            break;
        case AST_CALL:
            printf("Call:\n");
            ast_print(node->as.call.function, indent + 1);
            for (int i = 0; i < node->as.call.arg_count; i++) {
                ast_print(node->as.call.args[i], indent + 1);
            }
            break;
        default:
            printf("Unknown AST node type\n");
            break;
    }
}