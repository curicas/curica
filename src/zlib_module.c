#include "zlib_module.h"
#include "alloc.h"
#include "thread_pool.h"
#include "third_party/zlib/zlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file zlib_module.c
 * @brief Asynchronous Compression Native Bindings
 * 
 * This module wraps Cosmopolitan libc's built-in zlib implementation.
 * To prevent blocking the main JS event loop during heavy compression workloads,
 * deflate and inflate operations are dispatched to the POSIX thread pool
 * (via `tp_submit`).
 */

typedef struct ZlibTask {
    VM* vm;
    char* input_data;
    size_t input_len;
    char* output_data;
    size_t output_len;
    Value callback;
    int is_deflate; // 1 for deflate, 0 for inflate
} ZlibTask;

static void zlib_task_gc_mark(void* data, GCTraceFn trace) {
    ZlibTask* task = (ZlibTask*)data;
    trace(&task->callback);
}

static void zlib_work_fn(void* data, int* status_out) {
    ZlibTask* task = (ZlibTask*)data;
    
    // Initial guess for output size
    size_t out_capacity = task->is_deflate ? compressBound(task->input_len) : task->input_len * 4;
    if (out_capacity < 1024) out_capacity = 1024;
    
    task->output_data = malloc(out_capacity);
    uLongf destLen = out_capacity;
    
    int res;
    if (task->is_deflate) {
        res = compress((Bytef*)task->output_data, &destLen, (const Bytef*)task->input_data, task->input_len);
    } else {
        // For uncompress, we might need to loop if the buffer isn't big enough
        while (1) {
            res = uncompress((Bytef*)task->output_data, &destLen, (const Bytef*)task->input_data, task->input_len);
            if (res == Z_BUF_ERROR) {
                out_capacity *= 2;
                task->output_data = realloc(task->output_data, out_capacity);
                destLen = out_capacity;
            } else {
                break;
            }
        }
    }
    
    if (res == Z_OK) {
        task->output_len = destLen;
        *status_out = 0;
    } else {
        *status_out = res;
    }
}

static void zlib_after_fn(VM* vm, void* data, int status) {
    ZlibTask* task = (ZlibTask*)data;
    
    if (IS_POINTER(task->callback)) {
        Value err = VAL_NULL;
        Value res = VAL_UNDEFINED;
        
        if (status == 0) {
            res = create_buffer_from_string(task->output_data, task->output_len, "utf8");
        } else {
            char err_msg[64];
            snprintf(err_msg, sizeof(err_msg), "Zlib error: %d", status);
            err = create_error("ZlibError", create_string(err_msg, strlen(err_msg)));
        }
        
        Value argv[2] = { err, res };
        vm_call_function(vm, task->callback, 2, argv);
    }
    
    free(task->input_data);
    if (task->output_data) free(task->output_data);
    free(task);
}

static Value js_zlib_do_compress(VM* vm, Value this_val, int arg_count, Value* args, int is_deflate) {
    (void)this_val;
    if (arg_count < 2) return VAL_UNDEFINED;
    
    const char* data = NULL;
    size_t len = 0;

    if (IS_POINTER(args[0])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(args[0]);
            data = (const char*)buf->data; len = buf->length;
        } else if (h->obj_type == OBJ_STRING) {
            JSString* str = (JSString*)get_pointer(args[0]);
            data = str->data; len = str->length;
        }
    }
    
    if (!data) {
        vm_throw_error(vm, create_error("TypeError", create_string("Input must be a Buffer or String", 32)));
        return VAL_UNDEFINED;
    }
    
    Value callback = args[1];
    if (!IS_POINTER(callback) || ((BlockHeader*)((char*)get_pointer(callback) - sizeof(BlockHeader)))->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_error("TypeError", create_string("Callback must be a function", 27)));
        return VAL_UNDEFINED;
    }

    ZlibTask* task = malloc(sizeof(ZlibTask));
    task->vm = vm;
    task->input_len = len;
    task->input_data = malloc(len);
    memcpy(task->input_data, data, len);
    task->output_data = NULL;
    task->output_len = 0;
    task->callback = callback;
    task->is_deflate = is_deflate;
    
    WorkItem* item = calloc(1, sizeof(WorkItem));
    item->work = zlib_work_fn;
    item->after = zlib_after_fn;
    item->gc_mark = zlib_task_gc_mark;
    item->data = task;
    
    tp_submit(item);

    return VAL_UNDEFINED;
}

static Value js_zlib_deflate(VM* vm, Value this_val, int arg_count, Value* args) {
    return js_zlib_do_compress(vm, this_val, arg_count, args, 1);
}

static Value js_zlib_inflate(VM* vm, Value this_val, int arg_count, Value* args) {
    return js_zlib_do_compress(vm, this_val, arg_count, args, 0);
}

Value build_zlib_module(VM* vm) {
    (void)vm;
    Value exports = create_object();
    object_set(exports, create_string("deflate", 7),
               create_native_function((void*)js_zlib_deflate,
                                      create_string("deflate", 7)));
    object_set(exports, create_string("inflate", 7),
               create_native_function((void*)js_zlib_inflate,
                                      create_string("inflate", 7)));
    return exports;
}

void zlib_mark_gc_roots(GCTraceFn trace) {
    // Thread pool gc_mark handles pending tasks.
    (void)trace;
}
