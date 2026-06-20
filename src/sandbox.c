/**
 * @file sandbox.c
 * @brief Edge Compute Sandboxing Mode
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

static Value sandbox_enable(VM* vm, Value this_val, int arg_count, Value* args) {
    // Engages the secure sandbox, enforcing the strict capability-based security matrix
    // for the Curica Environment OS Kernel. This ensures the APE cannot break out
    // of its permitted VFS or networking constraints.
    vm->sandbox_mode = true;
    return VAL_TRUE;
}

static Value sandbox_isEnabled(VM* vm, Value this_val, int arg_count, Value* args) {
    // Queries the current microkernel OS state to check if the Environment is frozen.
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
