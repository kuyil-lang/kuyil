#define _POSIX_C_SOURCE 200809L
#include "vm.h"
#include "vm_library_integration.h"
// HTTP functionality now in shared libraries
#include "logging.h"
#include "tokens.h"
#include "ast.h"
#include "heapfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <unistd.h>
    #include <sys/stat.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

// Forward declarations for internal functions that might not be exposed
Function* compiler_compile(ASTNode* ast);
static void check_syntax_only(const char* path);

static void repl() {
    char line[1024];
    VM vm;
    vm_init(&vm);
    
    printf("Kuyil 1.0 - Fast scripting language with HTTP support and Logging\n");
    printf("Type 'exit' to quit.\n");
    printf("Logging commands: log_info(\"message\"), log_debug(\"message\"), etc.\n\n");
    
    for (;;) {
        printf("> ");
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        if (strcmp(line, "exit") == 0) break;
        if (strlen(line) == 0) continue;
        
        // Handle logging level commands
        if (strncmp(line, "set_log_level ", 14) == 0) {
            const char* level_str = line + 14;
            if (strcmp(level_str, "debug") == 0) {
                log_set_level(LOG_DEBUG);
                printf("Log level set to DEBUG\n");
            } else if (strcmp(level_str, "info") == 0) {
                log_set_level(LOG_INFO);
                printf("Log level set to INFO\n");
            } else if (strcmp(level_str, "warning") == 0) {
                log_set_level(LOG_WARNING);
                printf("Log level set to WARNING\n");
            } else if (strcmp(level_str, "error") == 0) {
                log_set_level(LOG_ERROR);
                printf("Log level set to ERROR\n");
            } else {
                printf("Unknown log level. Use: debug, info, warning, error\n");
            }
            continue;
        }
        
        if (strcmp(line, "show_call_stack") == 0) {
            log_print_call_stack();
            continue;
        }
        
        vm_interpret(&vm, line);
    }
    
    vm_free(&vm);
}

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    
    char* buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    
    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytes_read] = '\0';
    
    fclose(file);
    
    // Skip shebang line if present (#!/bin/kuyil or similar)
    char* source_start = buffer;
    if (bytes_read >= 2 && buffer[0] == '#' && buffer[1] == '!') {
        // Find the end of the first line
        while (*source_start != '\0' && *source_start != '\n') {
            source_start++;
        }
        if (*source_start == '\n') {
            source_start++; // Skip the newline
        }
    }
    
    // If we skipped the shebang, copy the rest to the beginning
    if (source_start != buffer) {
        size_t remaining = strlen(source_start);
        memmove(buffer, source_start, remaining + 1);
    }
    
    return buffer;
}

static void run_file(const char* path) {
    VM vm;
    vm_init(&vm);
    // Record source path for better error reporting (file:line in stack traces)
    vm.current_source_path = path;
    set_current_source_path(path);
    
    InterpretResult result;
    
    // Check file extension to determine if it's bytecode or source
    const char* ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".kyc") == 0) {
        // Execute bytecode file directly
        result = vm_interpret_bytecode(&vm, path);
    } else {
        // Execute source file
        char* source = read_file(path);
        result = vm_interpret(&vm, source);
        free(source);
    }
    
    vm_free(&vm);
    
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

typedef enum {
    COMPILE_WRAPPER,    // Shell script wrapper (default)
    COMPILE_BYTECODE,   // Bytecode binary with embedded runtime
    COMPILE_C_SOURCE,   // Generate C source code
    COMPILE_STANDALONE  // Self-contained executable
} CompileMode;

static void compile_to_wrapper(const char* input_path, const char* output_path, const char* source) {
    FILE* output = fopen(output_path, "w");
    if (output == NULL) {
        fprintf(stderr, "Could not create output file \"%s\".\n", output_path);
        exit(74);
    }
    
    fprintf(output, "#!/bin/bash\n");
    fprintf(output, "# Kuyil compiled binary (wrapper mode)\n");
    fprintf(output, "# Original source: %s\n\n", input_path);
    fprintf(output, "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n");
    fprintf(output, "cat << 'EOF' | \"$SCRIPT_DIR/kuyil\" -\n");
    fprintf(output, "%s", source);
    fprintf(output, "\nEOF\n");
    
    fclose(output);
    
    // Make executable
    char chmod_cmd[512];
    snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x %s", output_path);
    system(chmod_cmd);
}

static void compile_to_bytecode(const char* input_path, const char* output_path, const char* source) {
    // Compile source to bytecode
    VM vm;
    vm_init(&vm);
    
    // Tokenize with dynamic buffer (removes 1K token limit)
    Lexer lexer;
    lexer_init(&lexer, source);
    
    int capacity = 2048;
    int token_count = 0;
    Token* tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) {
        fprintf(stderr, "Out of memory while tokenizing %s\n", input_path);
        free(source);
        vm_free(&vm);
        exit(1);
    }
    
    for (;;) {
        Token token = lexer_scan_token(&lexer);
        if (token_count >= capacity) {
            int new_capacity = capacity * 2;
            Token* grown = (Token*)realloc(tokens, sizeof(Token) * new_capacity);
            if (!grown) {
                fprintf(stderr, "Error: Script too large (exceeds %d tokens) while compiling %s\n", capacity, input_path);
                free(tokens);
                free(source);
                vm_free(&vm);
                exit(65);
            }
            tokens = grown;
            capacity = new_capacity;
        }
        tokens[token_count++] = token;
        
        if (token.type == TOKEN_ERROR) {
            print_lexical_error_with_context(source, token);
            free(tokens);
            free(source);
            vm_free(&vm);
            exit(1);
        }
        
        if (token.type == TOKEN_EOF) break;
    }
    
    // Parse
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode* ast = parser_parse(&parser);
    
    if (parser.had_error) {
        ast_node_free(ast);
        free(tokens);
        vm_free(&vm);
        exit(65);
    }
    
    // Compile to bytecode
    Function* function = compiler_compile(ast);
    ast_node_free(ast);
    free(tokens);
    
    if (function == NULL) {
        vm_free(&vm);
        exit(65);
    }
    
    // Write bytecode to binary file
    FILE* output = fopen(output_path, "wb");
    if (output == NULL) {
        fprintf(stderr, "Could not create bytecode file \"%s\".\n", output_path);
        exit(74);
    }
    
    // Write magic header
    uint32_t magic = 0x4B59494C; // "KYIL" in hex
    fwrite(&magic, sizeof(uint32_t), 1, output);
    
    // Write bytecode size
    uint32_t code_size = function->chunk.count;
    fwrite(&code_size, sizeof(uint32_t), 1, output);
    
    // Write bytecode
    fwrite(function->chunk.code, sizeof(uint8_t), code_size, output);
    
    // Write constants count
    uint32_t const_count = function->chunk.constant_count;
    fwrite(&const_count, sizeof(uint32_t), 1, output);
    
    // Write constants
    for (int i = 0; i < const_count; i++) {
        Value value = function->chunk.constants[i];
        fwrite(&value.type, sizeof(ValueType), 1, output);
        
        switch (value.type) {
            case VALUE_NUMBER:
                fwrite(&value.as.number, sizeof(double), 1, output);
                break;
            case VALUE_STRING: {
                uint32_t len = strlen(value.as.string);
                fwrite(&len, sizeof(uint32_t), 1, output);
                fwrite(value.as.string, sizeof(char), len, output);
                break;
            }
            case VALUE_BOOL:
                fwrite(&value.as.boolean, sizeof(bool), 1, output);
                break;
            case VALUE_NIL:
                break; // No data to write
            default:
                break;
        }
    }
    
    fclose(output);
    
    // Clean up
    chunk_free(&function->chunk);
    free(function->name);
    free(function);
    vm_free(&vm);
}

static void compile_to_c_source(const char* input_path, const char* output_path, const char* source) {
    FILE* output = fopen(output_path, "w");
    if (output == NULL) {
        fprintf(stderr, "Could not create C source file \"%s\".\n", output_path);
        exit(74);
    }
    
    fprintf(output, "// Generated C code from Kuyil source: %s\n", input_path);
    fprintf(output, "#include <stdio.h>\n");
    fprintf(output, "#include <stdlib.h>\n");
    fprintf(output, "#include <string.h>\n");
    fprintf(output, "#include <stdbool.h>\n\n");
    
    fprintf(output, "// Minimal Kuyil runtime for generated C code\n");
    fprintf(output, "void kuyil_print(const char* str) { printf(\"%%s\\n\", str); }\n");
    fprintf(output, "char* str_upper(const char* str) {\n");
    fprintf(output, "    int len = strlen(str);\n");
    fprintf(output, "    char* result = malloc(len + 1);\n");
    fprintf(output, "    for (int i = 0; i < len; i++) {\n");
    fprintf(output, "        result[i] = (str[i] >= 'a' && str[i] <= 'z') ? str[i] - 32 : str[i];\n");
    fprintf(output, "    }\n");
    fprintf(output, "    result[len] = '\\0';\n");
    fprintf(output, "    return result;\n");
    fprintf(output, "}\n");
    fprintf(output, "double to_number(const char* str) { return atof(str); }\n\n");
    
    fprintf(output, "int main() {\n");
    fprintf(output, "    // Generated from Kuyil source\n");
    
    // Simple source-to-C translation (basic implementation)
    char* src_copy = strdup(source);
    char* line = strtok(src_copy, "\n");
    
    while (line != NULL) {
        // Skip empty lines and comments
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0' || *line == '#') {
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Convert print statements
        if (strncmp(line, "print(", 6) == 0) {
            fprintf(output, "    kuyil_print(%s;\n", line + 6);
        }
        // Convert variable declarations (simplified)
        else if (strncmp(line, "let ", 4) == 0) {
            char* var_part = line + 4;
            char* equals = strchr(var_part, '=');
            if (equals) {
                *equals = '\0';
                char* var_name = var_part;
                char* value = equals + 1;
                
                // Trim whitespace
                while (*var_name == ' ') var_name++;
                while (*value == ' ') value++;
                
                if (*value == '"') {
                    fprintf(output, "    char* %s = %s;\n", var_name, value);
                } else {
                    fprintf(output, "    double %s = %s;\n", var_name, value);
                }
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    fprintf(output, "    return 0;\n");
    fprintf(output, "}\n");
    
    free(src_copy);
    fclose(output);
}

static void compile_to_native_binary(const char* input_path, const char* output_path, const char* source, bool embed_bytecode, const char* target_platform) {
    // Create a temporary C file that will be compiled to native binary
    char temp_c_file[512];
    snprintf(temp_c_file, sizeof(temp_c_file), "%s_temp.c", output_path);
    
    FILE* output = fopen(temp_c_file, "w");
    if (output == NULL) {
        fprintf(stderr, "Could not create temporary C file \"%s\".\n", temp_c_file);
        exit(74);
    }
    
    // Write the native binary template with embedded content
    fprintf(output, "#define _POSIX_C_SOURCE 200809L\n");
    fprintf(output, "#include <stdio.h>\n");
    fprintf(output, "#include <stdlib.h>\n");
    fprintf(output, "#include <string.h>\n");
    fprintf(output, "#include <unistd.h>\n");
    fprintf(output, "#include <sys/stat.h>\n\n");
    
    if (embed_bytecode) {
        // Compile source to bytecode first, then embed it
        // Create temporary bytecode file
        char temp_kyc_file[512];
        snprintf(temp_kyc_file, sizeof(temp_kyc_file), "%s_temp.kyc", output_path);
        
        // Compile to bytecode first
        compile_to_bytecode(input_path, temp_kyc_file, source);
        
        // Read the bytecode file
        FILE* kyc_file = fopen(temp_kyc_file, "rb");
        if (kyc_file == NULL) {
            fprintf(stderr, "Could not read temporary bytecode file \"%s\".\n", temp_kyc_file);
            fclose(output);
            exit(74);
        }
        
        // Get bytecode size
        fseek(kyc_file, 0, SEEK_END);
        long bytecode_size = ftell(kyc_file);
        fseek(kyc_file, 0, SEEK_SET);
        
        // Write bytecode as hex array
        fprintf(output, "// Embedded bytecode (compiled from %s)\n", input_path);
        fprintf(output, "static const unsigned char EMBEDDED_BYTECODE[] = {\n");
        
        unsigned char byte;
        int count = 0;
        while (fread(&byte, 1, 1, kyc_file) == 1) {
            if (count % 16 == 0) fprintf(output, "    ");
            fprintf(output, "0x%02x", byte);
            count++;
            if (count < bytecode_size) fprintf(output, ",");
            if (count % 16 == 0 || count == bytecode_size) fprintf(output, "\n");
            else fprintf(output, " ");
        }
        
        fprintf(output, "};\n");
        fprintf(output, "static const size_t EMBEDDED_BYTECODE_SIZE = %ld;\n\n", bytecode_size);
        
        fclose(kyc_file);
        unlink(temp_kyc_file); // Clean up temp bytecode file
        
        // Write main function that runs bytecode with embedded VM
        fprintf(output, "// Embedded Kuyil VM headers\n");
        fprintf(output, "#include \"vm.h\"\n");
        fprintf(output, "#include \"http.h\"\n");
        fprintf(output, "#include \"logging.h\"\n\n");
        
        // Include the read_file function
        fprintf(output, "// File reading function\n");
        fprintf(output, "static char* read_file(const char* path) {\n");
        fprintf(output, "    FILE* file = fopen(path, \"rb\");\n");
        fprintf(output, "    if (file == NULL) {\n");
        fprintf(output, "        fprintf(stderr, \"Could not open file \\\"%%s\\\".\\n\", path);\n");
        fprintf(output, "        exit(74);\n");
        fprintf(output, "    }\n");
        fprintf(output, "    \n");
        fprintf(output, "    fseek(file, 0L, SEEK_END);\n");
        fprintf(output, "    size_t file_size = ftell(file);\n");
        fprintf(output, "    rewind(file);\n");
        fprintf(output, "    \n");
        fprintf(output, "    char* buffer = malloc(file_size + 1);\n");
        fprintf(output, "    if (buffer == NULL) {\n");
        fprintf(output, "        fprintf(stderr, \"Not enough memory to read \\\"%%s\\\".\\n\", path);\n");
        fprintf(output, "        exit(74);\n");
        fprintf(output, "    }\n");
        fprintf(output, "    \n");
        fprintf(output, "    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);\n");
        fprintf(output, "    if (bytes_read < file_size) {\n");
        fprintf(output, "        fprintf(stderr, \"Could not read file \\\"%%s\\\".\\n\", path);\n");
        fprintf(output, "        exit(74);\n");
        fprintf(output, "    }\n");
        fprintf(output, "    \n");
        fprintf(output, "    buffer[bytes_read] = '\\0';\n");
        fprintf(output, "    \n");
        fprintf(output, "    fclose(file);\n");
        fprintf(output, "    return buffer;\n");
        fprintf(output, "}\n\n");
        
        fprintf(output, "int main(int argc, char* argv[]) {\n");
        fprintf(output, "    (void)argc; (void)argv; // Suppress unused parameter warnings\n\n");
        fprintf(output, "    // Initialize systems\n");
        fprintf(output, "    log_init(LOG_INFO);\n");
        fprintf(output, "    // HTTP functionality loaded via shared libraries\n\n");
        fprintf(output, "    // Write bytecode to temporary file\n");
        fprintf(output, "    char temp_file[] = \"/tmp/kuyil_embedded_XXXXXX\";\n");
        fprintf(output, "    int fd = mkstemp(temp_file);\n");
        fprintf(output, "    if (fd == -1) {\n");
        fprintf(output, "        fprintf(stderr, \"Error: Cannot create temporary file\\n\");\n");
        fprintf(output, "        return 1;\n");
        fprintf(output, "    }\n\n");
        fprintf(output, "    // Write embedded bytecode\n");
        fprintf(output, "    ssize_t written = write(fd, EMBEDDED_BYTECODE, EMBEDDED_BYTECODE_SIZE);\n");
        fprintf(output, "    close(fd);\n");
        fprintf(output, "    if (written != (ssize_t)EMBEDDED_BYTECODE_SIZE) {\n");
        fprintf(output, "        fprintf(stderr, \"Error: Failed to write bytecode\\n\");\n");
        fprintf(output, "        unlink(temp_file);\n");
        fprintf(output, "        return 1;\n");
        fprintf(output, "    }\n\n");
        fprintf(output, "    // Initialize VM\n");
        fprintf(output, "    VM vm;\n");
        fprintf(output, "    vm_init(&vm);\n\n");
        fprintf(output, "    // Execute embedded bytecode directly using vm_interpret_bytecode\n");
        fprintf(output, "    InterpretResult result = vm_interpret_bytecode(&vm, temp_file);\n\n");
        fprintf(output, "    // Cleanup\n");
        fprintf(output, "    vm_free(&vm);\n");
        fprintf(output, "    unlink(temp_file);\n");
        fprintf(output, "    // HTTP cleanup handled by shared libraries\n");
        fprintf(output, "    log_cleanup();\n\n");
        fprintf(output, "    return (result == INTERPRET_OK) ? 0 : 1;\n");
        fprintf(output, "}\n");
        
    } else {
        // Embed source code (less secure but simpler)
        fprintf(output, "// Embedded source code (from %s)\n", input_path);
        fprintf(output, "static const char* EMBEDDED_SOURCE = \n");
        
        // Write source as escaped string literal
        const char* ptr = source;
        fprintf(output, "\"");
        while (*ptr) {
            if (*ptr == '"') fprintf(output, "\\\"");
            else if (*ptr == '\\') fprintf(output, "\\\\");
            else if (*ptr == '\n') fprintf(output, "\\n\"\n\"");
            else if (*ptr == '\t') fprintf(output, "\\t");
            else if (*ptr == '\r') fprintf(output, "\\r");
            else fprintf(output, "%c", *ptr);
            ptr++;
        }
        fprintf(output, "\";\n\n");
        
        // Get the current kuyil binary path (cross-platform)
        char kuyil_path[1024] = "kuyil"; // Default fallback
#ifdef _WIN32
        if (GetModuleFileNameA(NULL, kuyil_path, sizeof(kuyil_path)) == 0) {
            strcpy(kuyil_path, "kuyil.exe");
        }
#elif defined(__APPLE__)
        uint32_t size = sizeof(kuyil_path);
        if (_NSGetExecutablePath(kuyil_path, &size) != 0) {
            strcpy(kuyil_path, "kuyil");
        }
#else // Linux
        ssize_t len = readlink("/proc/self/exe", kuyil_path, sizeof(kuyil_path) - 1);
        if (len == -1) {
            strcpy(kuyil_path, "kuyil");
        } else {
            kuyil_path[len] = '\0';
        }
#endif
        
        // Write main function that runs source with embedded VM
        fprintf(output, "// Embedded Kuyil VM headers\n");
        fprintf(output, "#include \"vm.h\"\n");
        fprintf(output, "#include \"logging.h\"\n");
        fprintf(output, "#include \"heapfs.h\"\n\n");
        
        fprintf(output, "int main(int argc, char* argv[]) {\n");
        fprintf(output, "    (void)argc; (void)argv; // Suppress unused parameter warnings\n\n");
        fprintf(output, "    // Initialize systems\n");
        fprintf(output, "    log_init(LOG_INFO);\n");
        fprintf(output, "    // HTTP functionality loaded via shared libraries\n\n");
        fprintf(output, "    // Initialize embedded resources (if available)\n");
        fprintf(output, "    extern void heapfs_init_embedded_resources(HeapFS* fs) __attribute__((weak));\n");
        fprintf(output, "    extern HeapFS g_heapfs;\n");
        fprintf(output, "    if (heapfs_init_embedded_resources) {\n");
        fprintf(output, "        heapfs_init_embedded_resources(&g_heapfs);\n");
        fprintf(output, "    }\n\n");
        fprintf(output, "    // Initialize VM\n");
        fprintf(output, "    VM vm;\n");
        fprintf(output, "    vm_init(&vm);\n\n");
        fprintf(output, "    // Execute embedded source directly\n");
        fprintf(output, "    InterpretResult result = vm_interpret(&vm, EMBEDDED_SOURCE);\n\n");
        fprintf(output, "    // Cleanup\n");
        fprintf(output, "    vm_free(&vm);\n");
        fprintf(output, "    // HTTP cleanup handled by shared libraries\n");
        fprintf(output, "    log_cleanup();\n\n");
        fprintf(output, "    return (result == INTERPRET_OK) ? 0 : 1;\n");
        fprintf(output, "}\n");
    }
    
    fclose(output);
    
    // Now compile the C file to native binary with embedded VM
    char compile_command[4096];
    
    // Get the directory where kuyil binary is located (cross-platform)
    char kuyil_dir[1024] = "."; // Default to current directory
    
#ifdef _WIN32
    char exe_path[1024];
    if (GetModuleFileNameA(NULL, exe_path, sizeof(exe_path)) > 0) {
        strcpy(kuyil_dir, exe_path);
        char* last_slash = strrchr(kuyil_dir, '\\');
        if (!last_slash) last_slash = strrchr(kuyil_dir, '/');
        if (last_slash) *last_slash = '\0';
    }
#elif defined(__APPLE__)
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        realpath(exe_path, kuyil_dir);
        char* last_slash = strrchr(kuyil_dir, '/');
        if (last_slash) *last_slash = '\0';
    }
#else // Linux
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* resolved = realpath(exe_path, NULL);
        if (resolved) {
            strcpy(kuyil_dir, resolved);
            char* last_slash = strrchr(kuyil_dir, '/');
            if (last_slash) *last_slash = '\0';
            free(resolved);
        }
    }
#endif
    
    // Determine compiler and flags based on target platform
    const char* compiler;
    const char* exe_ext;
    const char* obj_ext;
    const char* libs;
    const char* extra_flags = "";
    
    // If no target specified, use current platform
    if (!target_platform) {
#ifdef _WIN32
        target_platform = "windows";
#elif defined(__APPLE__)
        target_platform = "macos";
#else
        target_platform = "linux";
#endif
    }
    
    // Cross-compilation setup
    if (strcmp(target_platform, "windows") == 0) {
        // Target: Windows
#ifdef _WIN32
        compiler = "gcc"; // Native Windows
#else
        compiler = "x86_64-w64-mingw32-gcc"; // Cross-compile from Linux/Mac
#endif
        exe_ext = ".exe";
        obj_ext = ".obj";
        libs = "-lws2_32 -lwinhttp -lpthread -lm -lpsapi";
        extra_flags = "-DWIN32 -D_WIN32_WINNT=0x0600";
    } else if (strcmp(target_platform, "macos") == 0) {
        // Target: macOS
#ifdef __APPLE__
        compiler = "clang"; // Native macOS
#else
        compiler = "x86_64-apple-darwin20.4-clang"; // Cross-compile (requires OSXCross)
#endif
        exe_ext = "";
        obj_ext = ".o";
        libs = "-lcurl -lpthread -lm -ldl";
        extra_flags = "-mmacosx-version-min=10.13";
    } else if (strcmp(target_platform, "linux") == 0) {
        // Target: Linux
        compiler = "gcc";
        exe_ext = "";
        obj_ext = ".o";
        libs = "-lcurl -lpthread -lm -ldl";
    } else {
        fprintf(stderr, "Error: Unknown target platform '%s'. Use: windows, linux, or macos\n", target_platform);
        exit(1);
    }
    
    // Both bytecode and source versions now link with the entire Kuyil VM for true self-containment
    // Use dynamic linking but embed the library paths
    snprintf(compile_command, sizeof(compile_command), 
             "%s -O2 -s %s %s %s/src/vm.c %s/src/logging.c %s/src/config.c "
             "%s/src/ffi.c %s/src/file_reader.c %s/src/green_threads.c "
             "%s/src/library_loader.c %s/src/vm_library_integration.c "
             "%s/src/vm_task_queue.c %s/src/async_http.c %s/src/thread_pool.c "
             "%s/src/avatar_runtime.c %s/src/async_request_queue.c %s/src/opcode_executor.c "
             "%s/src/heapfs.c %s/src/embedded_resources.c "
             "-I%s/src "
             "-L%s/libs -Wl,-rpath,%s/libs "
             "-lkylstr -lkylmath -lkylfileio -lkyldatetime -lkylasyncio -lkyleventloop -lkylwebview -lkylhttp "
             "-o %s%s %s -levent "
             "-DEMBEDDED_BINARY -DENABLE_EMBEDDED_RESOURCES",
             compiler, extra_flags, temp_c_file, 
             kuyil_dir, kuyil_dir, kuyil_dir, kuyil_dir,
             kuyil_dir, kuyil_dir, kuyil_dir, kuyil_dir, 
             kuyil_dir, kuyil_dir, kuyil_dir,
             kuyil_dir, kuyil_dir, kuyil_dir, 
             kuyil_dir, kuyil_dir,
             kuyil_dir,
             kuyil_dir, kuyil_dir,
             output_path, exe_ext, libs);
    
    printf("Compiling native binary for %s: %s\n", target_platform, compile_command);
    int result = system(compile_command);
    
    // Clean up temporary C file
    unlink(temp_c_file);
    
    if (result == 0) {
        // Make executable
        char chmod_command[1024];
        snprintf(chmod_command, sizeof(chmod_command), "chmod +x %s", output_path);
        system(chmod_command);
        
        if (embed_bytecode) {
            printf("✅ Created native binary '%s' with embedded bytecode (secure)\n", output_path);
        } else {
            printf("✅ Created native binary '%s' with embedded source (readable)\n", output_path);
        }
    } else {
        fprintf(stderr, "❌ Failed to compile native binary\n");
        exit(1);
    }
}

static void compile_file_with_options(const char* input_path, const char* output_path, bool native_mode, bool embed_bytecode, const char* target_platform) {
    char* source = read_file(input_path);
    
    if (native_mode) {
        compile_to_native_binary(input_path, output_path, source, embed_bytecode, target_platform);
    } else {
        // Use existing compile_file logic
        CompileMode mode = COMPILE_WRAPPER;
        
        // Check output file extension to determine compilation mode
        const char* ext = strrchr(output_path, '.');
        if (ext) {
            if (strcmp(ext, ".kyc") == 0) {
                mode = COMPILE_BYTECODE;
            } else if (strcmp(ext, ".c") == 0) {
                mode = COMPILE_C_SOURCE;
            }
        }
        
        switch (mode) {
            case COMPILE_WRAPPER:
                compile_to_wrapper(input_path, output_path, source);
                printf("Compiled '%s' to executable wrapper '%s'\n", input_path, output_path);
                break;
                
            case COMPILE_BYTECODE:
                compile_to_bytecode(input_path, output_path, source);
                printf("Compiled '%s' to bytecode '%s'\n", input_path, output_path);
                break;
                
            case COMPILE_C_SOURCE:
                compile_to_c_source(input_path, output_path, source);
                printf("Compiled '%s' to C source '%s'\n", input_path, output_path);
                printf("Compile with: gcc -o executable %s\n", output_path);
                break;
                
            case COMPILE_STANDALONE:
                fprintf(stderr, "Use --native flag for standalone compilation\n");
                exit(1);
                break;
        }
    }
    
    free(source);
}

static void compile_file(const char* input_path, const char* output_path) {
    char* source = read_file(input_path);
    
    // Default to wrapper mode for backward compatibility
    CompileMode mode = COMPILE_WRAPPER;
    
    // Check output file extension to determine compilation mode
    const char* ext = strrchr(output_path, '.');
    if (ext) {
        if (strcmp(ext, ".kyc") == 0) {
            mode = COMPILE_BYTECODE;
        } else if (strcmp(ext, ".c") == 0) {
            mode = COMPILE_C_SOURCE;
        }
    }
    
    switch (mode) {
        case COMPILE_WRAPPER:
            compile_to_wrapper(input_path, output_path, source);
            printf("Compiled '%s' to executable wrapper '%s'\n", input_path, output_path);
            break;
            
        case COMPILE_BYTECODE:
            compile_to_bytecode(input_path, output_path, source);
            printf("Compiled '%s' to bytecode '%s'\n", input_path, output_path);
            break;
            
        case COMPILE_C_SOURCE:
            compile_to_c_source(input_path, output_path, source);
            printf("Compiled '%s' to C source '%s'\n", input_path, output_path);
            printf("Compile with: gcc -o executable %s\n", output_path);
            break;
            
        case COMPILE_STANDALONE:
            // TODO: Implement standalone executable generation
            fprintf(stderr, "Standalone compilation not yet implemented\n");
            exit(1);
            break;
    }
    
    free(source);
}

static void print_usage() {
    printf("Usage: kuyil [options] [script]\n\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -d, --debug          Enable debug logging\n");
    printf("  -c, --compile        Compile script to binary\n");
    printf("  --native             Generate native executable (requires -c)\n");
    printf("  --embed-bytecode     Embed bytecode instead of source (more secure)\n");
    printf("  --target <platform>  Cross-compile target: windows, linux, macos\n");
    printf("  -o <output>          Specify output file for compilation\n");
    printf("                       Extensions: .kyc (bytecode), .c (C source), other (wrapper)\n");
    printf("  -v, --version        Show version information\n");
    printf("  --log-level <level>  Set logging level (debug|info|warning|error|fatal)\n");
    printf("  --log-file <file>    Log to file instead of stderr\n");
    printf("  --no-color           Disable colored log output\n");
    printf("  --no-trace           Disable function call tracing\n");
    printf("  --test               Enable test mode (assertions and FFI mocks)\n");
    printf("  --coverage           Enable code coverage instrumentation\n");
    printf("  --coverage-format <text|lcov>  Set coverage report format (default: text)\n");
    printf("  -                    Read from stdin\n\n");
    printf("Examples:\n");
    printf("  kuyil script.kyl                             Run script.kyl\n");
    printf("  kuyil -d script.kyl                          Run with debug logging\n");
    printf("  kuyil --log-level debug script.kyl           Run with debug logging\n");
    printf("  kuyil --log-file app.log script.kyl          Log to file\n");
    printf("  kuyil --test tests/sample.kyl                Run tests with assertions\n");
    printf("  kuyil --coverage --coverage-format lcov script.kyl > coverage.info\n");
    printf("  kuyil                                        Start interactive REPL\n");
    printf("  kuyil -c script.kyl -o app                   Compile to wrapper binary\n");
    printf("  kuyil -c script.kyl -o app.kyc               Compile to bytecode\n");
    printf("  kuyil -c script.kyl -o app.c                 Compile to C source\n");
    printf("  kuyil --native script.kyl -o myapp           Generate native binary\n");
    printf("  kuyil --native --embed-bytecode script.kyl   Secure native binary with bytecode\n");
    printf("  kuyil --native --target windows script.kyl   Cross-compile to Windows .exe\n");
    printf("  kuyil --native --target macos script.kyl     Cross-compile to macOS binary\n");
    printf("  echo 'print(\"Hi\")' | kuyil -                Run from stdin\n\n");
    printf("Cross-Compilation:\n");
    printf("  --target windows     Requires: mingw-w64 (sudo apt-get install mingw-w64)\n");
    printf("  --target macos       Requires: OSXCross (complex setup, use native Mac or CI)\n");
    printf("  --target linux       Default on Linux systems\n\n");
    printf("Logging in Kuyil:\n");
    printf("  log_fatal(\"message\")   - Fatal error (exits program)\n");
    printf("  log_error(\"message\")   - Error message\n");
    printf("  log_warning(\"message\") - Warning message\n");
    printf("  log_info(\"message\")    - Informational message\n");
    printf("  log_debug(\"message\")   - Debug message\n");
}

static void print_version() {
    printf("Kuyil 1.0.0\n");
    printf("Fast scripting language with built-in HTTP/REST support\n");
    printf("Copyright (c) 2025 Kuyil Contributors\n");
}

// Semantic analysis function - checks function calls and argument counts
static bool check_semantic_errors(ASTNode* node) {
    if (node == NULL) return false;
    
    bool has_errors = false;
    
    switch (node->type) {
        case AST_CALL: {
            // Check function calls - first check if function is an identifier
            if (node->as.call.function->type == AST_IDENTIFIER) {
                char* function_name = node->as.call.function->as.identifier;
                int arg_count = node->as.call.arg_count;
                
                // Known built-in functions and their expected argument counts
                struct {
                    const char* name;
                    int min_args;
                    int max_args;
                } builtins[] = {
                    {"print", 1, 1},
                    {"typeof", 1, 1},
                    {"log_info", 1, 1},
                    {"log_debug", 1, 1}, 
                    {"log_warning", 1, 1},
                    {"log_error", 1, 1},
                    {"log_fatal", 1, 1},
                    {"get_library_count", 0, 0},
                    {"get_library_name", 1, 1},
                    {"get_library_path", 1, 1},
                    {"is_library_loaded", 1, 1},
                    {"get_library_function_count", 1, 1},
                    {"add_library", 3, 3},
                    {"load_library", 1, 1},
                    {"clear_libraries", 0, 0},
                    {"import", 1, 1},
                    {"export_function", 2, 2},
                    // Configuration functions
                    {"init_config", 2, 2},
                    {"get_config", 1, 2},  // Can have 1 or 2 args (with/without default)
                    {"set_config", 2, 2},
                    {"reload_config", 0, 0},
                    // String functions
                    {"len", 1, 1},
                    {"str", 1, 1},
                    {"substr", 3, 3},
                    {"replace", 3, 3},
                    {"split", 2, 2},
                    {"join", 2, 2},
                    {"upper", 1, 1},
                    {"lower", 1, 1},
                    {"trim", 1, 1},
                    // Type conversion functions
                    {"int", 1, 1},
                    {"float", 1, 1},
                    {"bool", 1, 1},
                    {"type", 1, 1},
                    // HTTP functions
                    {"http_get", 1, 1},
                    {"http_post", 2, 2},
                    {"http_put", 2, 2},
                    {"http_delete", 1, 1},
                    {"http_server", 3, 3},
                    {"http_start_server", 3, 3},
                    {"http_json", 2, 2},
                    {"http_response", 2, 2},
                    // File functions
                    {"read_file", 1, 1},
                    {"write_file", 2, 2},
                    {"file_exists", 1, 1},
                    {NULL, 0, 0}
                };
                
                // Check if it's a known builtin
                for (int i = 0; builtins[i].name != NULL; i++) {
                    if (strcmp(function_name, builtins[i].name) == 0) {
                        if (arg_count < builtins[i].min_args || arg_count > builtins[i].max_args) {
                            if (builtins[i].min_args == builtins[i].max_args) {
                                fprintf(stderr, "Error: Function '%s' expects %d arguments, got %d\n", 
                                       function_name, builtins[i].min_args, arg_count);
                            } else {
                                fprintf(stderr, "Error: Function '%s' expects %d-%d arguments, got %d\n", 
                                       function_name, builtins[i].min_args, builtins[i].max_args, arg_count);
                            }
                            has_errors = true;
                        }
                        break;
                    }
                }
            }
            
            // Recursively check function expression and arguments
            if (check_semantic_errors(node->as.call.function)) has_errors = true;
            for (int i = 0; i < node->as.call.arg_count; i++) {
                if (check_semantic_errors(node->as.call.args[i])) {
                    has_errors = true;
                }
            }
            break;
        }
        
        case AST_BLOCK: {
            for (int i = 0; i < node->as.block.count; i++) {
                if (check_semantic_errors(node->as.block.statements[i])) {
                    has_errors = true;
                }
            }
            break;
        }
        
        case AST_IF_STMT: {
            if (check_semantic_errors(node->as.if_stmt.condition)) has_errors = true;
            if (check_semantic_errors(node->as.if_stmt.then_branch)) has_errors = true;
            if (node->as.if_stmt.else_branch && check_semantic_errors(node->as.if_stmt.else_branch)) {
                has_errors = true;
            }
            break;
        }
        
        case AST_WHILE_STMT: {
            if (check_semantic_errors(node->as.while_stmt.condition)) has_errors = true;
            if (check_semantic_errors(node->as.while_stmt.body)) has_errors = true;
            break;
        }
        
        case AST_FOR_STMT: {
            if (node->as.for_stmt.init && check_semantic_errors(node->as.for_stmt.init)) has_errors = true;
            if (node->as.for_stmt.condition && check_semantic_errors(node->as.for_stmt.condition)) has_errors = true;
            if (node->as.for_stmt.update && check_semantic_errors(node->as.for_stmt.update)) has_errors = true;
            if (check_semantic_errors(node->as.for_stmt.body)) has_errors = true;
            break;
        }
        
        case AST_BINARY_OP: {
            if (check_semantic_errors(node->as.binary.left)) has_errors = true;
            if (check_semantic_errors(node->as.binary.right)) has_errors = true;
            break;
        }
        
        case AST_UNARY_OP: {
            if (check_semantic_errors(node->as.unary.operand)) has_errors = true;
            break;
        }
        
        case AST_VAR_DECL: {
            if (node->as.var_decl.value && check_semantic_errors(node->as.var_decl.value)) {
                has_errors = true;
            }
            break;
        }
        
        case AST_ASSIGNMENT: {
            if (check_semantic_errors(node->as.assignment.value)) has_errors = true;
            break;
        }
        
        case AST_RETURN_STMT: {
            if (node->as.return_stmt.value && check_semantic_errors(node->as.return_stmt.value)) {
                has_errors = true;
            }
            break;
        }
        
        case AST_EXPRESSION_STMT: {
            if (check_semantic_errors(node->as.expression)) has_errors = true;
            break;
        }
        
        // Leaf nodes don't need recursive checking
        case AST_LITERAL:
        case AST_IDENTIFIER:
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
            break;
            
        default:
            // For any unhandled node types, just return false for now
            break;
    }
    
    return has_errors;
}

// Syntax checking function - validates syntax and function calls without execution  
static void check_syntax_only(const char* path) {
    char* source = read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    printf("Checking syntax for: %s\n", path);
    
    // Tokenize with dynamic buffer
    Lexer lexer;
    lexer_init(&lexer, source);
    
    int capacity = 2048;
    int token_count = 0;
    Token* tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) {
        fprintf(stderr, "Out of memory while tokenizing %s\n", path);
        free(source);
        exit(1);
    }
    bool has_lexical_errors = false;
    
    printf("Lexical analysis... ");
    for (;;) {
        Token token = lexer_scan_token(&lexer);
        if (token_count >= capacity) {
            int new_capacity = capacity * 2;
            Token* grown = (Token*)realloc(tokens, sizeof(Token) * new_capacity);
            if (!grown) {
                fprintf(stderr, "Error: File too large (exceeds %d tokens) while checking %s\n", capacity, path);
                free(tokens);
                free(source);
                exit(1);
            }
            tokens = grown;
            capacity = new_capacity;
        }
        tokens[token_count++] = token;
        
        if (token.type == TOKEN_ERROR) {
            fprintf(stderr, "Lexical error at line %d: %.*s\n", token.line, token.length, token.start);
            has_lexical_errors = true;
        }
        
        if (token.type == TOKEN_EOF) break;
    }
    
    if (has_lexical_errors) {
        printf("❌ FAILED\n");
        free(source);
        exit(1);
    } else {
        printf("✅ PASSED\n");
    }
    
    // Parse
    printf("Syntax analysis... ");
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode* ast = parser_parse(&parser);
    
    if (parser.had_error) {
        printf("❌ FAILED\n");
        ast_node_free(ast);
        free(tokens);
        free(source);
        exit(1);
    } else {
        printf("✅ PASSED\n");
    }
    
    // Semantic analysis - check function calls and argument counts
    printf("Semantic analysis... ");
    bool has_semantic_errors = check_semantic_errors(ast);
    
    if (has_semantic_errors) {
        printf("❌ FAILED\n");
        ast_node_free(ast);
        free(source);
        exit(1);
    } else {
        printf("✅ PASSED\n");
    }
    
    printf("\n✅ All checks passed! File is syntactically correct.\n");
    
    ast_node_free(ast);
    free(tokens);
    free(source);
}

// Helper: Get MIME type from file extension
static const char* get_mime_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    ext++; // Skip the dot
    
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) return "text/html";
    if (strcmp(ext, "css") == 0) return "text/css";
    if (strcmp(ext, "js") == 0) return "application/javascript";
    if (strcmp(ext, "json") == 0) return "application/json";
    if (strcmp(ext, "png") == 0) return "image/png";
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, "gif") == 0) return "image/gif";
    if (strcmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcmp(ext, "ico") == 0) return "image/x-icon";
    if (strcmp(ext, "woff") == 0) return "font/woff";
    if (strcmp(ext, "woff2") == 0) return "font/woff2";
    if (strcmp(ext, "ttf") == 0) return "font/ttf";
    if (strcmp(ext, "kyl") == 0) return "text/plain";
    if (strcmp(ext, "txt") == 0) return "text/plain";
    return "application/octet-stream";
}

// Helper: Sanitize filename for C variable name
static void sanitize_var_name(const char* path, char* out, size_t out_size) {
    size_t i, j = 0;
    for (i = 0; i < strlen(path) && j < out_size - 1; i++) {
        char c = path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

// Helper: Recursively scan directory and collect files
typedef struct {
    char** paths;
    int count;
    int capacity;
} FileList;

static void file_list_init(FileList* list) {
    list->capacity = 64;
    list->count = 0;
    list->paths = malloc(sizeof(char*) * list->capacity);
}

static void file_list_add(FileList* list, const char* path) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->paths = realloc(list->paths, sizeof(char*) * list->capacity);
    }
    list->paths[list->count++] = strdup(path);
}

static void file_list_free(FileList* list) {
    for (int i = 0; i < list->count; i++) {
        free(list->paths[i]);
    }
    free(list->paths);
}

#include <dirent.h>
static void scan_directory_recursive(const char* dir_path, const char* base_path, FileList* list) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Warning: Cannot open directory: %s\n", dir_path);
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Recurse into subdirectory
                scan_directory_recursive(full_path, base_path, list);
            } else if (S_ISREG(st.st_mode)) {
                // Regular file - compute relative path
                const char* rel_path = full_path + strlen(base_path);
                while (*rel_path == '/') rel_path++;  // Skip leading slashes
                file_list_add(list, rel_path);
            }
        }
    }
    
    closedir(dir);
}

// Generate embedded_resources.c from directories
static int embed_resources_from_dirs(const char** dirs, int dir_count, const char* output_file) {
    FILE* out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", output_file);
        return 1;
    }
    
    // Write header
    fprintf(out, "// Auto-generated embedded resources\n");
    fprintf(out, "// DO NOT EDIT - Generated by kuyil --embed-dir\n\n");
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include \"heapfs.h\"\n\n");
    
    // Collect all files from all directories
    FileList all_files;
    file_list_init(&all_files);
    
    size_t total_bytes = 0;
    
    for (int d = 0; d < dir_count; d++) {
        const char* dir = dirs[d];
        printf("Scanning directory: %s\n", dir);
        
        FileList dir_files;
        file_list_init(&dir_files);
        scan_directory_recursive(dir, dir, &dir_files);
        
        // Process each file
        for (int i = 0; i < dir_files.count; i++) {
            const char* rel_path = dir_files.paths[i];
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, rel_path);
            
            FILE* f = fopen(full_path, "rb");
            if (!f) {
                fprintf(stderr, "Warning: Cannot read %s\n", full_path);
                continue;
            }
            
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            rewind(f);
            
            if (file_size < 0) {
                fclose(f);
                continue;
            }
            
            printf("  - %s (%ld bytes, %s)\n", rel_path, file_size, get_mime_type(rel_path));
            total_bytes += file_size;
            
            // Generate variable name
            char var_name[256];
            sanitize_var_name(rel_path, var_name, sizeof(var_name));
            
            // Write array declaration
            fprintf(out, "static const unsigned char embedded_%s_data[] = {\n", var_name);
            
            // Write bytes
            unsigned char buffer[16];
            size_t bytes_read;
            int col = 0;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                for (size_t j = 0; j < bytes_read; j++) {
                    if (col == 0) fprintf(out, "    ");
                    fprintf(out, "0x%02x, ", buffer[j]);
                    col++;
                    if (col >= 16) {
                        fprintf(out, "\n");
                        col = 0;
                    }
                }
            }
            if (col > 0) fprintf(out, "\n");
            fprintf(out, "};\n\n");
            
            fclose(f);
            file_list_add(&all_files, rel_path);
        }
        
        file_list_free(&dir_files);
    }
    
    printf("\nTotal: %d files, %zu bytes\n", all_files.count, total_bytes);
    
    // Write initialization function
    fprintf(out, "// Initialize embedded resources into heapfs\n");
    fprintf(out, "void heapfs_init_embedded_resources(HeapFS* fs) {\n");
    
    for (int i = 0; i < all_files.count; i++) {
        const char* path = all_files.paths[i];
        char var_name[256];
        sanitize_var_name(path, var_name, sizeof(var_name));
        
        // Find file size (we need to re-read it)
        for (int d = 0; d < dir_count; d++) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dirs[d], path);
            
            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
                fprintf(out, "    heapfs_add_file(fs, \"%s\", \n", path);
                fprintf(out, "                    (const char*)embedded_%s_data, %ld, \n", 
                        var_name, (long)st.st_size);
                fprintf(out, "                    \"%s\");\n", get_mime_type(path));
                break;
            }
        }
    }
    
    fprintf(out, "}\n");
    
    fclose(out);
    file_list_free(&all_files);
    
    printf("Generated: %s\n", output_file);
    return 0;
}

int main(int argc, char* argv[]) {
    // Initialize logging system first
    log_init(LOG_WARNING);
    
    // Initialize heapfs
    extern HeapFS g_heapfs;
    heapfs_init(&g_heapfs, 32);  // Start with capacity for 32 files
    
    // Initialize embedded resources if available (runtime check)
    extern void heapfs_init_embedded_resources(HeapFS* fs) __attribute__((weak));
    if (heapfs_init_embedded_resources) {
        heapfs_init_embedded_resources(&g_heapfs);
        LOG_INFO("Initialized embedded heap filesystem with %d files", g_heapfs.count);
    }
    
    // HTTP subsystem now handled by shared libraries
    
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
        } else if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            print_version();
        } else if (strcmp(argv[1], "-") == 0) {
            // Read from stdin
            char* input = malloc(64 * 1024); // 64KB buffer
            size_t pos = 0;
            int c;
            
            while ((c = getchar()) != EOF && pos < 64 * 1024 - 1) {
                input[pos++] = c;
            }
            input[pos] = '\0';
            
            VM vm;
            vm_init(&vm);
            vm_interpret(&vm, input);
            vm_free(&vm);
            
            free(input);
        } else {
            run_file(argv[1]);
        }
    } else {
        // Parse command line arguments
        bool compile_mode = false;
        bool native_mode = false;
        bool embed_bytecode = false;
        char* input_file = NULL;
        char* output_file = NULL;
        char* target_platform = NULL; // For cross-compilation
        bool test_mode = false;
        bool coverage_enabled = false;
        const char* coverage_format = "text";
        
        // Embed directories (up to 16)
        char* embed_dirs[16];
        int embed_dir_count = 0;
        
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
                log_set_level(LOG_DEBUG);
            } else if (strcmp(argv[i], "--embed-dir") == 0) {
                if (i + 1 < argc) {
                    if (embed_dir_count >= 16) {
                        fprintf(stderr, "Error: Maximum 16 --embed-dir directories allowed\n");
                        exit(1);
                    }
                    embed_dirs[embed_dir_count++] = argv[++i];
                } else {
                    fprintf(stderr, "Error: --embed-dir requires a directory path\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compile") == 0) {
                compile_mode = true;
            } else if (strcmp(argv[i], "--native") == 0) {
                native_mode = true;
                compile_mode = true;
            } else if (strcmp(argv[i], "--embed-bytecode") == 0) {
                embed_bytecode = true;
            } else if (strcmp(argv[i], "--target") == 0) {
                if (i + 1 < argc) {
                    target_platform = argv[++i];
                } else {
                    fprintf(stderr, "Error: --target requires a platform (windows, linux, macos)\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    output_file = argv[++i];
                } else {
                    fprintf(stderr, "Error: -o requires an output filename\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "--log-level") == 0) {
                if (i + 1 < argc) {
                    const char* level = argv[++i];
                    if (strcmp(level, "debug") == 0) log_set_level(LOG_DEBUG);
                    else if (strcmp(level, "info") == 0) log_set_level(LOG_INFO);
                    else if (strcmp(level, "warning") == 0) log_set_level(LOG_WARNING);
                    else if (strcmp(level, "error") == 0) log_set_level(LOG_ERROR);
                    else if (strcmp(level, "fatal") == 0) log_set_level(LOG_FATAL);
                    else {
                        fprintf(stderr, "Error: Invalid log level '%s'\n", level);
                        exit(1);
                    }
                } else {
                    fprintf(stderr, "Error: --log-level requires a level\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "--log-file") == 0) {
                if (i + 1 < argc) {
                    FILE* log_file = fopen(argv[++i], "a");
                    if (log_file) {
                        log_set_output(log_file);
                    } else {
                        fprintf(stderr, "Error: Cannot open log file '%s'\n", argv[i]);
                        exit(1);
                    }
                } else {
                    fprintf(stderr, "Error: --log-file requires a filename\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "--no-color") == 0) {
                log_set_colored(false);
            } else if (strcmp(argv[i], "--no-trace") == 0) {
                log_set_trace_calls(false);
            } else if (strcmp(argv[i], "--test") == 0) {
                test_mode = true;
            } else if (strcmp(argv[i], "--coverage") == 0) {
                coverage_enabled = true;
            } else if (strcmp(argv[i], "--coverage-format") == 0) {
                if (i + 1 < argc) {
                    coverage_format = argv[++i];
                } else {
                    fprintf(stderr, "Error: --coverage-format requires a value (text|lcov)\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "--syntax-only") == 0) {
                // Syntax check mode - validate syntax without execution
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    check_syntax_only(argv[++i]);
                    return 0;
                } else {
                    fprintf(stderr, "Error: --check requires a filename\n");
                    exit(1);
                }
            } else if (strcmp(argv[i], "-") == 0) {
                input_file = argv[i];
            } else if (argv[i][0] != '-') {
                input_file = argv[i];
            }
        }
        
        // Process embed directories if specified
        if (embed_dir_count > 0) {
            printf("Embedding resources from %d director%s...\n", 
                   embed_dir_count, embed_dir_count == 1 ? "y" : "ies");
            
            // Add interfaces directory automatically
            embed_dirs[embed_dir_count++] = "interfaces";
            
            // Generate embedded_resources.c
            if (embed_resources_from_dirs((const char**)embed_dirs, embed_dir_count, 
                                         "src/embedded_resources.c") != 0) {
                fprintf(stderr, "Error: Failed to generate embedded resources\n");
                exit(1);
            }
            printf("\n");
            
            // Now rebuild kuyil with embedded resources
            printf("Rebuilding with embedded resources...\n");
            int ret = system("make -j4 2>&1 | grep -v 'Entering\\|Leaving' || true");
            if (ret != 0) {
                fprintf(stderr, "Error: Failed to rebuild (exit code %d)\n", ret);
                exit(1);
            }
            printf("✅ Rebuild complete\n\n");
            
            // Re-exec the newly built kuyil without --embed-dir flags
            printf("Re-executing with embedded resources...\n\n");
            
            // Build new argv without --embed-dir flags
            char** new_argv = malloc(sizeof(char*) * (argc + 1));
            int new_argc = 0;
            new_argv[new_argc++] = argv[0]; // Keep program name
            
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "--embed-dir") == 0) {
                    i++; // Skip next argument (the directory path)
                    continue;
                }
                new_argv[new_argc++] = argv[i];
            }
            new_argv[new_argc] = NULL;
            
            // Re-exec
            execv(argv[0], new_argv);
            
            // If execv fails
            perror("Error re-executing kuyil");
            exit(1);
        }
        
        if (compile_mode) {
            if (input_file == NULL) {
                fprintf(stderr, "Error: No input file specified for compilation\n");
                exit(1);
            }
            if (output_file == NULL) {
                // Generate output filename
                output_file = malloc(strlen(input_file) + 20);
                strcpy(output_file, input_file);
                char* dot = strrchr(output_file, '.');
                if (dot) *dot = '\0';
                if (native_mode) {
                    strcat(output_file, "_native");
                } else {
                    strcat(output_file, "_compiled");
                }
            }
            compile_file_with_options(input_file, output_file, native_mode, embed_bytecode, target_platform);
        } else if (input_file != NULL) {
            // Run with optional test/coverage modes
            VM vm;
            vm_init(&vm);
            if (coverage_enabled) {
                vm_enable_coverage(&vm, true, input_file);
            }
            if (test_mode) {
                vm_enable_test_mode(&vm, true);
            }

            InterpretResult result;
            if (strcmp(input_file, "-") == 0) {
                // Read from stdin
                char* input = malloc(64 * 1024);
                size_t pos = 0; int c;
                while ((c = getchar()) != EOF && pos < 64 * 1024 - 1) input[pos++] = c;
                input[pos] = '\0';
                result = vm_interpret(&vm, input);
                free(input);
            } else {
                const char* ext = strrchr(input_file, '.');
                if (ext && strcmp(ext, ".kyc") == 0) {
                    result = vm_interpret_bytecode(&vm, input_file);
                } else {
                    char* source = read_file(input_file);
                    result = vm_interpret(&vm, source);
                    free(source);
                }
            }

            if (coverage_enabled) {
                if (strcmp(coverage_format, "lcov") == 0) {
                    vm_coverage_report_lcov(&vm);
                } else {
                    vm_coverage_report_text(&vm);
                }
            }

            int failures = 0;
            if (test_mode) {
                // Use the same counter we print to avoid any mismatch
                failures = vm.assertions_failed;
                printf("\nTest summary: %d total, %d failed\n", vm.assertions_total, vm.assertions_failed);
            }

            // If tests failed, exit immediately with non-zero before further cleanup
            if (test_mode && failures > 0) {
                // Diagnostic log to confirm we hit this branch
                kuyil_log_error("Exiting with test failures: %d", failures);
                vm_free(&vm);
                // Use _exit to avoid any atexit handlers accidentally altering status
                _exit(1);
            }

            vm_free(&vm);

            if (result == INTERPRET_COMPILE_ERROR) exit(65);
            if (result == INTERPRET_RUNTIME_ERROR) exit(70);
        } else {
            print_usage();
            exit(1);
        }
    }
    
    log_cleanup();
    return 0;
}