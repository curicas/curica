/**
 * @file ts_stripper.c
 * @brief Implementation of the TypeScript Type Stripper.
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
#include "ts_stripper.h"
#include <string.h>

/**
 * @brief Scans and strips TypeScript types from the provided source code.
 * 
 * It iterates through the string, carefully ignoring contents inside strings
 * (single, double, or template literals) and comments (single-line or multi-line),
 * and overwrites `interface ... { ... }` and `type ... = ...;` declarations
 * with space characters.
 * 
 * @param source A null-terminated string containing the TS source to be modified in-place.
 */
void strip_typescript_types(char* source) {
    if (!source) return;
    int len = strlen(source);
    int in_string = 0; // 0: no, 1: ', 2: ", 3: `
    int in_comment = 0; // 0: no, 1: //, 2: /*
    
    for (int i = 0; i < len; i++) {
        if (in_comment == 0 && in_string == 0) {
            if (source[i] == '/' && source[i+1] == '/') {
                in_comment = 1; i++; continue;
            }
            if (source[i] == '/' && source[i+1] == '*') {
                in_comment = 2; i++; continue;
            }
        }
        if (in_comment == 1 && source[i] == '\n') { in_comment = 0; continue; }
        if (in_comment == 2 && source[i] == '*' && source[i+1] == '/') {
            in_comment = 0; i++; continue;
        }
        
        if (in_comment == 0) {
            if (in_string == 0) {
                if (source[i] == '\'') in_string = 1;
                else if (source[i] == '"') in_string = 2;
                else if (source[i] == '`') in_string = 3;
            } else {
                if (source[i] == '\\') { i++; continue; }
                if (in_string == 1 && source[i] == '\'') in_string = 0;
                else if (in_string == 2 && source[i] == '"') in_string = 0;
                else if (in_string == 3 && source[i] == '`') in_string = 0;
            }
        }
        
        if (in_string || in_comment) continue;
        
        // Strip interface blocks: `interface Name { ... }`
        if (i == 0 || source[i-1] == ' ' || source[i-1] == '\n' || source[i-1] == '\t') {
            if (i + 10 < len && strncmp(&source[i], "interface ", 10) == 0) {
                int start = i;
                i += 10;
                while (i < len && source[i] != '{' && source[i] != ';') i++;
                if (i < len && source[i] == '{') {
                    int brace_count = 1;
                    i++;
                    while (i < len && brace_count > 0) {
                        if (source[i] == '{') brace_count++;
                        if (source[i] == '}') brace_count--;
                        i++;
                    }
                }
                for (int j = start; j < i; j++) {
                    if (source[j] != '\n' && source[j] != '\r') source[j] = ' ';
                }
                i--;
                continue;
            }
            
            // Strip type aliases: `type Name = ...;`
            if (i + 5 < len && strncmp(&source[i], "type ", 5) == 0) {
                int start = i;
                i += 5;
                while (i < len && source[i] != ';' && source[i] != '\n') i++;
                if (i < len && source[i] == ';') i++;
                for (int j = start; j < i; j++) {
                    if (source[j] != '\n' && source[j] != '\r') source[j] = ' ';
                }
                i--;
                continue;
            }
        }
    }
}
