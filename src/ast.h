#ifndef AST_H
#define AST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tokens.h"

// Forward declarations
typedef struct ASTNode ASTNode;
typedef struct Block Block;

typedef enum {
    // Expressions
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_MEMBER_ACCESS,
    AST_ARRAY_ACCESS,
    AST_ARRAY_LITERAL,
    AST_OBJECT_LITERAL,
    AST_INTERPOLATED_STRING,
    AST_ANONYMOUS_FUNCTION,  // Anonymous function expressions
    
    // Statements
    AST_BLOCK,
    AST_VAR_DECL,
    AST_FUNCTION_DECL,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_RETURN_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_EXPRESSION_STMT,
    AST_ASSIGNMENT
} ASTNodeType;

typedef enum {
    VALUE_NIL,
    VALUE_BOOL,
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_ARRAY,
    VALUE_OBJECT,
    VALUE_FUNCTION
} ValueType;

// Forward declaration for recursive types
struct Value;

typedef struct {
    int count;
    struct Value* values;
} ValueArray;

typedef struct {
    int count;
    char** keys;
    struct Value* values;
} ValueObject;

typedef struct {
    struct Function* function;  // Forward declaration, will be in bytecode.h
    struct Value* captured_vars;  // For closures
    int capture_count;
} FunctionValue;

typedef struct Value {
    ValueType type;
    union {
        bool boolean;
        double number;
        char* string;
        ValueArray array;
        ValueObject object;
        FunctionValue function;
    } as;
} Value;

typedef struct {
    char* key;
    ASTNode* value;
} ObjectProperty;

typedef struct {
    ObjectProperty* properties;
    int count;
    int capacity;
} ObjectLiteral;

typedef struct {
    ASTNode** elements;
    int count;
    int capacity;
} ArrayLiteral;

typedef struct {
    char* name;
    ASTNode* value;
} VarDecl;

typedef struct {
    char* name;
    char** params;
    int param_count;
    Block* body;
} FunctionDecl;

typedef struct {
    char** params;
    int param_count;
    ASTNode* body;  // Can be a block (fn() {...}) or expression (arrow function)
    bool is_arrow;  // true for arrow functions, false for fn() {}
} AnonymousFunction;

struct Block {
    ASTNode** statements;
    int count;
    int capacity;
};

typedef struct {
    ASTNode* condition;
    ASTNode* then_branch;
    ASTNode* else_branch;
} IfStmt;

typedef struct {
    ASTNode* condition;
    ASTNode* body;
} WhileStmt;

typedef struct {
    ASTNode* init;
    ASTNode* condition;
    ASTNode* update;
    ASTNode* body;
} ForStmt;

typedef struct {
    ASTNode* value;
} ReturnStmt;

typedef struct {
    ASTNode* function;
    ASTNode** args;
    int arg_count;
} CallExpr;

typedef struct {
    ASTNode* object;
    char* property;
} MemberAccess;

typedef struct {
    ASTNode* array;
    ASTNode* index;
} ArrayAccess;

typedef struct {
    KuyilTokenType operator;
    ASTNode* left;
    ASTNode* right;
} BinaryOp;

typedef struct {
    KuyilTokenType operator;
    ASTNode* operand;
} UnaryOp;

typedef struct {
    int part_count;
    char** string_parts;     // Text parts between interpolations
    ASTNode** expressions;   // Expressions inside ${}
} InterpolatedString;

typedef struct {
    ASTNode* target;
    ASTNode* value;
    KuyilTokenType operator; // =, +=, -=, etc.
} Assignment;

struct ASTNode {
    ASTNodeType type;
    int line;
    int column;
    union {
        Value literal;
        char* identifier;
        VarDecl var_decl;
        FunctionDecl function_decl;
        AnonymousFunction anonymous_function;
        Block block;
        IfStmt if_stmt;
        WhileStmt while_stmt;
        ForStmt for_stmt;
        ReturnStmt return_stmt;
        CallExpr call;
        MemberAccess member;
        ArrayAccess array_access;
        BinaryOp binary;
        UnaryOp unary;
        Assignment assignment;
        ArrayLiteral array_literal;
        ObjectLiteral object_literal;
        InterpolatedString interpolated_string;
        ASTNode* expression; // For expression statements
    } as;
};

// Parser structure
typedef struct {
    Token* tokens;
    int current;
    int count;
    bool had_error;
    bool panic_mode;
} Parser;

// Function declarations
void parser_init(Parser* parser, Token* tokens, int count);
ASTNode* parser_parse(Parser* parser);
void ast_free(ASTNode* node);
void ast_print(ASTNode* node, int indent);

// Memory management
ASTNode* ast_node_new(ASTNodeType type);
void ast_node_free(ASTNode* node);

#endif