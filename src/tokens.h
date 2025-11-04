#ifndef KUYIL_TOKENS_H
#define KUYIL_TOKENS_H

// Note: Renamed to KuyilTokenType to avoid conflict with Windows KuyilTokenType
typedef enum {
    // Literals
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_BACKTICK_STRING,  // For multiline strings with backticks
    TOKEN_INTERPOLATED_STRING,  // For template literals with ${} syntax
    TOKEN_STRING_PART,      // Parts of interpolated string
    TOKEN_INTERPOLATION_START, // ${ token
    TOKEN_INTERPOLATION_END,   // } token in string context
    TOKEN_IDENTIFIER,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NIL,

    // Keywords
    TOKEN_LET,
    TOKEN_FN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_STRUCT,
    TOKEN_INTERFACE,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_AS,

    // Operators
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,
    TOKEN_MODULO,
    TOKEN_ASSIGN,
    TOKEN_PLUS_ASSIGN,
    TOKEN_MINUS_ASSIGN,
    TOKEN_INCREMENT,
    TOKEN_DECREMENT,

    // Comparison
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,

    // Logical
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,

    // Delimiters
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_ARROW,

    // Special
    TOKEN_AT,
    TOKEN_NEWLINE,
    TOKEN_EOF,
    TOKEN_ERROR
} KuyilTokenType;

typedef struct {
    KuyilTokenType type;
    const char* start;
    int length;
    int line;
    int column;
} Token;

typedef struct {
    const char* start;
    const char* current;
    int line;
    int column;
} Lexer;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_scan_token(Lexer* lexer);
const char* token_type_string(KuyilTokenType type);
void print_lexical_error_with_context(const char* source, Token token);

#endif