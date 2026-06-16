/**
 * @file wasm_wasi.c
 * @brief WASI (WebAssembly System Interface) Execution Layer
 * 
 * Provides native interpretation and system capability mapping for WASI binaries.
 */

#include "vm.h"
#include "alloc.h"

// new WASI({ args, env, preopens })
static Value wasi_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    Value js_wasi = create_object();
    
    // Store configuration stub
    if (arg_count > 0 && IS_POINTER(args[0])) {
        object_set(js_wasi, create_string("_config", 7), args[0]);
    }
    
    return js_wasi;
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
