#include "tokens.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_at_end(Lexer* lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer* lexer) {
    lexer->column++;
    return *lexer->current++;
}

static char peek(Lexer* lexer) {
    return *lexer->current;
}

static char peek_next(Lexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static bool match(Lexer* lexer, char expected) {
    if (is_at_end(lexer)) return false;
    if (*lexer->current != expected) return false;
    lexer->current++;
    lexer->column++;
    return true;
}

static Token make_token(Lexer* lexer, KuyilTokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    token.column = lexer->column - token.length;
    return token;
}

static Token error_token(Lexer* lexer, const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer->line;
    token.column = lexer->column;
    return token;
}

void print_lexical_error_with_context(const char* source, Token token) {
    // Find the start of the current line
    const char* line_start = source;
    const char* current = source;
    
    // Find the line containing the error
    int current_line = 1;
    while (current < token.start && *current) {
        if (*current == '\n') {
            current_line++;
            line_start = current + 1;
        }
        current++;
    }
    
    // Find the end of the current line
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') {
        line_end++;
    }
    
    // Calculate column position (ensure it's valid)
    int column = (int)(token.start - line_start) + 1;
    if (column < 1) column = 1;
    
    // Print error header
    fprintf(stderr, "\n❌ Lexical Error at line %d, column %d:\n", token.line, column);
    fprintf(stderr, "   %.*s\n", token.length, token.start);
    
    // Print the problematic line
    fprintf(stderr, "\n%4d | ", token.line);
    fprintf(stderr, "%.*s\n", (int)(line_end - line_start), line_start);
    
    // Print pointer to the error location
    fprintf(stderr, "     | ");
    for (int i = 1; i < column; i++) {
        fprintf(stderr, " ");
    }
    fprintf(stderr, "^ Unexpected character here\n\n");
}

static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '/':
                if (peek_next(lexer) == '/') {
                    // Line comment
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) advance(lexer);
                } else if (peek_next(lexer) == '*') {
                    // Block comment
                    advance(lexer); // consume '/'
                    advance(lexer); // consume '*'
                    while (!is_at_end(lexer)) {
                        if (peek(lexer) == '*' && peek_next(lexer) == '/') {
                            advance(lexer); // consume '*'
                            advance(lexer); // consume '/'
                            break;
                        }
                        if (peek(lexer) == '\n') {
                            lexer->line++;
                            lexer->column = 0;
                        }
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static KuyilTokenType check_keyword(int start, int length, const char* rest, KuyilTokenType type, Lexer* lexer) {
    if (lexer->current - lexer->start == start + length &&
        memcmp(lexer->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static KuyilTokenType identifier_type(Lexer* lexer) {
    switch (lexer->start[0]) {
        case 'a': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'n': return check_keyword(1, 2, "nd", TOKEN_AND, lexer);
                    case 's': return check_keyword(1, 1, "s", TOKEN_AS, lexer);
                    case 'v': return check_keyword(1, 5, "vatar", TOKEN_AVATAR, lexer);
                    case 'w': return check_keyword(1, 4, "wait", TOKEN_AWAIT, lexer);
                }
            }
            break;
        case 'b': return check_keyword(1, 4, "reak", TOKEN_BREAK, lexer);
        case 'c': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'a': return check_keyword(2, 2, "se", TOKEN_CASE, lexer);
                    case 'o': return check_keyword(1, 7, "ontinue", TOKEN_CONTINUE, lexer);
                }
            }
            break;
        case 'd': return check_keyword(1, 6, "efault", TOKEN_DEFAULT, lexer);
        case 'e': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'l': return check_keyword(1, 3, "lse", TOKEN_ELSE, lexer);
                    case 'x': return check_keyword(1, 5, "xport", TOKEN_EXPORT, lexer);
                }
            }
            break;
        case 'f': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE, lexer);
                    case 'n': return check_keyword(0, 2, "fn", TOKEN_FN, lexer);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR, lexer);
                    case 'u': 
                        if (lexer->current - lexer->start > 4) {
                            return check_keyword(0, 8, "function", TOKEN_FN, lexer);
                        }
                        return check_keyword(0, 4, "func", TOKEN_FN, lexer);
                }
            }
            break;
        case 'i': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'f': return check_keyword(0, 2, "if", TOKEN_IF, lexer);
                    case 'n': return check_keyword(1, 8, "nterface", TOKEN_INTERFACE, lexer);
                }
            }
            return check_keyword(1, 1, "f", TOKEN_IF, lexer);
        case 'l': return check_keyword(1, 2, "et", TOKEN_LET, lexer);
        case 'n': return check_keyword(1, 2, "il", TOKEN_NIL, lexer);
        case 'o': return check_keyword(1, 1, "r", TOKEN_OR, lexer);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN, lexer);
        case 's': 
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 't': return check_keyword(1, 5, "truct", TOKEN_STRUCT, lexer);
                    case 'w': return check_keyword(1, 5, "witch", TOKEN_SWITCH, lexer);
                }
            }
            break;
        case 't': return check_keyword(1, 3, "rue", TOKEN_TRUE, lexer);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE, lexer);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(Lexer* lexer) {
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer))) advance(lexer);
    return make_token(lexer, identifier_type(lexer));
}

static Token number(Lexer* lexer) {
    while (is_digit(peek(lexer))) advance(lexer);

    // Look for a fractional part.
    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
        // Consume the ".".
        advance(lexer);

        while (is_digit(peek(lexer))) advance(lexer);
    }

    return make_token(lexer, TOKEN_NUMBER);
}

static Token string(Lexer* lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\\') {
            // Handle escape sequences
            advance(lexer);  // Skip backslash
            if (!is_at_end(lexer)) {
                advance(lexer);  // Skip escaped character
            }
        } else {
            if (peek(lexer) == '\n') {
                lexer->line++;
                lexer->column = 0;
            }
            advance(lexer);
        }
    }

    if (is_at_end(lexer)) return error_token(lexer, "Unterminated string.");

    // The closing quote.
    advance(lexer);
    return make_token(lexer, TOKEN_STRING);
}

// Single-quoted string (allows double quotes inside)
static Token single_quote_string(Lexer* lexer) {
    while (peek(lexer) != '\'' && !is_at_end(lexer)) {
        if (peek(lexer) == '\\') {
            // Handle escape sequences
            advance(lexer);  // Skip backslash
            if (!is_at_end(lexer)) {
                advance(lexer);  // Skip escaped character
            }
        } else {
            if (peek(lexer) == '\n') {
                lexer->line++;
                lexer->column = 0;
            }
            advance(lexer);
        }
    }

    if (is_at_end(lexer)) return error_token(lexer, "Unterminated string.");

    // The closing quote.
    advance(lexer);
    return make_token(lexer, TOKEN_STRING);
}

static Token backtick_string(Lexer* lexer) {
    // Check if this is an interpolated string (contains ${})
    const char* scan = lexer->current;
    bool has_interpolation = false;
    
    // Quick scan to see if we have interpolation
    while (*scan != '`' && *scan != '\0') {
        if (*scan == '$' && *(scan + 1) == '{') {
            has_interpolation = true;
            break;
        }
        scan++;
    }
    
    if (!has_interpolation) {
        // Simple backtick string without interpolation
        while (peek(lexer) != '`' && !is_at_end(lexer)) {
            if (peek(lexer) == '\\') {
                // Handle escape sequences
                advance(lexer);  // Skip backslash
                if (!is_at_end(lexer)) {
                    advance(lexer);  // Skip escaped character
                }
            } else {
                if (peek(lexer) == '\n') {
                    lexer->line++;
                    lexer->column = 0;
                }
                advance(lexer);
            }
        }

        if (is_at_end(lexer)) return error_token(lexer, "Unterminated backtick string.");

        // The closing backtick.
        advance(lexer);
        return make_token(lexer, TOKEN_BACKTICK_STRING);
    } else {
        // Mark as interpolated string for parser to handle
        while (peek(lexer) != '`' && !is_at_end(lexer)) {
            if (peek(lexer) == '\n') {
                lexer->line++;
                lexer->column = 0;
            }
            advance(lexer);
        }

        if (is_at_end(lexer)) return error_token(lexer, "Unterminated interpolated string.");

        // The closing backtick.
        advance(lexer);
        return make_token(lexer, TOKEN_INTERPOLATED_STRING);
    }
}

// Helper function to parse string parts and interpolations
static Token parse_string_part(Lexer* lexer) {
    // Parse until we hit ${ or `
    while (peek(lexer) != '`' && !is_at_end(lexer)) {
        if (peek(lexer) == '$' && peek_next(lexer) == '{') {
            break;
        }
        if (peek(lexer) == '\n') {
            lexer->line++;
            lexer->column = 0;
        }
        advance(lexer);
    }
    return make_token(lexer, TOKEN_STRING_PART);
}

static Token parse_interpolation_start(Lexer* lexer) {
    // Skip the '${'
    advance(lexer); // skip $
    advance(lexer); // skip {
    return make_token(lexer, TOKEN_INTERPOLATION_START);
}

void lexer_init(Lexer* lexer, const char* source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->column = 1;
}

Token lexer_scan_token(Lexer* lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;

    if (is_at_end(lexer)) return make_token(lexer, TOKEN_EOF);

    char c = advance(lexer);

    if (is_alpha(c)) return identifier(lexer);
    if (is_digit(c)) return number(lexer);

    switch (c) {
        case '(': return make_token(lexer, TOKEN_LEFT_PAREN);
        case ')': return make_token(lexer, TOKEN_RIGHT_PAREN);
        case '{': return make_token(lexer, TOKEN_LEFT_BRACE);
        case '}': return make_token(lexer, TOKEN_RIGHT_BRACE);
        case '[': return make_token(lexer, TOKEN_LEFT_BRACKET);
        case ']': return make_token(lexer, TOKEN_RIGHT_BRACKET);
        case ';': return make_token(lexer, TOKEN_SEMICOLON);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case '.': return make_token(lexer, TOKEN_DOT);
        case ':': return make_token(lexer, TOKEN_COLON);
        case '"': return string(lexer);
        case '\'': return single_quote_string(lexer);
        case '`': return backtick_string(lexer);
        case '@': return make_token(lexer, TOKEN_AT);
        case '\n':
            lexer->line++;
            lexer->column = 1;
            return make_token(lexer, TOKEN_NEWLINE);
        case '+':
            if (match(lexer, '+')) return make_token(lexer, TOKEN_INCREMENT);
            if (match(lexer, '=')) return make_token(lexer, TOKEN_PLUS_ASSIGN);
            return make_token(lexer, TOKEN_PLUS);
        case '-':
            if (match(lexer, '-')) return make_token(lexer, TOKEN_DECREMENT);
            if (match(lexer, '=')) return make_token(lexer, TOKEN_MINUS_ASSIGN);
            if (match(lexer, '>')) return make_token(lexer, TOKEN_ARROW);
            return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_MULTIPLY);
        case '/': return make_token(lexer, TOKEN_DIVIDE);
        case '%': return make_token(lexer, TOKEN_MODULO);
        case '!':
            return make_token(lexer, match(lexer, '=') ? TOKEN_NOT_EQUAL : TOKEN_NOT);
        case '=':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_EQUAL);
            if (match(lexer, '>')) return make_token(lexer, TOKEN_ARROW);
            return make_token(lexer, TOKEN_ASSIGN);
        case '<':
            return make_token(lexer, match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return make_token(lexer, match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '&':
            if (match(lexer, '&')) return make_token(lexer, TOKEN_AND);
            break;
        case '|':
            if (match(lexer, '|')) return make_token(lexer, TOKEN_OR);
            break;
    }

    return error_token(lexer, "Unexpected character.");
}

const char* token_type_string(KuyilTokenType type) {
    switch (type) {
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_BACKTICK_STRING: return "BACKTICK_STRING";
        case TOKEN_INTERPOLATED_STRING: return "INTERPOLATED_STRING";
        case TOKEN_STRING_PART: return "STRING_PART";
        case TOKEN_INTERPOLATION_START: return "INTERPOLATION_START";
        case TOKEN_INTERPOLATION_END: return "INTERPOLATION_END";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_NIL: return "NIL";
        case TOKEN_LET: return "LET";
        case TOKEN_FN: return "FN";
        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_FOR: return "FOR";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_AS: return "AS";
        case TOKEN_AVATAR: return "AVATAR";
        case TOKEN_AWAIT: return "AWAIT";
        case TOKEN_EXPORT: return "EXPORT";
        case TOKEN_IMPORT: return "IMPORT";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_MULTIPLY: return "MULTIPLY";
        case TOKEN_DIVIDE: return "DIVIDE";
        case TOKEN_MODULO: return "MODULO";
        case TOKEN_ASSIGN: return "ASSIGN";
        case TOKEN_PLUS_ASSIGN: return "PLUS_ASSIGN";
        case TOKEN_MINUS_ASSIGN: return "MINUS_ASSIGN";
        case TOKEN_INCREMENT: return "INCREMENT";
        case TOKEN_DECREMENT: return "DECREMENT";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_NOT_EQUAL: return "NOT_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_LEFT_PAREN: return "LEFT_PAREN";
        case TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
        case TOKEN_LEFT_BRACE: return "LEFT_BRACE";
        case TOKEN_RIGHT_BRACE: return "RIGHT_BRACE";
        case TOKEN_LEFT_BRACKET: return "LEFT_BRACKET";
        case TOKEN_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_COLON: return "COLON";
        case TOKEN_ARROW: return "ARROW";
        case TOKEN_AT: return "AT";
        case TOKEN_NEWLINE: return "NEWLINE";
        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}