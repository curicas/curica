/**
 * @file sandbox.c
 * @brief Edge Compute Sandboxing Mode
 * 
 * Provides an API to lock down the execution environment for multi-tenant isolation.
 */

#include "vm.h"
#include "alloc.h"

static Value sandbox_enable(VM* vm, Value this_val, int arg_count, Value* args) {
    vm->sandbox_mode = true;
    return VAL_TRUE;
}

static Value sandbox_isEnabled(VM* vm, Value this_val, int arg_count, Value* args) {
    return vm->sandbox_mode ? VAL_TRUE : VAL_FALSE;
}

void vm_register_sandbox(VM* vm) {
    vm->sandbox_mode = false; // Default off
    
    Value sandbox_obj = create_object();
    object_set(sandbox_obj, create_string("enable", 6), create_native_function((void*)sandbox_enable, create_string("enable", 6)));
    object_set(sandbox_obj, create_string("isEnabled", 9), create_native_function((void*)sandbox_isEnabled, create_string("isEnabled", 9)));
    
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    object_set(curica_obj, create_string("Sandbox", 7), sandbox_obj);
}
