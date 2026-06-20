/**
 * @file atomics.c
 * @brief SharedArrayBuffer and Atomics Implementation
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
#include <stdatomic.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define MAX_WAITERS 256

typedef struct {
    void* addr;
    pthread_cond_t cond;
    int waiting;
} WaitNode;

static pthread_mutex_t atomics_mutex = PTHREAD_MUTEX_INITIALIZER;
static WaitNode wait_queue[MAX_WAITERS];

static WaitNode* get_or_create_wait_node(void* addr) {
    WaitNode* empty = NULL;
    for (int i = 0; i < MAX_WAITERS; i++) {
        if (wait_queue[i].addr == addr) return &wait_queue[i];
        if (wait_queue[i].addr == NULL && !empty) empty = &wait_queue[i];
    }
    if (empty) {
        empty->addr = addr;
        empty->waiting = 0;
        pthread_cond_init(&empty->cond, NULL);
        return empty;
    }
    return NULL;
}

static void release_wait_node(WaitNode* node) {
    if (node->waiting <= 0) {
        pthread_cond_destroy(&node->cond);
        node->addr = NULL;
    }
}

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

// Atomics.wait(typedArray, index, value, timeout)
static Value atomics_wait(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 3 || !IS_POINTER(args[0]) || !IS_INTEGER(args[1]) || !IS_INTEGER(args[2])) {
        return VAL_UNDEFINED;
    }
    JSSharedArrayBuffer* sab = (JSSharedArrayBuffer*)get_pointer(args[0]);
    int index = get_integer(args[1]);
    int32_t expected_value = get_integer(args[2]);
    double timeout = -1;
    if (arg_count >= 4) {
        if (IS_DOUBLE(args[3])) timeout = get_double(args[3]);
        else if (IS_INTEGER(args[3])) timeout = (double)get_integer(args[3]);
    }
    
    if (index < 0 || index + 3 >= (int)sab->length) return VAL_UNDEFINED;
    
    _Atomic(int32_t)* target = (_Atomic(int32_t)*)&sab->data[index];
    
    pthread_mutex_lock(&atomics_mutex);
    
    if (atomic_load(target) != expected_value) {
        pthread_mutex_unlock(&atomics_mutex);
        return create_string("not-equal", 9);
    }
    
    WaitNode* node = get_or_create_wait_node((void*)target);
    if (!node) {
        pthread_mutex_unlock(&atomics_mutex);
        return VAL_UNDEFINED; // Out of wait nodes
    }
    
    node->waiting++;
    
    int rc = 0;
    if (timeout >= 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t nsec = ts.tv_nsec + (uint64_t)(timeout * 1000000.0);
        ts.tv_sec += nsec / 1000000000;
        ts.tv_nsec = nsec % 1000000000;
        rc = pthread_cond_timedwait(&node->cond, &atomics_mutex, &ts);
    } else {
        rc = pthread_cond_wait(&node->cond, &atomics_mutex);
    }
    
    node->waiting--;
    release_wait_node(node);
    pthread_mutex_unlock(&atomics_mutex);
    
    if (rc == ETIMEDOUT) return create_string("timed-out", 9);
    return create_string("ok", 2);
}

// Atomics.notify(typedArray, index, count)
static Value atomics_notify(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_INTEGER(args[1])) {
        return make_integer(0);
    }
    JSSharedArrayBuffer* sab = (JSSharedArrayBuffer*)get_pointer(args[0]);
    int index = get_integer(args[1]);
    int count = -1; // Infinity
    if (arg_count >= 3 && IS_INTEGER(args[2])) count = get_integer(args[2]);
    
    if (index < 0 || index + 3 >= (int)sab->length) return make_integer(0);
    
    _Atomic(int32_t)* target = (_Atomic(int32_t)*)&sab->data[index];
    
    pthread_mutex_lock(&atomics_mutex);
    
    WaitNode* node = NULL;
    for (int i = 0; i < MAX_WAITERS; i++) {
        if (wait_queue[i].addr == (void*)target) {
            node = &wait_queue[i];
            break;
        }
    }
    
    int woken = 0;
    if (node && node->waiting > 0) {
        if (count == -1 || count >= node->waiting) {
            pthread_cond_broadcast(&node->cond);
            woken = node->waiting;
        } else {
            for (int i = 0; i < count; i++) {
                pthread_cond_signal(&node->cond);
            }
            woken = count;
        }
    }
    
    pthread_mutex_unlock(&atomics_mutex);
    return make_integer(woken);
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
    object_set(atomics_obj, create_string("wait", 4), create_native_function((void*)atomics_wait, create_string("wait", 4)));
    object_set(atomics_obj, create_string("notify", 6), create_native_function((void*)atomics_notify, create_string("notify", 6)));
    
    object_set(vm->global_obj, create_string("Atomics", 7), atomics_obj);
    object_set(vm->global_obj, create_string("SharedArrayBuffer", 17), 
               create_native_function((void*)shared_array_buffer_constructor, create_string("SharedArrayBuffer", 17)));
}
