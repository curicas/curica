/**
 * @file worker_threads_module.c
 * @brief Node.js `worker_threads` API Parity
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
#include "worker_module.h"
#include "event_loop.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

// Queue node
typedef struct WTMessageNode {
    char* payload;
    struct WTMessageNode* next;
} WTMessageNode;

// Shared Message Queue
typedef struct {
    int ref_count;
    pthread_mutex_t mutex;
    WTMessageNode* head;
    WTMessageNode* tail;
    int pipe_rx;
    int pipe_tx;
} SharedMessageQueue;

// Native Message Port Wrapper
typedef struct {
    SharedMessageQueue* rx;
    SharedMessageQueue* tx;
    IOHandle* io_handle;
    Value js_port_obj;
} NativeMessagePort;

static void wt_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static SharedMessageQueue* create_shared_queue(void) {
    SharedMessageQueue* q = malloc(sizeof(SharedMessageQueue));
    q->ref_count = 2; // Each queue has a sender and a receiver
    pthread_mutex_init(&q->mutex, NULL);
    q->head = q->tail = NULL;
    int p[2];
    pipe(p);
    wt_set_nonblock(p[0]);
    wt_set_nonblock(p[1]);
    q->pipe_rx = p[0];
    q->pipe_tx = p[1];
    return q;
}

static void free_shared_queue(SharedMessageQueue* q) {
    pthread_mutex_lock(&q->mutex);
    q->ref_count--;
    int count = q->ref_count;
    pthread_mutex_unlock(&q->mutex);
    if (count == 0) {
        close(q->pipe_rx);
        close(q->pipe_tx);
        pthread_mutex_destroy(&q->mutex);
        WTMessageNode* head = q->head;
        while (head) {
            WTMessageNode* next = head->next;
            free(head->payload);
            free(head);
            head = next;
        }
        free(q);
    }
}

static Value mp_post_message(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1) return VAL_UNDEFINED;
    
    Value native_port_val = object_get(this_val, create_string("_nativePort", 11));
    if (!IS_POINTER(native_port_val)) return VAL_UNDEFINED;
    
    NativeMessagePort* port = (NativeMessagePort*)get_pointer(native_port_val);
    if (!port->tx) return VAL_UNDEFINED; // Closed
    
    extern Value js_json_stringify(VM* vm, Value this_val, int arg_count, Value* args);
    Value json_str_val = js_json_stringify(vm, VAL_UNDEFINED, 1, &args[0]);
    if (!IS_POINTER(json_str_val)) return VAL_UNDEFINED;
    
    JSString* json_str = (JSString*)get_pointer(json_str_val);
    char* payload = strdup(json_str->data);
    
    WTMessageNode* node = malloc(sizeof(WTMessageNode));
    node->payload = payload;
    node->next = NULL;
    
    pthread_mutex_lock(&port->tx->mutex);
    if (port->tx->tail) {
        port->tx->tail->next = node;
        port->tx->tail = node;
    } else {
        port->tx->head = port->tx->tail = node;
    }
    pthread_mutex_unlock(&port->tx->mutex);
    
    char wakeup = '!';
    write(port->tx->pipe_tx, &wakeup, 1);
    
    return VAL_UNDEFINED;
}

static void mp_pipe_cb(void* user_data, int events) {
    (void)events;
    NativeMessagePort* port = (NativeMessagePort*)user_data;
    
    char buf[64];
    while (read(port->rx->pipe_rx, buf, sizeof(buf)) > 0) {}
    
    extern _Thread_local VM* g_current_vm;
    VM* vm = g_current_vm;
    
    pthread_mutex_lock(&port->rx->mutex);
    WTMessageNode* head = port->rx->head;
    port->rx->head = port->rx->tail = NULL;
    pthread_mutex_unlock(&port->rx->mutex);
    
    while (head) {
        WTMessageNode* node = head;
        head = head->next;
        
        Value payload_str = create_string(node->payload, strlen(node->payload));
        free(node->payload);
        free(node);
        
        extern Value js_json_parse(VM* vm, Value this_val, int arg_count, Value* args);
        Value parsed_val = js_json_parse(vm, VAL_UNDEFINED, 1, &payload_str);
        
        Value onmessage_fn = object_get(port->js_port_obj, create_string("onmessage", 9));
        if (IS_POINTER(onmessage_fn)) {
            extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
            vm_call_function(vm, onmessage_fn, 1, &parsed_val);
        }
    }
}

static Value mp_on(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    JSString* ev_name = (JSString*)get_pointer(args[0]);
    if (strcmp(ev_name->data, "message") == 0) {
        object_set(this_val, create_string("onmessage", 9), args[1]);
        
        Value native_port_val = object_get(this_val, create_string("_nativePort", 11));
        if (IS_POINTER(native_port_val)) {
            NativeMessagePort* port = (NativeMessagePort*)get_pointer(native_port_val);
            if (!port->io_handle && port->rx) {
                port->io_handle = malloc(sizeof(IOHandle));
                port->io_handle->fd = port->rx->pipe_rx;
                port->io_handle->events = POLLIN;
                port->io_handle->cb = mp_pipe_cb;
                port->io_handle->user_data = port;
                port->io_handle->active = true;
                
                extern _Thread_local EventLoop* g_event_loop;
                el_add_io(g_event_loop, port->io_handle);
            }
        }
    }
    return this_val;
}

static Value mp_close(VM* vm, Value this_val, int arg_count, Value* args) {
    Value native_port_val = object_get(this_val, create_string("_nativePort", 11));
    if (IS_POINTER(native_port_val)) {
        NativeMessagePort* port = (NativeMessagePort*)get_pointer(native_port_val);
        if (port->io_handle) {
            extern _Thread_local EventLoop* g_event_loop;
            el_remove_io(g_event_loop, port->io_handle);
            free(port->io_handle);
            port->io_handle = NULL;
        }
        if (port->rx) {
            free_shared_queue(port->rx);
            port->rx = NULL;
        }
        if (port->tx) {
            free_shared_queue(port->tx);
            port->tx = NULL;
        }
    }
    return VAL_UNDEFINED;
}

static Value create_message_port(VM* vm, SharedMessageQueue* rx, SharedMessageQueue* tx) {
    Value port = create_object();
    vm_push_root(vm, port);
    
    NativeMessagePort* np = malloc(sizeof(NativeMessagePort));
    np->rx = rx;
    np->tx = tx;
    np->io_handle = NULL;
    np->js_port_obj = port;
    
    object_set(port, create_string("_nativePort", 11), make_pointer(np));
    object_set(port, create_string("postMessage", 11), create_native_function((void*)mp_post_message, create_string("postMessage", 11)));
    object_set(port, create_string("on", 2), create_native_function((void*)mp_on, create_string("on", 2)));
    object_set(port, create_string("close", 5), create_native_function((void*)mp_close, create_string("close", 5)));
    
    vm_pop_root(vm);
    return port;
}

// new worker_threads.MessageChannel()
static Value wt_message_channel(VM* vm, Value this_val, int arg_count, Value* args) {
    SharedMessageQueue* q1 = create_shared_queue(); // port2 -> port1
    SharedMessageQueue* q2 = create_shared_queue(); // port1 -> port2
    
    Value port1 = create_message_port(vm, q1, q2);
    vm_push_root(vm, port1);
    
    Value port2 = create_message_port(vm, q2, q1);
    vm_push_root(vm, port2);
    
    Value channel = create_object();
    object_set(channel, create_string("port1", 5), port1);
    object_set(channel, create_string("port2", 5), port2);
    
    vm_pop_root(vm);
    vm_pop_root(vm);
    
    return channel;
}

static Value wt_parent_on(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    JSString* ev_name = (JSString*)get_pointer(args[0]);
    if (strcmp(ev_name->data, "message") == 0) {
        vm_push_root(vm, args[1]); // Protect the closure!
        Value key = create_string("onmessage", 9);
        vm_push_root(vm, key);
        
        Value updated_fn = vm->gc_roots[vm->gc_root_count - 2];
        object_set(vm->global_obj, key, updated_fn);
        
        vm_pop_root(vm);
        vm_pop_root(vm);
    }
    return this_val;
}

// parentPort.postMessage proxy
static Value wt_parent_post_message(VM* vm, Value this_val, int arg_count, Value* args) {
    Value global_post = object_get(vm->global_obj, create_string("postMessage", 11));
    if (IS_POINTER(global_post)) {
        extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
        return vm_call_function(vm, global_post, arg_count, args);
    }
    return VAL_UNDEFINED;
}

void vm_register_worker_threads(VM* vm) {
    Value wt_obj = create_object();
    vm_push_root(vm, wt_obj);
    
    // 1. Map Node's Worker to the Web Worker implementation
    Value worker_ctor = build_worker_constructor(vm);
    vm_push_root(vm, worker_ctor);
    Value worker_name = create_string("Worker", 6);
    vm_push_root(vm, worker_name);
    
    Value updated_wt_obj = vm->gc_roots[vm->gc_root_count - 3];
    object_set(updated_wt_obj, worker_name, worker_ctor);
    
    vm_pop_root(vm);
    vm_pop_root(vm);
    
    // 2. MessageChannel stub
    Value mc_name = create_string("MessageChannel", 14);
    vm_push_root(vm, mc_name);
    Value mc_fn = create_native_function((void*)wt_message_channel, mc_name);
    vm_push_root(vm, mc_fn);
    object_set(vm->gc_roots[vm->gc_root_count - 3], mc_name, mc_fn);
    vm_pop_root(vm);
    vm_pop_root(vm);
    
    // 3. Node.js thread context variables (parentPort, isMainThread)
    if (current_thread_worker_handle != NULL) {
        // We are inside a worker thread
        Value is_main_name = create_string("isMainThread", 12);
        vm_push_root(vm, is_main_name);
        object_set(vm->gc_roots[vm->gc_root_count - 2], is_main_name, VAL_FALSE);
        vm_pop_root(vm);
        
        Value parentPort = create_object();
        vm_push_root(vm, parentPort);
        
        Value pm_name = create_string("postMessage", 11);
        vm_push_root(vm, pm_name);
        Value pm_fn = create_native_function((void*)wt_parent_post_message, pm_name);
        vm_push_root(vm, pm_fn);
        object_set(vm->gc_roots[vm->gc_root_count - 3], pm_name, pm_fn);
        vm_pop_root(vm);
        vm_pop_root(vm);
        
        Value on_name = create_string("on", 2);
        vm_push_root(vm, on_name);
        Value on_fn = create_native_function((void*)wt_parent_on, on_name);
        vm_push_root(vm, on_fn);
        object_set(vm->gc_roots[vm->gc_root_count - 3], on_name, on_fn);
        vm_pop_root(vm);
        vm_pop_root(vm);
        
        Value pp_name = create_string("parentPort", 10);
        vm_push_root(vm, pp_name);
        object_set(vm->gc_roots[vm->gc_root_count - 3], pp_name, vm->gc_roots[vm->gc_root_count - 2]);
        vm_pop_root(vm);
        vm_pop_root(vm);
    } else {
        // We are on the main thread
        Value is_main_name = create_string("isMainThread", 12);
        vm_push_root(vm, is_main_name);
        object_set(vm->gc_roots[vm->gc_root_count - 2], is_main_name, VAL_TRUE);
        vm_pop_root(vm);
        
        Value pp_name = create_string("parentPort", 10);
        vm_push_root(vm, pp_name);
        object_set(vm->gc_roots[vm->gc_root_count - 2], pp_name, VAL_NULL);
        vm_pop_root(vm);
    }
    
    // Register globally as `worker_threads`
    Value wt_name = create_string("worker_threads", 14);
    vm_push_root(vm, wt_name);
    object_set(vm->global_obj, wt_name, vm->gc_roots[vm->gc_root_count - 2]);
    vm_pop_root(vm);
    
    vm_pop_root(vm); // wt_obj
}
