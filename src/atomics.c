/**
 * @file atomics.c
 * @brief SharedArrayBuffer and Atomics Implementation
 * 
 * Implements cross-thread shared memory buffers and lockless atomic operations 
 * via C11 <stdatomic.h>.
 */

#include "vm.h"
#include "alloc.h"
#include <stdatomic.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t length;
    uint8_t* data;
} JSSharedArrayBuffer;

// Atomics.add(typedArray, index, value)
static Value atomics_add(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 3 || !IS_POINTER(args[0]) || !IS_INTEGER(args[1]) || !IS_INTEGER(args[2])) {
        vm_throw_error(vm, create_error("TypeError", create_string("Invalid arguments to Atomics.add", 32)));
        return VAL_UNDEFINED;
    }
    
    // In a full implementation, we'd extract the underlying SharedArrayBuffer from the TypedArray.
    // Here we stub out the logic assuming args[0] is the SharedArrayBuffer itself.
    JSSharedArrayBuffer* sab = (JSSharedArrayBuffer*)get_pointer(args[0]);
    int index = get_integer(args[1]);
    int value = get_integer(args[2]);
    
    if (index < 0 || index >= (int)sab->length) {
        vm_throw_error(vm, create_error("RangeError", create_string("Out of bounds", 13)));
        return VAL_UNDEFINED;
    }
    
    _Atomic(int32_t)* target = (_Atomic(int32_t)*)&sab->data[index];
    int32_t old_val = atomic_fetch_add(target, value);
    
    return make_integer(old_val);
}

// Atomics.load(typedArray, index)
static Value atomics_load(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_INTEGER(args[1])) {
        return VAL_UNDEFINED;
    }
    
    JSSharedArrayBuffer* sab = (JSSharedArrayBuffer*)get_pointer(args[0]);
    int index = get_integer(args[1]);
    
    if (index < 0 || index >= (int)sab->length) return VAL_UNDEFINED;
    
    _Atomic(int32_t)* target = (_Atomic(int32_t)*)&sab->data[index];
    int32_t val = atomic_load(target);
    
    return make_integer(val);
}

// SharedArrayBuffer constructor stub
static Value shared_array_buffer_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    
    uint32_t length = get_integer(args[0]);
    void* shared_mem = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_mem == MAP_FAILED) {
        vm_throw_error(vm, create_error("SystemError", create_string("Failed to mmap SharedArrayBuffer", 32)));
        return VAL_UNDEFINED;
    }
    
    memset(shared_mem, 0, length);
    
    // Allocate wrapper in Arena
    JSSharedArrayBuffer* sab = arena_alloc(OBJ_SHARED_ARRAY_BUFFER, sizeof(JSSharedArrayBuffer));
    sab->length = length;
    sab->data = shared_mem;
    
    return make_pointer(sab);
}

void vm_register_atomics(VM* vm) {
    Value atomics_obj = create_object();
    object_set(atomics_obj, create_string("add", 3), create_native_function((void*)atomics_add, create_string("add", 3)));
    object_set(atomics_obj, create_string("load", 4), create_native_function((void*)atomics_load, create_string("load", 4)));
    
    object_set(vm->global_obj, create_string("Atomics", 7), atomics_obj);
    object_set(vm->global_obj, create_string("SharedArrayBuffer", 17), 
               create_native_function((void*)shared_array_buffer_constructor, create_string("SharedArrayBuffer", 17)));
}
