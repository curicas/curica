/**
 * @file ffi_module.c
 * @brief Foreign Function Interface (FFI) Bindings for Curica
 * 
 * Exposes a 'Curica.FFI' module enabling dynamic loading of native libraries
 * and execution of C functions without N-API wrapper code.
 */
#include "vm.h"
#include "alloc.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// ffi_loadLibrary(path)
static Value ffi_loadLibrary(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_NULL;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    void* handle = dlopen(path_str->data, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        vm_throw_error(vm, create_error("Error", create_string(dlerror(), strlen(dlerror()))));
        return VAL_NULL;
    }
    return make_pointer(handle);
}

// ffi_getSymbol(handle, name)
static Value ffi_getSymbol(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_NULL;
    void* handle = get_pointer(args[0]);
    JSString* name_str = (JSString*)get_pointer(args[1]);
    
    void* symbol = dlsym(handle, name_str->data);
    if (!symbol) {
        vm_throw_error(vm, create_error("Error", create_string(dlerror(), strlen(dlerror()))));
        return VAL_NULL;
    }
    return make_pointer(symbol);
}

// ffi_callSymbol(symbol, returnType, [argTypes], [args])
// Simplified stub that handles primitive args up to 4 parameters.
// For a production FFI, libffi would be required here.
static Value ffi_callSymbol(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 4 || !IS_POINTER(args[0])) return VAL_NULL;
    void* symbol = get_pointer(args[0]);
    // args[1] returnType (string: 'int', 'double', 'void')
    // args[2] argTypes (array of strings)
    // args[3] args (array of values)
    
    JSArray* js_args = (JSArray*)get_pointer(args[3]);
    
    // Very primitive calling convention stub:
    // In x86_64, first 6 integer/pointer args go in RDI, RSI, RDX, RCX, R8, R9.
    // First 8 float args go in XMM0-7.
    // Since we don't have libffi, we'll cast to a variadic-like function pointer,
    // though this is technically undefined behavior, it serves as the initial PoC.
    
    typedef long (*BasicFFIFunc)(long, long, long, long);
    BasicFFIFunc func = (BasicFFIFunc)symbol;
    
    long c_args[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4 && i < (int)js_args->length; i++) {
        Value val = js_args->elements[i];
        if (IS_INTEGER(val)) c_args[i] = get_integer(val);
        else if (IS_POINTER(val)) c_args[i] = (long)get_pointer(val);
        else c_args[i] = 0;
    }
    
    long result = func(c_args[0], c_args[1], c_args[2], c_args[3]);
    
    JSString* ret_type = (JSString*)get_pointer(args[1]);
    if (strcmp(ret_type->data, "int") == 0) {
        return make_integer(result);
    } else if (strcmp(ret_type->data, "pointer") == 0) {
        return make_pointer((void*)result);
    }
    
    return VAL_UNDEFINED;
}

void vm_register_ffi_module(VM* vm) {
    Value ffi_obj = create_object();
    
    object_set(ffi_obj, create_string("loadLibrary", 11), create_native_function((void*)ffi_loadLibrary, create_string("loadLibrary", 11)));
    object_set(ffi_obj, create_string("getSymbol", 9), create_native_function((void*)ffi_getSymbol, create_string("getSymbol", 9)));
    object_set(ffi_obj, create_string("callSymbol", 10), create_native_function((void*)ffi_callSymbol, create_string("callSymbol", 10)));
    
    // Mount it on the global object for easy access during development
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    object_set(curica_obj, create_string("FFI", 3), ffi_obj);
}
