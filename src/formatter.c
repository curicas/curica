/**
 * @file formatter.c
 * @brief Implementation of the native JavaScript code formatter.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#include "formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Formats a JavaScript file in-place using a naive lexical pass.
 * 
 * The algorithm scans through the source character by character. It safely
 * ignores strings and comments, inserting newlines and spaces appropriately
 * around braces `{ }` and semicolons `;` based on the current indentation level.
 * 
 * @param filepath Path to the target JavaScript file.
 */
void format_javascript_file(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s for formatting\n", filepath);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = malloc(size + 1);
    fread(src, 1, size, f);
    src[size] = '\0';
    fclose(f);

    // Naive formatter: Track indent level, insert newlines at {, }, ;
    char* out = malloc(size * 2 + 1024); // Safe buffer
    int out_idx = 0;
    int indent = 0;
    int in_string = 0;
    int in_comment = 0;
    int needs_indent = 1;

    for (int i = 0; i < size; i++) {
        char c = src[i];
        
        if (in_comment == 0 && in_string == 0) {
            if (c == '/' && src[i+1] == '/') { in_comment = 1; }
            if (c == '/' && src[i+1] == '*') { in_comment = 2; }
        }
        
        if (in_comment == 1) {
            out[out_idx++] = c;
            if (c == '\n') { in_comment = 0; needs_indent = 1; }
            continue;
        }
        if (in_comment == 2) {
            out[out_idx++] = c;
            if (c == '*' && src[i+1] == '/') {
                out[out_idx++] = '/';
                i++;
                in_comment = 0;
            }
            continue;
        }
        
        if (in_string == 0) {
            if (c == '\'') in_string = 1;
            else if (c == '"') in_string = 2;
            else if (c == '`') in_string = 3;
        } else {
            out[out_idx++] = c;
            if (c == '\\') { out[out_idx++] = src[++i]; continue; }
            if ((in_string == 1 && c == '\'') || (in_string == 2 && c == '"') || (in_string == 3 && c == '`')) {
                in_string = 0;
            }
            continue;
        }
        
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (c == '\n') needs_indent = 1;
            if (out_idx > 0 && out[out_idx-1] != ' ' && out[out_idx-1] != '\n') {
                out[out_idx++] = ' ';
            }
            continue;
        }
        
        if (c == '}') {
            indent--;
            if (out_idx > 0 && out[out_idx-1] == ' ') out_idx--;
            if (out_idx > 0 && out[out_idx-1] != '\n') out[out_idx++] = '\n';
            needs_indent = 1;
        }
        
        if (needs_indent) {
            if (out_idx > 0 && out[out_idx-1] == ' ') out_idx--;
            for (int j = 0; j < indent; j++) { out[out_idx++] = ' '; out[out_idx++] = ' '; }
            needs_indent = 0;
        }
        
        out[out_idx++] = c;
        
        if (c == '{') {
            indent++;
            out[out_idx++] = '\n';
            needs_indent = 1;
        } else if (c == ';') {
            out[out_idx++] = '\n';
            needs_indent = 1;
        }
    }
    
    out[out_idx] = '\0';
    
    f = fopen(filepath, "w");
    if (f) {
        fwrite(out, 1, out_idx, f);
        fclose(f);
        printf("Formatted %s\n", filepath);
    }
    
    free(src);
    free(out);
}
