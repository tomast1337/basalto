#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ast.h"
#include "symtable.h"
#include <stdbool.h>
#include "embedded_files.h"

extern int yyparse();
extern FILE* yyin;
extern ASTNode* root_node;

// Declaration from codegen.c
void codegen(ASTNode* node, FILE* file, FILE* asm_file, const char* source_file_path);

#include "debug.h"

// Helper to write embedded file content to disk
void write_embedded_file(const char* path, const char* content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", content);
        fclose(f);
    }
}

// Global debug flag (accessible from lexer)
bool debug_mode = false;

int main(int argc, char** argv) {
    const char* input_filename = NULL;
    const char* output_filename = NULL; // Specified via -o
    bool transpile_only = false;             // Specified via --emit-c
    bool run_after_compile = false;          // Specified via --run or -r

    // 1. Parse Arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = true;
        } 
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: basalto [options] <input.bso>\n");
            printf("Options:\n");
            printf("  -o <name>     Specify output binary name\n");
            printf("  --emit-c      Generate C code only (skip GCC)\n");
            printf("  --run, -r     Run the compiled program immediately\n");
            printf("  --debug, -d   Enable debug output\n");
            return 0;
        } 
        else if (strcmp(argv[i], "--emit-c") == 0) {
            transpile_only = true;
        }
        else if (strcmp(argv[i], "--run") == 0 || strcmp(argv[i], "-r") == 0) {
            run_after_compile = true;
        }
        else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_filename = argv[i + 1];
                i++; // Skip next arg
            } else {
                fprintf(stderr, "[Basalto] Error: -o requires a filename\n");
                return EXIT_FAILURE;
            }
        }
        else if (argv[i][0] != '-') {
            input_filename = argv[i];
        }
    }

    if (!input_filename) {
        printf("Usage: basalto [options] <input.bso>\n");
        return EXIT_FAILURE;
    }

    // 2. SETUP RUNTIME ENVIRONMENT
    // Create a temp directory for the runtime
    const char* tmp_dir = "/tmp/basalto_runtime";
    #ifdef _WIN32
        mkdir(tmp_dir);
    #else
        mkdir(tmp_dir, 0777);
    #endif

    // Extract the embedded files
    char path_buf[512];
    
    sprintf(path_buf, "%s/basalto.h", tmp_dir);
    write_embedded_file(path_buf, SRC_BASALTO_H);

    sprintf(path_buf, "%s/core.c", tmp_dir);
    write_embedded_file(path_buf, SRC_CORE_C);

    sprintf(path_buf, "%s/sds.h", tmp_dir);
    write_embedded_file(path_buf, SRC_SDS_H);

    sprintf(path_buf, "%s/sds.c", tmp_dir);
    write_embedded_file(path_buf, SRC_SDS_C);

    sprintf(path_buf, "%s/stb_ds.h", tmp_dir);
    write_embedded_file(path_buf, SRC_STB_DS_H);

    sprintf(path_buf, "%s/sdsalloc.h", tmp_dir);
    write_embedded_file(path_buf, SRC_SDSALLOC_H);

    // 3. Open Input
    yyin = fopen(input_filename, "r");
    if (!yyin) {
        fprintf(stderr, "[Basalto] Error: Could not open file %s\n", input_filename);
        return EXIT_FAILURE;
    }

    // 4. Parse (Build AST)
    if (debug_mode) printf("[Basalto] Parsing...\n");
    scope_enter();
    if (yyparse() != 0) {
        // Error already printed by yyerror
        return EXIT_FAILURE;
    }
    
    if (!root_node) {
        fprintf(stderr, "[Basalto] Error: Empty program or parse failure.\n");
        return EXIT_FAILURE;
    }

    // Debug: Print AST tree (before unwrapping)
    if (debug_mode) {
        print_ast(root_node);
    }

    // 5. Unwrap the wrapper structure and flatten the BLOCK
    // The parser creates: WRAPPER -> [IMPORTS..., ACTUAL_PROGRAM -> [BLOCK -> [statements...]]]
    // We need: ACTUAL_PROGRAM -> [statements...] (statements are direct children, no BLOCK wrapper)
    ASTNode* actual_root = root_node;
    
    // Check if we have a wrapper structure (imports before program/library)
    if (root_node->type == NODE_PROGRAM || root_node->type == NODE_LIBRARY) {
        // Find the actual program/library node (should be the last child if wrapper has imports)
        ASTNode* inner_program = NULL;
        
        // Look for the actual PROGRAM/LIBRARY node in children
        // It should be the last child (after any imports)
        for (int i = arrlen(root_node->children) - 1; i >= 0; i--) {
            if (root_node->children[i]->type == NODE_PROGRAM || 
                root_node->children[i]->type == NODE_LIBRARY) {
                inner_program = root_node->children[i];
                break;
            }
        }
        
        if (inner_program) {
            // Extract the name from inner program
            const char* program_name = inner_program->name;
            
            // Create a new flattened root
            actual_root = ast_new(inner_program->type);
            if (program_name) {
                actual_root->name = sdsnew(program_name);
            }
            
            // --- FLATTENING LOGIC ---
            // Iterate children of the inner Program/Library
            for(int j = 0; j < arrlen(inner_program->children); j++) {
                ASTNode* inner = inner_program->children[j];
                
                // Skip imports (handled recursively elsewhere)
                if (inner->type == NODE_IMPORT) continue;
                
                // CRITICAL: Unwrap the BLOCK
                // The parser puts the program body inside a NODE_BLOCK. 
                // We must pull the contents OUT so they become top-level instructions.
                if (inner->type == NODE_BLOCK) {
                    for(int k = 0; k < arrlen(inner->children); k++) {
                        ASTNode* instruction = inner->children[k];
                        ast_add_child(actual_root, instruction);
                    }
                } else {
                    // If there are other direct children (unlikely with current grammar, but safe)
                    ast_add_child(actual_root, inner);
                }
            }
            // --- END FLATTENING ---
        }
    }
    
    // If we didn't have a wrapper, we still need to check for direct BLOCK unwrapping
    // This handles the case where parser returns PROGRAM -> [BLOCK -> [statements]]
    if (actual_root == root_node && 
        (actual_root->type == NODE_PROGRAM || actual_root->type == NODE_LIBRARY)) {
        // Check if first child is a BLOCK - if so, unwrap it
        if (arrlen(actual_root->children) > 0 && 
            actual_root->children[0]->type == NODE_BLOCK) {
            
            ASTNode* block = actual_root->children[0];
            ASTNode* new_root = ast_new(actual_root->type);
            if (actual_root->name) {
                new_root->name = sdsnew(actual_root->name);
            }
            
            // Move all block children to root (unwrapping)
            for (int i = 0; i < arrlen(block->children); i++) {
                ast_add_child(new_root, block->children[i]);
            }
            
            actual_root = new_root;
        }
    }

    // Debug: Print AST tree (after unwrapping)
    if (debug_mode) {
        printf("\n=== AST After Unwrapping ===\n");
        print_ast(actual_root);
    }

    // 6. Determine Output Name & Type
    // Priority: CLI Flag (-o) > Program/Library Name > Default "output"
    const char* final_name = "output";
    int is_library = (actual_root->type == NODE_LIBRARY);
    
    if (output_filename) {
        final_name = output_filename;
    } else if (actual_root->name) {
        // Use the name defined in 'programa "Name"' or 'biblioteca "Name"'
        final_name = actual_root->name;
    }

    // 7. Generate C File Name AND ASM File Name
    char c_filename[256];
    char asm_filename[256];
    snprintf(c_filename, sizeof(c_filename), "%s.c", final_name);
    snprintf(asm_filename, sizeof(asm_filename), "%s_embeds.S", final_name);

    // 8. Generate Code
    if (debug_mode) printf("[Basalto] Generating %s and %s...\n", c_filename, asm_filename);
    
    FILE* out_c = fopen(c_filename, "w");
    FILE* out_asm = fopen(asm_filename, "w");
    
    if (!out_c || !out_asm) {
        fprintf(stderr, "[Basalto] Error: Could not create output files.\n");
        if (out_c) fclose(out_c);
        if (out_asm) fclose(out_asm);
        return EXIT_FAILURE;
    }
    
    // Pass BOTH files and source path to codegen (use unwrapped root)
    codegen(actual_root, out_c, out_asm, input_filename);
    
    fclose(out_c);
    fclose(out_asm);

    // 9. Compile with GCC (unless --emit-c is set)
    if (transpile_only) {
        printf("[Basalto] Transpilation complete: %s\n", c_filename);
    } else {
        char cmd[2048]; // Bump size just in case
        
        if (is_library) {
            // LIBRARY MODE: Output .so, add -shared -fPIC
            printf("[Basalto] Compiling Library '%s.so'...\n", final_name);
            snprintf(cmd, sizeof(cmd), 
                "gcc %s %s %s/core.c %s/sds.c -o %s.so -shared -fPIC -I %s -Wall -ldl -lm", 
                c_filename, asm_filename, tmp_dir, tmp_dir, final_name, tmp_dir);
        } else {
            // PROGRAM MODE: Output executable
            printf("[Basalto] Compiling Executable '%s'...\n", final_name);
            snprintf(cmd, sizeof(cmd), 
                "gcc %s %s %s/core.c %s/sds.c -o %s -I %s -Wall -ldl -lm", 
                c_filename, asm_filename, tmp_dir, tmp_dir, final_name, tmp_dir);
        }
        
        if (debug_mode) printf("[CMD] %s\n", cmd);
        
        int res = system(cmd);
        if (res != 0) {
            fprintf(stderr, "[Basalto] Compilation failed.\n");
            return EXIT_FAILURE;
        }
        
        if (is_library) {
            printf("[Basalto] Build successful: ./%s.so\n", final_name);
        } else {
            printf("[Basalto] Build successful: ./%s\n", final_name);
            
            // 10. Run the program if --run flag is set
            if (run_after_compile) {
                printf("[Basalto] Running ./%s...\n", final_name);
                char run_cmd[512];
                snprintf(run_cmd, sizeof(run_cmd), "./%s", final_name);
                
                if (debug_mode) printf("[CMD] %s\n", run_cmd);
                
                int run_res = system(run_cmd);
                // Return the program's exit code
                return run_res;
            }
        }
    }

    return EXIT_SUCCESS;
}