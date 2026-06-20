/**
 * @file wasm_ext.c
 * @brief WebAssembly SIMD & Thread Extensions
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

#include "vm.h"
#include "alloc.h"

// WebAssembly.SIMD.isSupported()
static Value wasm_simd_isSupported(VM* vm, Value this_val, int arg_count, Value* args) {
    return VAL_TRUE;
}

// WebAssembly.threads.isSupported()
static Value wasm_threads_isSupported(VM* vm, Value this_val, int arg_count, Value* args) {
    return VAL_FALSE;
}

void vm_register_wasm_extensions(VM* vm) {
    Value wasm_obj = object_get(vm->global_obj, create_string("WebAssembly", 11));
    if (!IS_POINTER(wasm_obj)) {
        wasm_obj = create_object();
        object_set(vm->global_obj, create_string("WebAssembly", 11), wasm_obj);
    }
    
    Value simd_obj = create_object();
    object_set(simd_obj, create_string("isSupported", 11), create_native_function((void*)wasm_simd_isSupported, create_string("isSupported", 11)));
    object_set(wasm_obj, create_string("SIMD", 4), simd_obj);
    
    Value threads_obj = create_object();
    object_set(threads_obj, create_string("isSupported", 11), create_native_function((void*)wasm_threads_isSupported, create_string("isSupported", 11)));
    object_set(wasm_obj, create_string("threads", 7), threads_obj);
}
