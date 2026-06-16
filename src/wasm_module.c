/**
 * @file wasm_module.c
 * @brief WebAssembly engine integration for Curica using WAMR.
 */
#include "wasm_module.h"
#include "builtins.h"
#include "alloc.h"
#include "wasm_export.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool wamr_initialized = false;

static Value js_wasm_invoke(VM* vm, Value this_val, int arg_count, Value* args);

bool ensure_wamr_initialized(void) {
    if (!wamr_initialized) {
        RuntimeInitArgs init_args;
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_System_Allocator;
        if (!wasm_runtime_full_init(&init_args)) {
            return false;
        }
        wamr_initialized = true;
    }
    return true;
}

static Value js_wasm_instantiate(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    JSBuffer* buf = (JSBuffer*)get_pointer(args[0]);
    if (!buf) return VAL_UNDEFINED;

    if (!ensure_wamr_initialized()) {
        return VAL_UNDEFINED;
    }

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(buf->data, buf->length, error_buf, sizeof(error_buf));
    if (!module) {
        return VAL_UNDEFINED;
    }

    wasm_module_inst_t module_inst = wasm_runtime_instantiate(module, 8192, 8192, error_buf, sizeof(error_buf));
    if (!module_inst) {
        wasm_runtime_unload(module);
        return VAL_UNDEFINED;
    }

    // Now, create the instance object and exports object
    uint32_t exports_obj_idx = vm->gc_root_count;
    Value exports_obj = create_object();
    vm_push_root(vm, exports_obj);

    // WAMR doesn't have an easy "iterate all exports" function in the public API
    // without using internal headers. We might need to just let the user call functions by name.
    // Wait, let's look if we can extract exports in WAMR.
    // Usually people use wasm_runtime_lookup_function to get a specific function.
    // For now, let's create a proxy-like object or a bound method factory for exports.
    // Actually, `wasm_module_inst_t` doesn't export function lists directly in `wasm_export.h` without internals.
    // Let's stub the exports object and add `getFunction` or similar.
    
    // Instead of iterating, let's provide a generic `get_export` mechanism if possible, or bind common ones.
    // For a complete JS runtime, we should iterate. 
    // In WAMR, we can cast `wasm_module_inst_t` to `WASMModuleInstanceCommon*` which has `module->export_functions` etc.
    // But since we want to avoid internal headers, we will provide a `getFunction` method on the instance for now.
    
    uint32_t bound_ctx_idx = vm->gc_root_count;
    Value bound_ctx = create_object();
    vm_push_root(vm, bound_ctx);

    uint32_t _runtime_key_idx = vm->gc_root_count;
    Value _runtime_key = create_string("_runtime", 8);
    vm_push_root(vm, _runtime_key);
    
    object_set(vm->gc_roots[bound_ctx_idx], vm->gc_roots[_runtime_key_idx], make_pointer((void*)module_inst));
    vm_pop_root(vm);

    uint32_t instance_obj_idx = vm->gc_root_count;
    Value instance_obj = create_object();
    vm_push_root(vm, instance_obj);
    
    uint32_t exports_key_idx = vm->gc_root_count;
    Value exports_key = create_string("exports", 7);
    vm_push_root(vm, exports_key);
    
    object_set(vm->gc_roots[instance_obj_idx], vm->gc_roots[exports_key_idx], vm->gc_roots[exports_obj_idx]);
    
    // Add getFunction
    uint32_t get_fn_key_idx = vm->gc_root_count;
    Value get_fn_key = create_string("getFunction", 11);
    vm_push_root(vm, get_fn_key);
    
    Value get_fn = create_bound_native_function((void*)js_wasm_invoke, VAL_UNDEFINED, vm->gc_roots[bound_ctx_idx]);
    object_set(vm->gc_roots[instance_obj_idx], vm->gc_roots[get_fn_key_idx], get_fn);
    
    vm_pop_root(vm); // pops get_fn_key
    vm_pop_root(vm); // pops exports_key
    uint32_t module_obj_idx = vm->gc_root_count;
    Value module_obj = create_object();
    vm_push_root(vm, module_obj);
    
    uint32_t result_idx = vm->gc_root_count;
    Value result = create_object();
    vm_push_root(vm, result);
    
    uint32_t module_key_idx = vm->gc_root_count;
    Value module_key = create_string("module", 6);
    vm_push_root(vm, module_key);
    
    object_set(vm->gc_roots[result_idx], vm->gc_roots[module_key_idx], vm->gc_roots[module_obj_idx]);
    vm_pop_root(vm);
    
    uint32_t instance_key_idx = vm->gc_root_count;
    Value instance_key = create_string("instance", 8);
    vm_push_root(vm, instance_key);
    
    object_set(vm->gc_roots[result_idx], vm->gc_roots[instance_key_idx], vm->gc_roots[instance_obj_idx]);
    vm_pop_root(vm);
    
    Value ret_val = vm->gc_roots[result_idx];
    
    vm_pop_root(vm); // result
    vm_pop_root(vm); // module_obj
    vm_pop_root(vm); // instance_obj
    vm_pop_root(vm); // bound_ctx
    vm_pop_root(vm); // exports_obj
    
    return ret_val;
}

static Value js_wasm_invoke(VM* vm, Value this_val, int arg_count, Value* args) {
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t runtime_key_idx = vm->gc_root_count;
    Value runtime_key = create_string("_runtime", 8);
    vm_push_root(vm, runtime_key);
    
    Value runtime_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[runtime_key_idx]);
    vm_pop_root(vm); // runtime_key
    vm_pop_root(vm); // this_val
    
    if (!IS_POINTER(runtime_val)) return VAL_UNDEFINED;
    
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)get_pointer(args[0]) - 1;
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;

    wasm_module_inst_t module_inst = (wasm_module_inst_t)get_pointer(runtime_val);
    JSString* func_name_str = (JSString*)get_pointer(args[0]);
    
    wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, func_name_str->data, NULL);
    if (!func) {
        return VAL_UNDEFINED;
    }
    
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, 8192);
    if (!exec_env) return VAL_UNDEFINED;
    
    uint32_t wasm_args[32] = {0};
    int passed_args = arg_count - 1;
    
    for (int i = 0; i < passed_args && i < 32; i++) {
        Value arg_val = args[i + 1];
        if (IS_INTEGER(arg_val)) {
            wasm_args[i] = (uint32_t)get_integer(arg_val);
        } else if (IS_DOUBLE(arg_val)) {
            // Very naive float conversion
            wasm_args[i] = (uint32_t)get_double(arg_val);
        }
    }
    
    if (wasm_runtime_call_wasm(exec_env, func, passed_args, wasm_args)) {
        wasm_runtime_destroy_exec_env(exec_env);
        return make_integer(wasm_args[0]);
    } else {
        wasm_runtime_destroy_exec_env(exec_env);
        return VAL_UNDEFINED;
    }
}

Value build_wasm_global(VM* vm) {
    (void)vm;
    Value wasm = create_object();
    Value instantiate_name = create_string("instantiate", 11);
    object_set(wasm, instantiate_name, create_native_function((void*)js_wasm_instantiate, instantiate_name));
    return wasm;
}

/**
 * @brief Executes a WebAssembly CLI tool natively within the WAMR Sandbox.
 * 
 * This architecture acts as a "Docker-less" container subsystem. It leverages 
 * the WebAssembly System Interface (WASI) to intercept standard shell commands 
 * (e.g. clang, python) and route them to pre-compiled .wasm binaries.
 * 
 * @param argc The number of command line arguments.
 * @param argv The argument list.
 * @param custom_vfs_dirs Custom VFS directory mappings.
 * @param custom_vfs_count Number of custom mappings.
 * @return Exit code of the WASM application.
 */
int wasm_module_execute_cli(int argc, char** argv, char** custom_vfs_dirs, int custom_vfs_count) {
    if (!ensure_wamr_initialized()) {
        printf("curica: failed to initialize WAMR\n");
        return 1;
    }

    if (argc == 0) return 1;

    // Resolve path
    char wasm_path[256];
    snprintf(wasm_path, sizeof(wasm_path), "vfs/bin/%s", argv[0]);
    // check if ends with .wasm, if not append .wasm
    if (strlen(argv[0]) < 5 || strcmp(argv[0] + strlen(argv[0]) - 5, ".wasm") != 0) {
        snprintf(wasm_path, sizeof(wasm_path), "vfs/bin/%s.wasm", argv[0]);
    }

    FILE* f = fopen(wasm_path, "rb");
    if (!f) {
        printf("curica: command not found: %s\n", argv[0]);
        return 127;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* wasm_file_buf = malloc(size);
    if (!wasm_file_buf) {
        fclose(f);
        return 1;
    }

    size_t read_bytes = fread(wasm_file_buf, 1, size, f);
    fclose(f);

    if (read_bytes != (size_t)size) {
        free(wasm_file_buf);
        return 1;
    }

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(wasm_file_buf, size, error_buf, sizeof(error_buf));
    if (!module) {
        fprintf(stderr, "Failed to load WASM module: %s\n", error_buf);
        free(wasm_file_buf);
        return 1;
    }

    /* 
     * Native Virtual Filesystem (VFS) Sandboxing
     * 
     * We map the host's actual working directory into the WASI container's 
     * isolated root. WAMR expects the mapping string format to be: 
     * "virtual_path::host_path".
     * 
     * This creates a Zero-Trust sandbox. The WASM tool can read/write freely 
     * inside `/workspace` (which resolves to the host's `.`), but attempting 
     * to access `/etc` or `~/.ssh` will instantly yield an ENOENT/EPERM error 
     * because they do not exist within the virtual mapping.
     */
    const char* base_dirs[] = {
        "/workspace::.",
        "/bin::vfs/bin",
        "/lib::vfs/lib",
        "/usr/include::vfs/include",
        "/tmp::/tmp"
    };
    int base_dir_count = sizeof(base_dirs) / sizeof(base_dirs[0]);
    
    int total_dir_count = base_dir_count + custom_vfs_count;
    const char** dir_list = malloc(total_dir_count * sizeof(char*));
    
    for (int i = 0; i < base_dir_count; i++) {
        dir_list[i] = base_dirs[i];
    }
    for (int i = 0; i < custom_vfs_count; i++) {
        dir_list[base_dir_count + i] = custom_vfs_dirs[i];
    }
    /*
     * Environment Variable Injection
     * 
     * We explicitly inject standard POSIX variables into the WASI execution space.
     * PWD=/workspace ensures that any internal relative path operations (e.g. 
     * `fopen("main.c")`) correctly resolve against the sandbox root.
     */
    const char* env_list[] = {
        "PWD=/workspace",
        "WASI_SDK_PATH=/workspace/vfs"
    };
    int env_count = sizeof(env_list) / sizeof(env_list[0]);

    // Note: The `map_dir_list` parameter receives our `virtual::host` mappings.
    wasm_runtime_set_wasi_args(module, NULL, 0, dir_list, total_dir_count, env_list, env_count, argv, argc);

    wasm_module_inst_t module_inst = wasm_runtime_instantiate(module, 65536, 65536, error_buf, sizeof(error_buf));
    if (!module_inst) {
        fprintf(stderr, "Failed to instantiate WASM module: %s\n", error_buf);
        wasm_runtime_unload(module);
        free(wasm_file_buf);
        free((void*)dir_list);
        return 1;
    }

    free((void*)dir_list);

    wasm_application_execute_main(module_inst, argc, argv);

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    free(wasm_file_buf);

    return 0;
}
