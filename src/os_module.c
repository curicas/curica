/**
 * @file os_module.c
 * @brief OS/POSIX Syscall Bridge for Curica
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
#include <unistd.h>
#include <sys/syscall.h>
#include <termios.h>
#include <string.h>

// Curica.os.syscall(sysno, arg1, arg2, arg3, arg4, arg5, arg6)
static Value os_syscall(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_INTEGER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("syscall expects a numeric syscall number", 40)));
        return VAL_UNDEFINED;
    }
    
    long sysno = get_integer(args[0]);
    long c_args[6] = {0, 0, 0, 0, 0, 0};
    
    for (int i = 0; i < 6 && (i + 1) < arg_count; i++) {
        Value val = args[i + 1];
        if (IS_INTEGER(val)) c_args[i] = get_integer(val);
        else if (IS_POINTER(val)) c_args[i] = (long)get_pointer(val);
        else if (IS_DOUBLE(val)) c_args[i] = (long)get_double(val);
        else c_args[i] = 0;
    }
    
    // Execute the actual syscall using Cosmopolitan libc's syscall() variadic wrapper
    long ret = syscall(sysno, c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]);
    
    return make_integer(ret);
}

// Curica.os.isatty(fd)
static Value js_os_isatty(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return make_boolean(false);
    int fd = get_integer(args[0]);
    return make_boolean(isatty(fd));
}

// Curica.os.setRawMode(fd, mode)
static Value js_os_setRawMode(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 2 || !IS_INTEGER(args[0]) || !IS_BOOLEAN(args[1])) {
        vm_throw_error(vm, create_error("TypeError", create_string("setRawMode expects (fd, boolean)", 32)));
        return VAL_UNDEFINED;
    }
    
    int fd = get_integer(args[0]);
    bool enable = get_boolean(args[1]);
    
    struct termios t;
    if (tcgetattr(fd, &t) == -1) {
        return make_boolean(false);
    }
    
    if (enable) {
        // Equivalent to cfmakeraw(&t) for basic input capture
        t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        t.c_cflag &= ~(CSIZE | PARENB);
        t.c_cflag |= CS8;
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
    } else {
        // Restore canonical mode
        t.c_iflag |= (BRKINT | ICRNL | IXON);
        t.c_lflag |= (ECHO | ICANON | ISIG | IEXTEN);
    }
    
    if (tcsetattr(fd, TCSANOW, &t) == -1) {
        return make_boolean(false);
    }
    
    return make_boolean(true);
}

// Curica.os.getenv(name)
static Value js_os_getenv(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    
    JSString* name_str = (JSString*)get_pointer(args[0]);
    char* val = getenv(name_str->data);
    if (!val) return VAL_UNDEFINED;
    return create_string(val, strlen(val));
}

Value build_os_module(VM* vm) {
    Value os_obj = create_object();
    
    object_set(os_obj, create_string("syscall", 7), create_native_function((void*)os_syscall, create_string("syscall", 7)));
    object_set(os_obj, create_string("isatty", 6), create_native_function((void*)js_os_isatty, create_string("isatty", 6)));
    object_set(os_obj, create_string("setRawMode", 10), create_native_function((void*)js_os_setRawMode, create_string("setRawMode", 10)));
    object_set(os_obj, create_string("getenv", 6), create_native_function((void*)js_os_getenv, create_string("getenv", 6)));
    
    return os_obj;
}
