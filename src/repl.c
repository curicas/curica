/**
 * @file repl.c
 * @brief Interactive REPL (Read-Eval-Print Loop) Subsystem
 * 
 * Implements a dual-mode interactive shell for Curica. Supports evaluating
 * raw JavaScript expressions dynamically and directly invoking compiled
 * WebAssembly (WASI) tools seamlessly from the terminal prompt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include "repl.h"
#include "compiler.h"
#include "vm.h"
#include "alloc.h"
#include "wasm_export.h"
#include "wasm_module.h"

static void execute_wasm_command(char* line) {
    if (!ensure_wamr_initialized()) {
        printf("curica: failed to initialize WAMR\n");
        return;
    }

    // Parse arguments
    char* argv[64];
    int argc = 0;
    char line_copy[1024];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';
    
    char* token = strtok(line_copy, " \t\r\n");
    while (token && argc < 64) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\r\n");
    }

    if (argc == 0) return;

    extern int wasm_module_execute_cli(int argc, char** argv, char** custom_vfs_dirs, int custom_vfs_count);
    wasm_module_execute_cli(argc, argv, NULL, 0);
}

// Basic REPL that evaluates JS, and falls back to WASM if it's a command
void repl_start(void) {
    printf("Curica Environment (v%s)\n", CURICA_VERSION);
    printf("Type '.help' for more information.\n");

    arena_init(32);
    VM vm;
    vm_init(&vm);
    
    // Default permissions
    vm.allow_read = true;
    vm.allow_write = true;
    vm.allow_net = true;
    vm.allow_ffi = true;
    vm.allow_run = true;

    // Ensure directories exist
    struct stat st = {0};
    if (stat("vfs", &st) == -1) mkdir("vfs", 0755);
    if (stat("vfs/bin", &st) == -1) mkdir("vfs/bin", 0755);

    /*
     * Docker-less WASI Container Bootstrapper
     * 
     * When Curica boots in REPL mode, it transparently intercepts execution to 
     * read `curica.env.json`. If a developer requests tools like `clang` or 
     * `llama.cpp`, the embedded JS bootstrapper automatically provisions the 
     * corresponding pre-compiled WebAssembly (.wasm) binaries from WAPM or GitHub.
     * 
     * This achieves zero-install containerization. The tools are saved to the 
     * local `vfs/bin` directory and seamlessly intercepted by the REPL parser.
     */
    const char* js_bootstrap = 
        "const fs = require('fs');\n"
        "const cp = require('child_process');\n"
        "try {\n"
        "  const envStr = fs.readFileSync('curica.env.json', 'utf8');\n"
        "  const env = JSON.parse(envStr);\n"
        "  if (env && env.tools) {\n"
        "    const tools = Object.keys(env.tools);\n"
        "    if (!fs.existsSync('vfs/src')) { fs.mkdirSync('vfs/src'); }\n"
        "    if (!fs.existsSync('vfs/bin')) { fs.mkdirSync('vfs/bin'); }\n"
        "    for (let i = 0; i < tools.length; i = i + 1) {\n"
        "       let tool = tools[i];\n"
        "       let wasmName = tool;\n"
        "       if (tool == 'llama') { wasmName = 'llama-cli'; }\n"
        "       let wasmPath = ['vfs/bin/', wasmName, '.wasm'].join('');\n"
        "       try {\n"
        "         fs.readFileSync(wasmPath, 'utf8');\n"
        "       } catch (err) {\n"
        "         console.log(['[Curica Environment] Bootstrapping tool: ', tool, ' (', env.tools[tool], ')'].join(''));\n"
        "         console.log(['[Curica Environment] Fetching ', wasmName, '.wasm from registry...'].join(''));\n"
        "         let fetchSuccess = false;\n"
        "         try {\n"
        "             let ret = cp.execSync(['curl -f -sL \"https://wapm.io/', tool, '.wasm\" -o ', wasmPath].join(''));\n"
        "             if (ret != 0) throw new Error('Fetch failed');\n"
        "             fetchSuccess = true;\n"
        "         } catch (fetchErr) {\n"
        "             console.log(['[Curica Environment] Pre-compiled ', wasmName, '.wasm not found on registry. Compiling from source...'].join(''));\n"
        "             console.log(['[Curica Environment] Cloning ', tool, ' source code...'].join(''));\n"
        "             let cloneCmd = ['git clone https://github.com/curica/', tool, ' vfs/src/', tool].join('');\n"
        "             let ret2 = cp.execSync(cloneCmd);\n"
        "             if (ret2 != 0) throw new Error('Git clone failed');\n"
        "             console.log(['[Curica Environment] Running WASI SDK compiler for ', tool, '...'].join(''));\n"
        "             let makeCmd = ['cd vfs/src/', tool, ' && make wasm && cp build/', wasmName, '.wasm ../../bin/'].join('');\n"
        "             let ret3 = cp.execSync(makeCmd);\n"
        "             if (ret3 != 0) throw new Error('Make failed');\n"
        "             console.log(['[Curica Environment] Successfully compiled ', wasmName, ' from source.'].join(''));\n"
        "             fetchSuccess = true;\n"
        "         }\n"
        "         if (fetchSuccess) {\n"
        "             console.log(['[Curica Environment] Cached ', wasmName, '.wasm into local environment.'].join(''));\n"
        "         }\n"
        "       }\n"
        "       if (tool == 'clang') {\n"
        "           if (!fs.existsSync('vfs/include')) { fs.mkdirSync('vfs/include'); }\n"
        "           if (!fs.existsSync('vfs/lib')) { fs.mkdirSync('vfs/lib'); }\n"
        "           try {\n"
        "               fs.readFileSync('vfs/include/stdio.h', 'utf8');\n"
        "           } catch(e) {\n"
        "               console.log('[Curica Environment] Provisioning WASI libc headers for clang...');\n"
        "               cp.execSync('curl -f -sL https://raw.githubusercontent.com/WebAssembly/wasi-libc/main/libc-bottom-half/headers/public/wasi/api.h -o vfs/include/wasi_api.h');\n"
        "               cp.execSync('curl -f -sL https://raw.githubusercontent.com/WebAssembly/wasi-libc/main/libc-top-half/musl/include/stdio.h -o vfs/include/stdio.h');\n"
        "               cp.execSync('git clone https://github.com/WebAssembly/wasi-libc vfs/src/wasi-libc');\n"
        "               cp.execSync('cd vfs/src/wasi-libc && make && cp vfs/lib/wasm32-wasi/libc.a ../../lib/libc.a');\n"
        "           }\n"
        "       }\n"
        "    }\n"
        "  }\n"
        "} catch (e) { console.log('Bootstrapper Error:', e); }\n";

    extern Value js_process_require(VM* vm, Value this_val, int arg_count, Value* args);
    Value require_str = create_string("require", 7);
    Value require_fn = create_native_function((void*)js_process_require, require_str);
    object_set(vm.global_obj, require_str, require_fn);

    CompiledProgram* boot_prog = compile_source((char*)js_bootstrap);
    if (boot_prog) {
        vm_load_program(&vm, boot_prog);
        vm_run(&vm);
        free_compiled_program(boot_prog);
    }

    char line[1024];
    while (1) {
        printf("curica> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break; // EOF
        }

        // Trim newline
        line[strcspn(line, "\n")] = 0;

        if (strlen(line) == 0) continue;

        if (strcmp(line, ".exit") == 0) {
            break;
        }

        if (strcmp(line, ".help") == 0) {
            printf(".exit    Exit the REPL\n");
            printf(".help    Print this help message\n");
            continue;
        }

        // Check if the first word matches a WASM command in vfs/bin/
        char line_copy[1024];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';
        char* token = strtok(line_copy, " \t\r\n");
        
        bool is_wasm_command = false;
        if (token) {
            char wasm_path[256];
            snprintf(wasm_path, sizeof(wasm_path), "vfs/bin/%s.wasm", token);
            if (access(wasm_path, F_OK) == 0) {
                is_wasm_command = true;
            }
        }

        if (is_wasm_command) {
            execute_wasm_command(line);
        } else {
            CompiledProgram* prog = compile_source(line);
            if (prog) {
                vm_load_program(&vm, prog);
                vm_run(&vm);
                free_compiled_program(prog);
            } else {
                // If it fails to compile as JS, try it as a WASM command anyway as a fallback
                execute_wasm_command(line);
            }
        }
    }

    vm_free(&vm);
    arena_free();
}
