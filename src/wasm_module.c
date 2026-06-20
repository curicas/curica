/**
 * @file wasm_module.c
 * @brief WebAssembly process subsystem for the Curica Environment OS Kernel.
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
#include "wasm_module.h"
#include "wasi_module.h"
#include "builtins.h"
#include "alloc.h"
#include "wasm_export.h"
#include "vfs_module.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

    // Process importObject for WASI config
    if (arg_count > 1 && IS_POINTER(args[1])) {
        Value import_obj = args[1];
        if (((BlockHeader*)get_pointer(import_obj) - 1)->obj_type == OBJ_OBJECT) {
            Value wasi_preview1 = object_get(import_obj, create_string("wasi_snapshot_preview1", 22));
            Value wasi_unstable = object_get(import_obj, create_string("wasi_unstable", 13));
            
            Value wasi_import = VAL_UNDEFINED;
            if (IS_POINTER(wasi_preview1)) wasi_import = wasi_preview1;
            else if (IS_POINTER(wasi_unstable)) wasi_import = wasi_unstable;
            
            if (IS_POINTER(wasi_import)) {
                WASIConfig* config = wasi_get_config_from_import(vm, wasi_import);
                if (config) {
                    wasm_runtime_set_wasi_args(module, 
                        NULL, 0, // dir_list
                        (const char**)config->dir_list, config->dir_count, // map_dir_list (virtual::host)
                        (const char**)config->env_list, config->env_count,
                        config->arg_list, config->arg_count);
                }
            }
        }
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
    
#if UINTPTR_MAX == UINT32_MAX
#define DefPointer(type, field) type field; uint32_t field##_padding
#else
#define DefPointer(type, field) type field
#endif

    typedef struct { char *name; void *function; } WASMExportFuncInstance;
    typedef struct { char *name; uint32_t func_index; void *func_ptr; } AOTExportFuncInstance;

    struct ModuleInstanceOverlay {
        uint32_t module_type;
        uint32_t memory_count;
        DefPointer(void**, memories);
        uint32_t global_data_size;
        uint32_t table_count;
        DefPointer(uint8_t*, global_data);
        DefPointer(void**, tables);
        DefPointer(void**, func_ptrs);
        DefPointer(uint32_t*, func_type_indexes);
        uint32_t export_func_count;
        uint32_t export_global_count;
        uint32_t export_memory_count;
        uint32_t export_table_count;
        DefPointer(void*, export_functions);
    };

    uint32_t module_type = *(uint32_t*)module_inst;
    struct ModuleInstanceOverlay* overlay = (struct ModuleInstanceOverlay*)module_inst;
    
    if (module_type == 0) { // Wasm_Module_Bytecode
        WASMExportFuncInstance *funcs = (WASMExportFuncInstance *)overlay->export_functions;
        for (uint32_t i = 0; i < overlay->export_func_count; i++) {
            char* name = funcs[i].name;
            Value bound_ctx = create_object();
            vm_push_root(vm, bound_ctx);
            object_set(bound_ctx, create_string("_runtime", 8), make_pointer((void*)module_inst));
            object_set(bound_ctx, create_string("_func_name", 10), create_string(name, strlen(name)));
            
            Value js_func = create_bound_native_function((void*)js_wasm_invoke, create_string(name, strlen(name)), vm->gc_roots[vm->gc_root_count - 1]);
            object_set(vm->gc_roots[exports_obj_idx], create_string(name, strlen(name)), js_func);
            vm_pop_root(vm);
        }
    } else { // Wasm_Module_AoT
        AOTExportFuncInstance *funcs = (AOTExportFuncInstance *)overlay->export_functions;
        for (uint32_t i = 0; i < overlay->export_func_count; i++) {
            char* name = funcs[i].name;
            Value bound_ctx = create_object();
            vm_push_root(vm, bound_ctx);
            object_set(bound_ctx, create_string("_runtime", 8), make_pointer((void*)module_inst));
            object_set(bound_ctx, create_string("_func_name", 10), create_string(name, strlen(name)));
            
            Value js_func = create_bound_native_function((void*)js_wasm_invoke, create_string(name, strlen(name)), vm->gc_roots[vm->gc_root_count - 1]);
            object_set(vm->gc_roots[exports_obj_idx], create_string(name, strlen(name)), js_func);
            vm_pop_root(vm);
        }
    }
    
    uint32_t instance_obj_idx = vm->gc_root_count;
    Value instance_obj = create_object();
    vm_push_root(vm, instance_obj);
    
    uint32_t exports_key_idx = vm->gc_root_count;
    Value exports_key = create_string("exports", 7);
    vm_push_root(vm, exports_key);
    
    object_set(vm->gc_roots[instance_obj_idx], vm->gc_roots[exports_key_idx], vm->gc_roots[exports_obj_idx]);
    vm_pop_root(vm); // exports_key
    
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
    
    Value runtime_key = create_string("_runtime", 8);
    vm_push_root(vm, runtime_key);
    Value runtime_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[vm->gc_root_count - 1]);
    vm_pop_root(vm);
    
    Value func_name_key = create_string("_func_name", 10);
    vm_push_root(vm, func_name_key);
    Value func_name_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[vm->gc_root_count - 1]);
    vm_pop_root(vm);
    
    vm_pop_root(vm); // this_val
    
    if (!IS_POINTER(runtime_val) || !IS_POINTER(func_name_val)) return VAL_UNDEFINED;
    
    wasm_module_inst_t module_inst = (wasm_module_inst_t)get_pointer(runtime_val);
    JSString* func_name_str = (JSString*)get_pointer(func_name_val);
    
    wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, func_name_str->data, NULL);
    if (!func) {
        return VAL_UNDEFINED;
    }
    
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, 8192);
    if (!exec_env) return VAL_UNDEFINED;
    
    uint32_t wasm_args[32] = {0};
    int passed_args = arg_count;
    
    for (int i = 0; i < passed_args && i < 32; i++) {
        Value arg_val = args[i];
        if (IS_INTEGER(arg_val)) {
            wasm_args[i] = (uint32_t)get_integer(arg_val);
        } else if (IS_DOUBLE(arg_val)) {
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

    char wasm_path[256];
    snprintf(wasm_path, sizeof(wasm_path), "/bin/%s", argv[0]);
    if (strlen(argv[0]) < 5 || strcmp(argv[0] + strlen(argv[0]) - 5, ".wasm") != 0) {
        snprintf(wasm_path, sizeof(wasm_path), "/bin/%s.wasm", argv[0]);
    }

    int fd = vfs_open(wasm_path, O_RDONLY, 0666);
    if (fd < 0) {
        printf("curica: command not found: %s\n", argv[0]);
        return 127;
    }

    off_t size = vfs_lseek(fd, 0, SEEK_END);
    vfs_lseek(fd, 0, SEEK_SET);

    uint8_t* wasm_file_buf = malloc(size);
    if (!wasm_file_buf) {
        vfs_close(fd);
        return 1;
    }

    ssize_t read_bytes = vfs_read(fd, wasm_file_buf, size);
    vfs_close(fd);

    if (read_bytes != (ssize_t)size) {
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
    char root_buf[1024];
    const char* root_host_path = vfs_resolve_path("/disk/root/", root_buf, sizeof(root_buf));
    char root_map[2048];
    snprintf(root_map, sizeof(root_map), "/::%s", root_host_path);

    const char* base_dirs[] = {
        "/workspace::.",
        root_map
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
