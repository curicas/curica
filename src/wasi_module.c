/**
 * @file wasi_module.c
 * @brief WASI (WebAssembly System Interface) Execution Layer
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

#include "wasi_module.h"
#include "alloc.h"
#include <string.h>
#include <stdio.h>

// new WASI({ args, env, preopens })
static Value wasi_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || ((BlockHeader*)get_pointer(this_val) - 1)->obj_type != OBJ_OBJECT) {
        this_val = create_object();
    }
    
    WASIConfig* config = calloc(1, sizeof(WASIConfig));
    
    if (arg_count > 0 && IS_POINTER(args[0])) {
        Value options = args[0];
        
        // args
        Value args_val = object_get(options, create_string("args", 4));
        if (IS_POINTER(args_val) && ((BlockHeader*)get_pointer(args_val) - 1)->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(args_val);
            config->arg_count = arr->length;
            if (config->arg_count > 0) {
                config->arg_list = calloc(config->arg_count, sizeof(char*));
                for (uint32_t i = 0; i < config->arg_count; i++) {
                    Value item = arr->elements[i];
                    if (IS_POINTER(item) && ((BlockHeader*)get_pointer(item) - 1)->obj_type == OBJ_STRING) {
                        JSString* str = (JSString*)get_pointer(item);
                        config->arg_list[i] = strdup(str->data);
                    } else {
                        config->arg_list[i] = strdup("");
                    }
                }
            }
        }
        
        // env
        Value env_val = object_get(options, create_string("env", 3));
        if (IS_POINTER(env_val) && ((BlockHeader*)get_pointer(env_val) - 1)->obj_type == OBJ_OBJECT) {
            JSObject* env_obj = (JSObject*)get_pointer(env_val);
            config->env_count = env_obj->count;
            if (config->env_count > 0) {
                config->env_list = calloc(config->env_count, sizeof(char*));
                for (uint32_t i = 0; i < config->env_count; i++) {
                    Value key_val = env_obj->properties[i].key;
                    if (IS_POINTER(key_val) && ((BlockHeader*)get_pointer(key_val) - 1)->obj_type == OBJ_STRING) {
                        JSString* key = (JSString*)get_pointer(key_val);
                        Value val = env_obj->properties[i].value;
                        if (IS_POINTER(val) && ((BlockHeader*)get_pointer(val) - 1)->obj_type == OBJ_STRING) {
                            JSString* val_str = (JSString*)get_pointer(val);
                            size_t len = key->length + 1 + val_str->length + 1;
                            char* env_str = malloc(len);
                            snprintf(env_str, len, "%s=%s", key->data, val_str->data);
                            config->env_list[i] = env_str;
                        } else {
                            size_t len = key->length + 2;
                            char* env_str = malloc(len);
                            snprintf(env_str, len, "%s=", key->data);
                            config->env_list[i] = env_str;
                        }
                    } else {
                        config->env_list[i] = strdup("INVALID_KEY=");
                    }
                }
            }
        }
        
        // preopens
        Value preopens_val = object_get(options, create_string("preopens", 8));
        if (IS_POINTER(preopens_val) && ((BlockHeader*)get_pointer(preopens_val) - 1)->obj_type == OBJ_OBJECT) {
            JSObject* po_obj = (JSObject*)get_pointer(preopens_val);
            config->dir_count = po_obj->count;
            if (config->dir_count > 0) {
                config->dir_list = calloc(config->dir_count, sizeof(char*));
                for (uint32_t i = 0; i < config->dir_count; i++) {
                    Value virtual_path_val = po_obj->properties[i].key;
                    if (IS_POINTER(virtual_path_val) && ((BlockHeader*)get_pointer(virtual_path_val) - 1)->obj_type == OBJ_STRING) {
                        JSString* virtual_path = (JSString*)get_pointer(virtual_path_val);
                        Value val = po_obj->properties[i].value;
                        if (IS_POINTER(val) && ((BlockHeader*)get_pointer(val) - 1)->obj_type == OBJ_STRING) {
                            JSString* host_path = (JSString*)get_pointer(val);
                            size_t len = virtual_path->length + 2 + host_path->length + 1;
                            char* map_str = malloc(len);
                            snprintf(map_str, len, "%s::%s", virtual_path->data, host_path->data);
                            config->dir_list[i] = map_str;
                        } else {
                            config->dir_list[i] = strdup(virtual_path->data);
                        }
                    } else {
                        config->dir_list[i] = strdup("");
                    }
                }
            }
        }
    }
    
    Value import_marker = create_object();
    object_set(import_marker, create_string("_is_wasi_import", 15), make_integer(1));
    object_set(import_marker, create_string("_native_config", 14), make_pointer(config));
    
    object_set(this_val, create_string("wasiImport", 10), import_marker);
    
    return this_val;
}

WASIConfig* wasi_get_config_from_import(VM* vm, Value import_obj) {
    if (!IS_POINTER(import_obj)) return NULL;
    Value is_wasi = object_get(import_obj, create_string("_is_wasi_import", 15));
    if (IS_INTEGER(is_wasi) && get_integer(is_wasi) == 1) {
        Value ptr_val = object_get(import_obj, create_string("_native_config", 14));
        if (IS_POINTER(ptr_val)) {
            return (WASIConfig*)get_pointer(ptr_val);
        }
    }
    return NULL;
}

// wasi.start(instance)
static Value wasi_start(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("start expects a WebAssembly.Instance", 36)));
        return VAL_UNDEFINED;
    }
    
    Value exports_key = create_string("exports", 7);
    Value exports_obj = object_get(args[0], exports_key);
    if (!IS_POINTER(exports_obj)) {
        vm_throw_error(vm, create_error("TypeError", create_string("instance has no exports", 23)));
        return VAL_UNDEFINED;
    }
    
    Value start_key = create_string("_start", 6);
    Value start_fn = object_get(exports_obj, start_key);
    if (!IS_POINTER(start_fn)) {
        vm_throw_error(vm, create_error("TypeError", create_string("instance has no _start export", 29)));
        return VAL_UNDEFINED;
    }
    
    extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
    return vm_call_function(vm, start_fn, 0, NULL);
}

void vm_register_wasi(VM* vm) {
    Value wasi_proto = create_object();
    object_set(wasi_proto, create_string("start", 5), create_native_function((void*)wasi_start, create_string("start", 5)));
    
    Value wasi_ctor = create_native_function((void*)wasi_constructor, create_string("WASI", 4));
    object_set(wasi_ctor, create_string("prototype", 9), wasi_proto);
    
    // Register globally as `WASI`
    object_set(vm->global_obj, create_string("WASI", 4), wasi_ctor);
}
