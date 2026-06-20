/**
 * @file child_process_module.c
 * @brief Process spawning subsystem for the Curica Environment OS Kernel.
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
#include "child_process_module.h"
#include "vfs_module.h"
#include "alloc.h"
#include "event_loop.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define ENFORCE_RUN_ACCESS(vm) \
    if (!(vm)->allow_run) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-run access", 27))); \
        return VAL_UNDEFINED; \
    }

static Value js_child_process_spawn(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_RUN_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    JSString* cmd_str = (JSString*)get_pointer(args[0]);
    char* cmd = strdup(cmd_str->data);
    
    char** exec_args = malloc(2 * sizeof(char*));
    exec_args[0] = cmd;
    exec_args[1] = NULL;
    int exec_args_count = 1;
    
    if (arg_count >= 2 && IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(args[1]);
            exec_args = realloc(exec_args, (arr->length + 2) * sizeof(char*));
            for (uint32_t i = 0; i < arr->length; i++) {
                if (IS_POINTER(arr->elements[i])) {
                    JSString* arg_str = (JSString*)get_pointer(arr->elements[i]);
                    exec_args[exec_args_count++] = strdup(arg_str->data);
                } else {
                    exec_args[exec_args_count++] = strdup("");
                }
            }
            exec_args[exec_args_count] = NULL;
        }
    }
    
    char** vfs_dirs = NULL;
    int vfs_dirs_count = 0;
    
    if (arg_count >= 3 && IS_POINTER(args[2])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[2]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(args[2]);
            vfs_dirs_count = arr->length;
            if (vfs_dirs_count > 0) {
                vfs_dirs = malloc(vfs_dirs_count * sizeof(char*));
                for (uint32_t i = 0; i < arr->length; i++) {
                    if (IS_POINTER(arr->elements[i])) {
                        JSString* str = (JSString*)get_pointer(arr->elements[i]);
                        vfs_dirs[i] = strdup(str->data);
                    } else {
                        vfs_dirs[i] = strdup("");
                    }
                }
            }
        }
    }
    
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        return VAL_UNDEFINED;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        return VAL_UNDEFINED;
    }
    
    if (pid == 0) {
        // Child
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        char wasm_path[256];
        snprintf(wasm_path, sizeof(wasm_path), "/bin/%s.wasm", cmd);
        struct stat st;
        if (vfs_stat(wasm_path, &st) == 0) {
            extern int wasm_module_execute_cli(int argc, char** argv, char** custom_vfs_dirs, int custom_vfs_count);
            int ret = wasm_module_execute_cli(exec_args_count, exec_args, vfs_dirs, vfs_dirs_count);
            exit(ret);
        } else {
            execvp(cmd, exec_args);
        }
        // If execvp or wasm fails
        exit(1);
    }
    
    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    for (int i = 0; i < exec_args_count; i++) free(exec_args[i]);
    free(exec_args);
    
    for (int i = 0; i < vfs_dirs_count; i++) free(vfs_dirs[i]);
    if (vfs_dirs) free(vfs_dirs);
    
    Value ret_obj = create_object();
    object_set(ret_obj, create_string("pid", 3), make_integer(pid));
    object_set(ret_obj, create_string("stdinFd", 7), make_integer(stdin_pipe[1]));
    object_set(ret_obj, create_string("stdoutFd", 8), make_integer(stdout_pipe[0]));
    object_set(ret_obj, create_string("stderrFd", 8), make_integer(stderr_pipe[0]));
    
    return ret_obj;
}

static Value js_child_process_write(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 2 || (!IS_INTEGER(args[0]) && !IS_DOUBLE(args[0]))) return VAL_UNDEFINED;
    
    int fd = (int)(IS_INTEGER(args[0]) ? get_integer(args[0]) : get_double(args[0]));
    
    if (IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* str = (JSString*)get_pointer(args[1]);
            write(fd, str->data, str->length);
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(args[1]);
            write(fd, buf->data, buf->length);
        }
    }
    
    if (arg_count >= 3 && IS_POINTER(args[2])) {
        vm_enqueue_microtask(vm, args[2], VAL_UNDEFINED);
    }
    
    return VAL_UNDEFINED;
}

// We will use a dedicated thread to read from stdout/stderr and emit chunks, 
// or register an IOHandle. Since event_loop.c provides IOHandle, we can use it!

typedef struct {
    IOHandle io;
    Value cb_val;
} ChildIO;

static void child_read_cb(void* data, int revents) {
    ChildIO* child_io = (ChildIO*)data;
    int fd = child_io->io.fd;
    Value cb_val = child_io->cb_val;
    VM* vm = g_current_vm;
    
    if (revents & POLLIN) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            Value chunk = create_buffer(n, false);
            JSBuffer* jsbuf = (JSBuffer*)get_pointer(chunk);
            memcpy(jsbuf->data, buf, n);
            
            Value cb_args[1] = { chunk };
            extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
            vm_call_function(vm, cb_val, 1, cb_args);
        } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
            // EOF
            Value cb_args[1] = { VAL_NULL };
            extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
            vm_call_function(vm, cb_val, 1, cb_args);
            el_remove_io(g_event_loop, &child_io->io);
            close(fd);
            free(child_io);
        }
    } else if (revents & (POLLHUP | POLLERR)) {
        Value cb_args[1] = { VAL_NULL };
        extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
        vm_call_function(vm, cb_val, 1, cb_args);
        el_remove_io(g_event_loop, &child_io->io);
        close(fd);
        free(child_io);
    }
}

static Value js_child_process_read_start(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 2 || (!IS_INTEGER(args[0]) && !IS_DOUBLE(args[0])) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    int fd = (int)(IS_INTEGER(args[0]) ? get_integer(args[0]) : get_double(args[0]));
    Value cb = args[1];
    
    ChildIO* child_io = malloc(sizeof(ChildIO));
    child_io->io.fd = fd;
    child_io->io.cb = child_read_cb;
    child_io->io.user_data = child_io;
    child_io->io.events = POLLIN;
    child_io->cb_val = cb;
    
    el_add_io(g_event_loop, &child_io->io);
    return VAL_UNDEFINED;
}

#include "thread_pool.h"

struct WaitData {
    pid_t pid;
    Value cb;
    WorkItem tp_item;
};

static void wait_thread(void* arg, int* status_out) {
    struct WaitData* data = (struct WaitData*)arg;
    int status = 0;
    waitpid(data->pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *status_out = code;
}

static void wait_done(struct VM* vm, void* arg, int status) {
    (void)vm;
    struct WaitData* data = (struct WaitData*)arg;
    Value cb_args[1] = { make_integer(status) };
    extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
    vm_call_function(g_current_vm, data->cb, 1, cb_args);
    free(data);
}

static void wait_gc_mark(void* arg, GCTraceFn trace) {
    struct WaitData* data = (struct WaitData*)arg;
    trace(&data->cb);
}

static Value js_child_process_wait(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 2 || (!IS_INTEGER(args[0]) && !IS_DOUBLE(args[0])) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    pid_t pid = (pid_t)(IS_INTEGER(args[0]) ? get_integer(args[0]) : get_double(args[0]));
    Value cb = args[1];
    
    struct WaitData* data = malloc(sizeof(struct WaitData));
    data->pid = pid;
    data->cb = cb;
    data->tp_item.work = wait_thread;
    data->tp_item.after = wait_done;
    data->tp_item.gc_mark = wait_gc_mark;
    data->tp_item.data = data;
    data->tp_item.status = 0;
    data->tp_item.next = NULL;
    
    tp_submit(&data->tp_item);
    
    return VAL_UNDEFINED;
}

static Value js_child_process_execSync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_RUN_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    JSString* cmd_str = (JSString*)get_pointer(args[0]);
    int ret = system(cmd_str->data);
    return make_integer(ret);
}

Value build_child_process_module(VM* vm) {
    (void)vm;
    Value cp_obj = create_object();
    object_set(cp_obj, create_string("__child_process_spawn", 21), create_native_function((void*)js_child_process_spawn, create_string("__child_process_spawn", 21)));
    object_set(cp_obj, create_string("__child_process_write", 21), create_native_function((void*)js_child_process_write, create_string("__child_process_write", 21)));
    object_set(cp_obj, create_string("__child_process_read_start", 26), create_native_function((void*)js_child_process_read_start, create_string("__child_process_read_start", 26)));
    object_set(cp_obj, create_string("__child_process_wait", 20), create_native_function((void*)js_child_process_wait, create_string("__child_process_wait", 20)));
    object_set(cp_obj, create_string("execSync", 8), create_native_function((void*)js_child_process_execSync, create_string("execSync", 8)));
    return cp_obj;
}

void child_process_mark_gc_roots(GCTraceFn trace) {
    if (!g_event_loop) return;
    
    for (IOHandle* h = g_event_loop->io_handles; h; h = h->next) {
        if (h->cb == child_read_cb) {
            ChildIO* child_io = (ChildIO*)h->user_data;
            trace(&child_io->cb_val);
        }
    }
}

