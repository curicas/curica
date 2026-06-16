/**
 * @file worker_module.c
 * @brief Web Worker / Background Thread Manager
 * 
 * Implements the core threading engine for Curica. Spawns isolated VMs in
 * background POSIX threads, establishing safe, asynchronous bidirectional
 * message channels (via JSON serialization and pipes) between threads.
 */
#include "worker_module.h"
#include "builtins.h"
#include "alloc.h"
#include "event_loop.h"
#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static _Thread_local WorkerHandle* worker_roots = NULL;

void worker_mark_gc_roots(GCTraceFn trace) {
    WorkerHandle* curr = worker_roots;
    while (curr) {
        trace(&curr->worker_obj);
        curr = curr->next;
    }
}

// Utility to read an entire file
static char* read_entire_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

// Push a JSON string into a specific queue
static void push_message(pthread_mutex_t* mutex, MessageNode** head, MessageNode** tail, char* payload) {
    MessageNode* node = malloc(sizeof(MessageNode));
    node->payload = payload;
    node->next = NULL;
    pthread_mutex_lock(mutex);
    if (*tail) {
        (*tail)->next = node;
        *tail = node;
    } else {
        *head = *tail = node;
    }
    pthread_mutex_unlock(mutex);
}

// Pop a JSON string from a specific queue
static char* pop_message(pthread_mutex_t* mutex, MessageNode** head, MessageNode** tail) {
    pthread_mutex_lock(mutex);
    MessageNode* node = *head;
    char* payload = NULL;
    if (node) {
        payload = node->payload;
        *head = node->next;
        if (!*head) *tail = NULL;
        free(node);
    }
    pthread_mutex_unlock(mutex);
    return payload;
}

/* ── Worker Thread Context ── */

// Called when the Worker thread receives an eventfd wakeup from the Main thread
static void worker_thread_pipe_cb(void* arg, int events) {
    (void)events;
    WorkerHandle* w = (WorkerHandle*)arg;

    char buf[64];
    while (read(w->worker_pipe_rx, buf, sizeof(buf)) > 0) {}

    if (w->terminate_requested) {
        extern _Thread_local EventLoop* g_event_loop;
        el_stop(g_event_loop);
        if (w->worker_io) {
            el_remove_io(g_event_loop, w->worker_io);
            free(w->worker_io);
            w->worker_io = NULL;
        }
        return;
    }

    // Drain worker_queue
    char* payload;
    while ((payload = pop_message(&w->worker_mutex, &w->worker_queue_head, &w->worker_queue_tail)) != NULL) {
        // Parse JSON payload
        Value payload_str = create_string(payload, strlen(payload));
        free(payload);

        Value parsed_val = js_json_parse(g_current_vm, VAL_UNDEFINED, 1, &payload_str);

        // Call global onmessage(parsed_val) in the Worker VM
        Value onmessage_fn = object_get(g_current_vm->global_obj, create_string("onmessage", 9));
        if (IS_POINTER(onmessage_fn)) {
            vm_call_function(g_current_vm, onmessage_fn, 1, &parsed_val);
        }
    }
}

static Value js_worker_postMessage(VM* vm, Value this_val, int arg_count, Value* args);

// Runs inside the isolated background thread
static void* worker_thread_main(void* arg) {
    WorkerHandle* w = (WorkerHandle*)arg;

    extern _Thread_local WorkerHandle* current_thread_worker_handle;
    current_thread_worker_handle = w;

    arena_init(32); 
    VM vm; vm_init(&vm);
    EventLoop loop; el_init(&loop);

    char* src = read_entire_file(w->script_path);
    if (!src) {
        fprintf(stderr, "Worker Error: Failed to read '%s'\n", w->script_path);
        goto cleanup;
    }

    CompiledProgram* prog = compile_source(src);
    free(src);
    if (!prog) goto cleanup;

    vm_load_program(&vm, prog);

    // Register Worker-side postMessage
    object_set(vm.global_obj, create_string("postMessage", 11), create_native_function((void*)js_worker_postMessage, create_string("postMessage", 11)));

    // Listen for incoming Main -> Worker messages
    extern _Thread_local EventLoop* g_event_loop;
    IOHandle* io = malloc(sizeof(IOHandle));
    io->fd = w->worker_pipe_rx;
    io->events = POLLIN;
    io->cb = worker_thread_pipe_cb;
    io->user_data = w;
    el_add_io(g_event_loop, io);
    w->worker_io = io;

    vm_run(&vm);

    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    if (w->worker_io) {
        el_remove_io(&loop, w->worker_io);
        free(w->worker_io);
        w->worker_io = NULL;
    }

    free_compiled_program(prog);
cleanup:
    vm_free(&vm);
    arena_free();
    return NULL;
}

_Thread_local WorkerHandle* current_thread_worker_handle = NULL;

static Value js_worker_postMessage(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;

    WorkerHandle* w = current_thread_worker_handle;
    if (!w) return VAL_UNDEFINED;

    Value json_str_val = js_json_stringify(vm, VAL_UNDEFINED, 1, args);
    if (!IS_POINTER(json_str_val)) return VAL_UNDEFINED;

    JSString* json_str = (JSString*)get_pointer(json_str_val);
    char* payload = strdup(json_str->data);

    push_message(&w->main_mutex, &w->main_queue_head, &w->main_queue_tail, payload);

    char byte = 1;
    write(w->main_pipe_tx, &byte, 1);

    return VAL_UNDEFINED;
}

// Main thread JS calling worker.postMessage()
static Value js_main_postMessage(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (arg_count < 1 || !IS_POINTER(this_val)) return VAL_UNDEFINED;
    
    // We need to retrieve the WorkerHandle* associated with this JS worker object.
    // We can store the pointer as a number inside a hidden property `_handle`.
    Value handle_val = object_get(this_val, create_string("_handle", 7));
    if (!IS_DOUBLE(handle_val)) return VAL_UNDEFINED;
    
    uintptr_t ptr = (uintptr_t)get_double(handle_val);
    WorkerHandle* w = (WorkerHandle*)ptr;

    // Serialize payload to JSON
    Value json_str_val = js_json_stringify(vm, VAL_UNDEFINED, 1, args);
    if (!IS_POINTER(json_str_val)) return VAL_UNDEFINED;
    JSString* json_str = (JSString*)get_pointer(json_str_val);
    char* payload = strdup(json_str->data);

    // Push to the Worker queue (Main -> Worker)
    push_message(&w->worker_mutex, &w->worker_queue_head, &w->worker_queue_tail, payload);

    // Wake up worker thread
    char byte = 1;
    write(w->worker_pipe_tx, &byte, 1);

    return VAL_UNDEFINED;
}

/* ── Main Thread Context ── */

// Called when the Main thread receives an eventfd wakeup from the Worker
static void main_thread_pipe_cb(void* arg, int events) {
    (void)events;
    WorkerHandle* w = (WorkerHandle*)arg;

    char buf[64];
    while (read(w->main_pipe_rx, buf, sizeof(buf)) > 0) {}

    w->in_callback = true;
    // Drain main_queue
    char* payload;
    while ((payload = pop_message(&w->main_mutex, &w->main_queue_head, &w->main_queue_tail)) != NULL) {
        // Parse JSON payload
        Value payload_str = create_string(payload, strlen(payload));
        free(payload);

        Value parsed_val = js_json_parse(g_current_vm, VAL_UNDEFINED, 1, &payload_str);
        vm_push_root(g_current_vm, parsed_val);

        Value key = create_string("onmessage", 9);
        Value onmessage_fn = object_get(w->worker_obj, key);
        
        if (IS_POINTER(onmessage_fn)) {
            vm_push_root(g_current_vm, onmessage_fn);
            vm_call_function(g_current_vm, onmessage_fn, 1, &parsed_val);
            vm_pop_root(g_current_vm);
        }
        vm_pop_root(g_current_vm);

        if (w->terminate_requested) break;
    }
    w->in_callback = false;
    if (w->needs_free) {
        free(w);
    }
}

static Value js_worker_terminate(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;

    Value handle_val = object_get(this_val, create_string("_handle", 7));
    if (!IS_DOUBLE(handle_val)) return VAL_UNDEFINED;

    uintptr_t ptr = (uintptr_t)get_double(handle_val);
    WorkerHandle* w = (WorkerHandle*)ptr;
    if (!w) return VAL_UNDEFINED;

    // 1. Set termination flag
    w->terminate_requested = true;

    // 2. Wake up the worker thread so it can exit its event loop
    char byte = 1;
    write(w->worker_pipe_tx, &byte, 1);

    // 3. Wait for the worker thread to exit
    pthread_join(w->thread, NULL);

    // 4. Remove and free main thread's pipe listener
    if (w->main_io) {
        extern _Thread_local EventLoop* g_event_loop;
        el_remove_io(g_event_loop, w->main_io);
        free(w->main_io);
        w->main_io = NULL;
    }

    // 5. Close all descriptors
    close(w->worker_pipe_rx);
    close(w->worker_pipe_tx);
    close(w->main_pipe_rx);
    close(w->main_pipe_tx);

    char* payload;
    while ((payload = pop_message(&w->worker_mutex, &w->worker_queue_head, &w->worker_queue_tail)) != NULL) {
        free(payload);
    }
    while ((payload = pop_message(&w->main_mutex, &w->main_queue_head, &w->main_queue_tail)) != NULL) {
        free(payload);
    }

    // 6. Free queues messages
    pthread_mutex_destroy(&w->worker_mutex);
    pthread_mutex_destroy(&w->main_mutex);

    free(w->script_path);

    // Remove from roots
    if (worker_roots == w) {
        worker_roots = w->next;
    } else {
        WorkerHandle* prev = worker_roots;
        while (prev && prev->next != w) prev = prev->next;
        if (prev) prev->next = w->next;
    }

    if (w->in_callback) {
        w->needs_free = true;
    } else {
        free(w);
    }

    // Clear the _handle property to prevent double termination
    object_set(this_val, create_string("_handle", 7), VAL_UNDEFINED);

    return VAL_UNDEFINED;
}

static Value js_worker_on(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    JSString* ev_name = (JSString*)get_pointer(args[0]);
    if (strcmp(ev_name->data, "message") == 0) {
        vm_push_root(g_current_vm, this_val);
        vm_push_root(g_current_vm, args[1]); // Protect the closure!
        Value key = create_string("onmessage", 9);
        vm_push_root(g_current_vm, key);
        
        Value updated_this = g_current_vm->gc_roots[g_current_vm->gc_root_count - 3];
        Value updated_fn = g_current_vm->gc_roots[g_current_vm->gc_root_count - 2];
        object_set(updated_this, key, updated_fn);
        
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
    }
    return this_val;
}

// Constructor: new Worker(filename)
static Value js_worker_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;

    JSString* filename = (JSString*)get_pointer(args[0]);
    
    WorkerHandle* w = malloc(sizeof(WorkerHandle));
    w->script_path = strdup(filename->data);
    w->worker_obj = this_val;

    int wp[2], mp[2];
    pipe(wp); pipe(mp);
    set_nonblock(wp[0]); set_nonblock(wp[1]);
    set_nonblock(mp[0]); set_nonblock(mp[1]);

    w->worker_pipe_rx = wp[0];
    w->worker_pipe_tx = wp[1];
    w->main_pipe_rx = mp[0];
    w->main_pipe_tx = mp[1];

    pthread_mutex_init(&w->worker_mutex, NULL);
    pthread_mutex_init(&w->main_mutex, NULL);
    w->worker_queue_head = w->worker_queue_tail = NULL;
    w->main_queue_head = w->main_queue_tail = NULL;
    w->main_io = NULL;
    w->worker_io = NULL;
    w->worker_io = NULL;
    w->terminate_requested = false;
    w->in_callback = false;
    w->needs_free = false;

    // Add to roots
    w->next = worker_roots;
    worker_roots = w;

    vm_push_root(g_current_vm, w->worker_obj);

    // Bind `postMessage` and `terminate` to this Worker instance
    Value handle_name = create_string("_handle", 7);
    vm_push_root(g_current_vm, handle_name);
    object_set(w->worker_obj, handle_name, make_double((uintptr_t)w));
    vm_pop_root(g_current_vm);
    
    Value pm_name = create_string("postMessage", 11);
    vm_push_root(g_current_vm, pm_name);
    Value pm_fn = create_bound_native_function((void*)js_main_postMessage, pm_name, w->worker_obj);
    vm_push_root(g_current_vm, pm_fn);
    object_set(w->worker_obj, pm_name, pm_fn);
    vm_pop_root(g_current_vm);
    vm_pop_root(g_current_vm);

    Value term_name = create_string("terminate", 9);
    vm_push_root(g_current_vm, term_name);
    Value term_fn = create_bound_native_function((void*)js_worker_terminate, term_name, w->worker_obj);
    vm_push_root(g_current_vm, term_fn);
    object_set(w->worker_obj, term_name, term_fn);
    vm_pop_root(g_current_vm);
    vm_pop_root(g_current_vm);

    Value on_name = create_string("on", 2);
    vm_push_root(g_current_vm, on_name);
    Value on_fn = create_bound_native_function((void*)js_worker_on, on_name, w->worker_obj);
    vm_push_root(g_current_vm, on_fn);
    object_set(w->worker_obj, on_name, on_fn);
    vm_pop_root(g_current_vm);
    vm_pop_root(g_current_vm);

    // Update worker_obj from root
    w->worker_obj = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
    vm_pop_root(g_current_vm);

    // Register pipe listener on Main Event Loop
    extern _Thread_local EventLoop* g_event_loop;
    IOHandle* io = malloc(sizeof(IOHandle));
    io->fd = w->main_pipe_rx;
    io->events = POLLIN;
    io->cb = main_thread_pipe_cb;
    io->user_data = w;
    el_add_io(g_event_loop, io);
    w->main_io = io;

    pthread_create(&w->thread, NULL, worker_thread_main, w);

    return w->worker_obj;
}

Value build_worker_constructor(VM* vm) {
    (void)vm;
    Value worker_ctor = create_native_function((void*)js_worker_constructor, create_string("Worker", 6));
    // Set prototype etc.
    return worker_ctor;
}
