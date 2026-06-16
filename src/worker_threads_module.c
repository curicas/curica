/**
 * @file worker_threads_module.c
 * @brief Node.js `worker_threads` API Parity
 * 
 * Provides backwards compatibility for the Node.js ecosystem threading model
 * by mapping it to the underlying web Worker engine.
 */

#include "vm.h"
#include "alloc.h"
#include "worker_module.h"
#include <string.h>

// new worker_threads.MessageChannel()
static Value wt_message_channel(VM* vm, Value this_val, int arg_count, Value* args) {
    Value port1 = create_object();
    Value port2 = create_object();
    
    Value channel = create_object();
    object_set(channel, create_string("port1", 5), port1);
    object_set(channel, create_string("port2", 5), port2);
    
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
