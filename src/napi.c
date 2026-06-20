/**
 * @file napi.c
 * @brief Node-API (N-API) Implementation.
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
#define CURICA_RUNTIME
#include "napi.h"
#include "vm.h"
#include "alloc.h"
#include "thread_pool.h"
#include <string.h>

extern JSFunction* g_current_native_function;

typedef struct NAPIWrap {
    Value target_js_obj;
    void* native_data;
    napi_finalize finalize_cb;
    void* finalize_hint;
    struct NAPIWrap* next;
} NAPIWrap;

NAPIWrap* g_napi_wraps = NULL;

void napi_sweep_wraps(napi_env env) {
    NAPIWrap** prev = &g_napi_wraps;
    while (*prev) {
        NAPIWrap* wrap = *prev;
        BlockHeader* header = (BlockHeader*)((char*)get_pointer(wrap->target_js_obj) - sizeof(BlockHeader));
        if (!header->gc_mark) {
            // Target is about to be swept, call finalizer
            if (wrap->finalize_cb) {
                wrap->finalize_cb(env, wrap->native_data, wrap->finalize_hint);
            }
            *prev = wrap->next;
            free(wrap);
        } else {
            prev = &wrap->next;
        }
    }
}

// Convert Curica Value to napi_value and vice-versa
#define VAL_TO_NAPI(v) ((napi_value)(uintptr_t)(v))
#define NAPI_TO_VAL(nv) ((Value)(uintptr_t)(nv))

napi_status napi_get_undefined(napi_env env, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(VAL_UNDEFINED);
    return napi_ok;
}

napi_status napi_get_null(napi_env env, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(VAL_NULL);
    return napi_ok;
}

napi_status napi_create_int32(napi_env env, int32_t value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(make_integer(value));
    return napi_ok;
}

napi_status napi_create_string_utf8(napi_env env, const char* str, size_t length, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    if (length == (size_t)-1) length = strlen(str);
    Value s = create_string(str, length);
    *result = VAL_TO_NAPI(s);
    return napi_ok;
}

napi_status napi_create_object(napi_env env, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(create_object());
    return napi_ok;
}

napi_status napi_set_named_property(napi_env env, napi_value object, const char* utf8name, napi_value value) {
    (void)env;
    if (!object || !utf8name || !value) return napi_invalid_arg;
    Value obj = NAPI_TO_VAL(object);
    Value val = NAPI_TO_VAL(value);
    Value key = create_string(utf8name, strlen(utf8name));
    object_set(obj, key, val);
    return napi_ok;
}

napi_status napi_get_named_property(napi_env env, napi_value object, const char* utf8name, napi_value* result) {
    (void)env;
    if (!object || !utf8name || !result) return napi_invalid_arg;
    Value obj = NAPI_TO_VAL(object);
    Value key = create_string(utf8name, strlen(utf8name));
    *result = VAL_TO_NAPI(object_get(obj, key));
    return napi_ok;
}

typedef struct {
    napi_callback cb;
    void* data;
} NapiCallbackData;

// Struct to pass args to callback info
struct napi_callback_info__ {
    VM* vm;
    Value this_val;
    int arg_count;
    Value* args;
    void* data;
};

// Trampoline to call N-API callback from Curica VM
static Value napi_trampoline(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!g_current_native_function) return VAL_UNDEFINED;
    
    NapiCallbackData* cb_data = (NapiCallbackData*)g_current_native_function->user_data;
    if (!cb_data || !cb_data->cb) return VAL_UNDEFINED;
    
    struct napi_callback_info__ info = {
        .vm = vm,
        .this_val = this_val,
        .arg_count = arg_count,
        .args = args,
        .data = cb_data->data
    };
    
    napi_value result = cb_data->cb((napi_env)vm, &info);
    if (!result) return VAL_UNDEFINED;
    return NAPI_TO_VAL(result);
}

napi_status napi_create_function(napi_env env, const char* utf8name, size_t length, napi_callback cb, void* data, napi_value* result) {
    (void)env;
    if (!result || !cb) return napi_invalid_arg;
    if (length == (size_t)-1) length = strlen(utf8name);
    
    Value name_val = create_string(utf8name, length);
    Value func_val = create_native_function((void*)napi_trampoline, name_val);
    JSFunction* f = (JSFunction*)get_pointer(func_val);
    
    NapiCallbackData* cb_data = (NapiCallbackData*)arena_alloc(OBJ_BUFFER_DATA, sizeof(NapiCallbackData));
    cb_data->cb = cb;
    cb_data->data = data;
    f->user_data = cb_data;
    
    *result = VAL_TO_NAPI(func_val);
    return napi_ok;
}

napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo, size_t* argc, napi_value* argv, napi_value* this_arg, void** data) {
    (void)env;
    if (!cbinfo) return napi_invalid_arg;
    
    if (argc) {
        size_t requested = *argc;
        size_t provided = cbinfo->arg_count;
        size_t to_copy = requested < provided ? requested : provided;
        
        if (argv) {
            for (size_t i = 0; i < to_copy; i++) {
                argv[i] = VAL_TO_NAPI(cbinfo->args[i]);
            }
            for (size_t i = to_copy; i < requested; i++) {
                argv[i] = VAL_TO_NAPI(VAL_UNDEFINED);
            }
        }
        *argc = provided;
    }
    
    if (this_arg) {
        *this_arg = VAL_TO_NAPI(cbinfo->this_val);
    }
    
    if (data) {
        *data = cbinfo->data;
    }
    
    return napi_ok;
}


// ---------------------------------------------------------------------------
// Extended N-API implementations
// ---------------------------------------------------------------------------

napi_status napi_get_boolean(napi_env env, bool value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(make_boolean(value));
    return napi_ok;
}

napi_status napi_create_double(napi_env env, double value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(make_double(value));
    return napi_ok;
}

napi_status napi_create_int64(napi_env env, int64_t value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(make_double((double)value));
    return napi_ok;
}

napi_status napi_create_uint32(napi_env env, uint32_t value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(make_integer((int)value));
    return napi_ok;
}

napi_status napi_create_array(napi_env env, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(create_array(8));
    return napi_ok;
}

napi_status napi_create_array_with_length(napi_env env, size_t length, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value arr = create_array((uint32_t)length);
    // Pre-fill with undefined
    JSArray* a = (JSArray*)get_pointer(arr);
    for (size_t i = 0; i < length; i++) {
        a->elements[i] = VAL_UNDEFINED;
    }
    a->length = (uint32_t)length;
    *result = VAL_TO_NAPI(arr);
    return napi_ok;
}

napi_status napi_create_buffer(napi_env env, size_t size, void** data, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value buf = create_buffer((uint32_t)size, false);
    JSBuffer* b = (JSBuffer*)get_pointer(buf);
    if (data) *data = b->data;
    *result = VAL_TO_NAPI(buf);
    return napi_ok;
}

napi_status napi_create_buffer_copy(napi_env env, size_t size, const void* data, void** result_data, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value buf = create_buffer((uint32_t)size, false);
    JSBuffer* b = (JSBuffer*)get_pointer(buf);
    if (data) memcpy(b->data, data, size);
    if (result_data) *result_data = b->data;
    *result = VAL_TO_NAPI(buf);
    return napi_ok;
}

napi_status napi_create_external_buffer(napi_env env, size_t byte_length, void* data,
                                         napi_finalize finalize_cb, void* finalize_hint,
                                         napi_value* result) {
    (void)env; (void)finalize_cb; (void)finalize_hint;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(create_buffer_external(data, byte_length));
    return napi_ok;
}

napi_status napi_create_error(napi_env env, napi_value code, napi_value msg, napi_value* result) {
    (void)env; (void)code;
    if (!result) return napi_invalid_arg;
    Value msg_val = NAPI_TO_VAL(msg);
    *result = VAL_TO_NAPI(create_error("Error", msg_val));
    return napi_ok;
}

napi_status napi_get_value_int32(napi_env env, napi_value value, int32_t* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (IS_INTEGER(v)) *result = (int32_t)get_integer(v);
    else if (IS_DOUBLE(v)) *result = (int32_t)get_double(v);
    else return napi_number_expected;
    return napi_ok;
}

napi_status napi_get_value_int64(napi_env env, napi_value value, int64_t* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (IS_INTEGER(v)) *result = (int64_t)get_integer(v);
    else if (IS_DOUBLE(v)) *result = (int64_t)get_double(v);
    else return napi_number_expected;
    return napi_ok;
}

napi_status napi_get_value_uint32(napi_env env, napi_value value, uint32_t* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (IS_INTEGER(v)) *result = (uint32_t)get_integer(v);
    else if (IS_DOUBLE(v)) *result = (uint32_t)get_double(v);
    else return napi_number_expected;
    return napi_ok;
}

napi_status napi_get_value_double(napi_env env, napi_value value, double* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (IS_DOUBLE(v)) *result = get_double(v);
    else if (IS_INTEGER(v)) *result = (double)get_integer(v);
    else return napi_number_expected;
    return napi_ok;
}

napi_status napi_get_value_bool(napi_env env, napi_value value, bool* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (!IS_BOOLEAN(v)) return napi_boolean_expected;
    *result = get_boolean(v);
    return napi_ok;
}

napi_status napi_get_value_string_utf8(napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result) {
    (void)env;
    Value v = NAPI_TO_VAL(value);
    if (!IS_POINTER(v)) return napi_string_expected;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return napi_string_expected;
    JSString* s = (JSString*)get_pointer(v);
    if (result) *result = s->length;
    if (buf && bufsize > 0) {
        size_t to_copy = (s->length < bufsize - 1) ? s->length : bufsize - 1;
        memcpy(buf, s->data, to_copy);
        buf[to_copy] = '\0';
    }
    return napi_ok;
}

napi_status napi_get_buffer_info(napi_env env, napi_value value, void** data, size_t* length) {
    (void)env;
    Value v = NAPI_TO_VAL(value);
    if (!IS_POINTER(v)) return napi_invalid_arg;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_BUFFER) return napi_invalid_arg;
    JSBuffer* buf = (JSBuffer*)get_pointer(v);
    if (data) *data = buf->data;
    if (length) *length = buf->length;
    return napi_ok;
}

napi_status napi_get_array_length(napi_env env, napi_value value, uint32_t* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (!IS_POINTER(v)) return napi_array_expected;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return napi_array_expected;
    *result = ((JSArray*)get_pointer(v))->length;
    return napi_ok;
}

napi_status napi_get_element(napi_env env, napi_value object, uint32_t index, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value arr = NAPI_TO_VAL(object);
    if (!IS_POINTER(arr)) return napi_array_expected;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(arr) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return napi_array_expected;
    JSArray* a = (JSArray*)get_pointer(arr);
    *result = (index < a->length) ? VAL_TO_NAPI(a->elements[index]) : VAL_TO_NAPI(VAL_UNDEFINED);
    return napi_ok;
}

napi_status napi_set_element(napi_env env, napi_value object, uint32_t index, napi_value value) {
    (void)env;
    Value arr = NAPI_TO_VAL(object);
    if (!IS_POINTER(arr)) return napi_array_expected;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(arr) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return napi_array_expected;
    JSArray* a = (JSArray*)get_pointer(arr);
    if (index < a->capacity) {
        a->elements[index] = NAPI_TO_VAL(value);
        if (index >= a->length) a->length = index + 1;
    }
    return napi_ok;
}

napi_status napi_define_properties(napi_env env, napi_value object, size_t property_count,
                                    const napi_property_descriptor* properties) {
    (void)env;
    Value obj = NAPI_TO_VAL(object);
    for (size_t i = 0; i < property_count; i++) {
        const napi_property_descriptor* p = &properties[i];
        if (!p->utf8name) continue;
        Value key = create_string(p->utf8name, (int)strlen(p->utf8name));
        Value val;
        if (p->value) {
            val = NAPI_TO_VAL(p->value);
        } else if (p->method) {
            // Wrap as a native function
            val = create_native_function((void*)p->method,
                                         create_string(p->utf8name, (int)strlen(p->utf8name)));
        } else {
            val = VAL_UNDEFINED;
        }
        object_set(obj, key, val);
    }
    return napi_ok;
}

napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    if (IS_UNDEFINED(v))      *result = napi_undefined;
    else if (IS_NULL(v))      *result = napi_null;
    else if (IS_BOOLEAN(v))   *result = napi_boolean;
    else if (IS_INTEGER(v) || IS_DOUBLE(v)) *result = napi_number;
    else if (IS_POINTER(v)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING)   *result = napi_string;
        else if (h->obj_type == OBJ_FUNCTION) *result = napi_function;
        else                             *result = napi_object;
    } else {
        *result = napi_undefined;
    }
    return napi_ok;
}

napi_status napi_is_array(napi_env env, napi_value value, bool* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    *result = IS_POINTER(v) &&
              ((BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader)))->obj_type == OBJ_ARRAY;
    return napi_ok;
}

napi_status napi_is_buffer(napi_env env, napi_value value, bool* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    *result = IS_POINTER(v) &&
              ((BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader)))->obj_type == OBJ_BUFFER;
    return napi_ok;
}

napi_status napi_is_error(napi_env env, napi_value value, bool* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    Value v = NAPI_TO_VAL(value);
    *result = IS_POINTER(v) &&
              ((BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader)))->obj_type == OBJ_ERROR;
    return napi_ok;
}

napi_status napi_throw(napi_env env, napi_value error) {
    VM* vm = (VM*)env;
    vm_throw_error(vm, NAPI_TO_VAL(error));
    return napi_ok;
}

napi_status napi_throw_error(napi_env env, const char* code, const char* msg) {
    VM* vm = (VM*)env;
    (void)code;
    size_t len = msg ? strlen(msg) : 0;
    Value err = create_error("Error", create_string(msg ? msg : "", (int)len));
    vm_throw_error(vm, err);
    return napi_ok;
}

napi_status napi_coerce_to_string(napi_env env, napi_value value, napi_value* result) {
    (void)env;
    if (!result) return napi_invalid_arg;
    *result = VAL_TO_NAPI(value_to_string(NAPI_TO_VAL(value)));
    return napi_ok;
}

napi_status napi_wrap(napi_env env, napi_value js_object, void* native_object, napi_finalize finalize_cb, void* finalize_hint, napi_ref* result) {
    (void)env;
    if (!js_object || !native_object) return napi_invalid_arg;
    
    Value obj = NAPI_TO_VAL(js_object);
    if (!IS_POINTER(obj)) return napi_invalid_arg;
    
    NAPIWrap* wrap = (NAPIWrap*)malloc(sizeof(NAPIWrap));
    wrap->target_js_obj = obj;
    wrap->native_data = native_object;
    wrap->finalize_cb = finalize_cb;
    wrap->finalize_hint = finalize_hint;
    wrap->next = g_napi_wraps;
    g_napi_wraps = wrap;
    
    if (result) *result = NULL; // references not fully implemented yet
    return napi_ok;
}

napi_status napi_unwrap(napi_env env, napi_value js_object, void** result) {
    (void)env;
    if (!js_object || !result) return napi_invalid_arg;
    Value obj = NAPI_TO_VAL(js_object);
    
    for (NAPIWrap* wrap = g_napi_wraps; wrap != NULL; wrap = wrap->next) {
        if (wrap->target_js_obj == obj) {
            *result = wrap->native_data;
            return napi_ok;
        }
    }
    return napi_invalid_arg;
}

napi_status napi_remove_wrap(napi_env env, napi_value js_object, void** result) {
    (void)env;
    if (!js_object) return napi_invalid_arg;
    Value obj = NAPI_TO_VAL(js_object);
    
    NAPIWrap** prev = &g_napi_wraps;
    while (*prev) {
        NAPIWrap* wrap = *prev;
        if (wrap->target_js_obj == obj) {
            if (result) *result = wrap->native_data;
            *prev = wrap->next;
            free(wrap);
            return napi_ok;
        }
        prev = &wrap->next;
    }
    return napi_invalid_arg;
}

struct napi_async_work__ {
    napi_env env;
    napi_async_execute_callback execute;
    napi_async_complete_callback complete;
    void* data;
    WorkItem* tp_item;
};

static void napi_async_work_cb(void* data, int* status_out) {
    napi_async_work work = (napi_async_work)data;
    if (work->execute) {
        work->execute(work->env, work->data);
    }
    *status_out = 0;
}

static void napi_async_after_cb(struct VM* vm, void* data, int status) {
    (void)vm;
    napi_async_work work = (napi_async_work)data;
    if (work->complete) {
        work->complete(work->env, status == 0 ? napi_ok : napi_generic_failure, work->data);
    }
}

napi_status napi_create_async_work(napi_env env, napi_value async_resource, napi_value async_resource_name, napi_async_execute_callback execute, napi_async_complete_callback complete, void* data, napi_async_work* result) {
    (void)async_resource; (void)async_resource_name;
    if (!result) return napi_invalid_arg;
    
    napi_async_work work = (napi_async_work)malloc(sizeof(struct napi_async_work__));
    work->env = env;
    work->execute = execute;
    work->complete = complete;
    work->data = data;
    
    work->tp_item = (WorkItem*)malloc(sizeof(WorkItem));
    work->tp_item->work = napi_async_work_cb;
    work->tp_item->after = napi_async_after_cb;
    work->tp_item->gc_mark = NULL;
    work->tp_item->data = work;
    work->tp_item->status = 0;
    work->tp_item->next = NULL;
    
    *result = work;
    return napi_ok;
}

napi_status napi_queue_async_work(napi_env env, napi_async_work work) {
    (void)env;
    if (!work) return napi_invalid_arg;
    tp_submit(work->tp_item);
    return napi_ok;
}

napi_status napi_delete_async_work(napi_env env, napi_async_work work) {
    (void)env;
    if (!work) return napi_invalid_arg;
    free(work->tp_item);
    free(work);
    return napi_ok;
}

struct napi_threadsafe_function__ {
    napi_env env;
    napi_value func;
    void* context;
    napi_threadsafe_function_call_js call_js_cb;
    napi_finalize thread_finalize_cb;
    void* thread_finalize_data;
};

typedef struct TSFNCall {
    napi_threadsafe_function tsfn;
    void* data;
    WorkItem* item;
} TSFNCall;

static void tsfn_dummy_work(void* data, int* status_out) {
    (void)data;
    *status_out = 0;
}

static void tsfn_after_work(struct VM* vm, void* data, int status) {
    (void)vm; (void)status;
    TSFNCall* call = (TSFNCall*)data;
    if (call->tsfn->call_js_cb) {
        call->tsfn->call_js_cb(call->tsfn->env, call->tsfn->func, call->tsfn->context, call->data);
    }
    free(call->item);
    free(call);
}

napi_status napi_create_threadsafe_function(napi_env env, napi_value func, napi_value async_resource, napi_value async_resource_name, size_t max_queue_size, size_t initial_thread_count, void* thread_finalize_data, napi_finalize thread_finalize_cb, void* context, napi_threadsafe_function_call_js call_js_cb, napi_threadsafe_function* result) {
    (void)async_resource; (void)async_resource_name; (void)max_queue_size; (void)initial_thread_count;
    if (!result) return napi_invalid_arg;
    
    napi_threadsafe_function tsfn = (napi_threadsafe_function)malloc(sizeof(struct napi_threadsafe_function__));
    tsfn->env = env;
    tsfn->func = func;
    tsfn->context = context;
    tsfn->call_js_cb = call_js_cb;
    tsfn->thread_finalize_cb = thread_finalize_cb;
    tsfn->thread_finalize_data = thread_finalize_data;
    *result = tsfn;
    return napi_ok;
}

napi_status napi_call_threadsafe_function(napi_threadsafe_function func, void* data, napi_threadsafe_function_call_mode is_blocking) {
    (void)is_blocking;
    if (!func) return napi_invalid_arg;
    
    TSFNCall* call = (TSFNCall*)malloc(sizeof(TSFNCall));
    call->tsfn = func;
    call->data = data;
    call->item = (WorkItem*)malloc(sizeof(WorkItem));
    call->item->work = tsfn_dummy_work;
    call->item->after = tsfn_after_work;
    call->item->gc_mark = NULL;
    call->item->data = call;
    call->item->status = 0;
    call->item->next = NULL;
    
    tp_submit(call->item);
    return napi_ok;
}

napi_status napi_release_threadsafe_function(napi_threadsafe_function func, napi_threadsafe_function_release_mode mode) {
    (void)mode;
    if (!func) return napi_invalid_arg;
    
    if (func->thread_finalize_cb) {
        func->thread_finalize_cb(func->env, func->thread_finalize_data, NULL);
    }
    free(func);
    return napi_ok;
}

// ---------------------------------------------------------------------------
// Global vtable — populated with all registered N-API functions
// ---------------------------------------------------------------------------
NapiApiTable g_napi_api = {
    .get_undefined          = napi_get_undefined,
    .get_null               = napi_get_null,
    .get_boolean            = napi_get_boolean,
    .create_int32           = napi_create_int32,
    .create_int64           = napi_create_int64,
    .create_uint32          = napi_create_uint32,
    .create_double          = napi_create_double,
    .create_string_utf8     = napi_create_string_utf8,
    .create_object          = napi_create_object,
    .create_array           = napi_create_array,
    .create_array_with_length = napi_create_array_with_length,
    .create_function        = napi_create_function,
    .create_buffer          = napi_create_buffer,
    .create_buffer_copy     = napi_create_buffer_copy,
    .create_error           = napi_create_error,
    .get_value_int32        = napi_get_value_int32,
    .get_value_int64        = napi_get_value_int64,
    .get_value_uint32       = napi_get_value_uint32,
    .get_value_double       = napi_get_value_double,
    .get_value_bool         = napi_get_value_bool,
    .get_value_string_utf8  = napi_get_value_string_utf8,
    .get_buffer_info        = napi_get_buffer_info,
    .get_array_length       = napi_get_array_length,
    .get_element            = napi_get_element,
    .set_named_property     = napi_set_named_property,
    .get_named_property     = napi_get_named_property,
    .set_element            = napi_set_element,
    .define_properties      = napi_define_properties,
    .wrap                   = napi_wrap,
    .unwrap                 = napi_unwrap,
    .remove_wrap            = napi_remove_wrap,
    .typeof_value           = napi_typeof,
    .is_array               = napi_is_array,
    .is_buffer              = napi_is_buffer,
    .is_error               = napi_is_error,
    .get_cb_info            = napi_get_cb_info,
    .throw_error            = napi_throw_error,
    .coerce_to_string       = napi_coerce_to_string,
    .create_async_work      = napi_create_async_work,
    .queue_async_work       = napi_queue_async_work,
    .delete_async_work      = napi_delete_async_work,
    .create_threadsafe_function = napi_create_threadsafe_function,
    .call_threadsafe_function = napi_call_threadsafe_function,
    .release_threadsafe_function = napi_release_threadsafe_function,
};

