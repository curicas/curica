/**
 * @file wasm_module.h
 * @brief Header definitions for the WebAssembly runtime.
 * 
 * Declares `build_wasm_global` which sets up the `WebAssembly` global object 
 * and Wasm3 interpreter bridges.
 */
#ifndef WASM_MODULE_H
#define WASM_MODULE_H

#include <stdbool.h>
#include "value.h"
#include "vm.h"

bool ensure_wamr_initialized(void);
Value build_wasm_global(VM* vm);

int wasm_module_execute_cli(int argc, char** argv, char** custom_vfs_dirs, int custom_vfs_count);

#endif
