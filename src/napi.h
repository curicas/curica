/**
 * @file napi.h
 * @brief Node-API types and declarations.
 * 
 * C-compatible N-API headers for addon compilation and ABI stability.
 */
#ifndef CURICA_NAPI_H
#define CURICA_NAPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standard N-API Types
typedef struct napi_env__* napi_env;
typedef struct napi_value__* napi_value;
typedef struct napi_callback_info__* napi_callback_info;

typedef enum {
    napi_ok,
    napi_invalid_arg,
    napi_object_expected,
    napi_string_expected,
    napi_name_expected,
    napi_function_expected,
    napi_number_expected,
    napi_boolean_expected,
    napi_array_expected,
    napi_generic_failure,
    napi_pending_exception,
    napi_cancelled,
    napi_escape_called_twice,
    napi_handle_scope_mismatch,
    napi_callback_scope_mismatch,
    napi_queue_full,
    napi_closing,
    napi_bigint_expected,
    napi_date_expected,
    napi_arraybuffer_expected,
    napi_detachable_arraybuffer_expected,
    napi_would_deadlock,
} napi_status;

typedef napi_value (*napi_callback)(napi_env env, napi_callback_info info);

// Property attributes
typedef enum {
    napi_default = 0,
    napi_writable = 1 << 0,
    napi_enumerable = 1 << 1,
    napi_configurable = 1 << 2,
    napi_static = 1 << 10,
} napi_property_attributes;

typedef struct {
    const char* utf8name;
    napi_value name;
    napi_callback method;
    napi_callback getter;
    napi_callback setter;
    napi_value value;
    napi_property_attributes attributes;
    void* data;
} napi_property_descriptor;

// Module Registration
typedef napi_value (*napi_addon_register_func)(napi_env env, napi_value exports);

typedef struct {
    int nm_version;
    unsigned int nm_flags;
    const char* nm_filename;
    napi_addon_register_func nm_register_func;
    const char* nm_modname;
    void* nm_priv;
    void* reserved[4];
} napi_module;

#define NAPI_MODULE_VERSION 1

#ifdef __cplusplus
#define EXTERN_C_START extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_START
#define EXTERN_C_END
#endif

#define NAPI_MODULE_EXPORT __attribute__((visibility("default")))

#define NAPI_MODULE(modname, regfunc)                                 \
  EXTERN_C_START                                                      \
  NAPI_MODULE_EXPORT napi_module _module = {                          \
    NAPI_MODULE_VERSION,                                              \
    0,                                                                \
    __FILE__,                                                         \
    (napi_addon_register_func)(regfunc),                              \
    #modname,                                                         \
    NULL,                                                             \
    {0},                                                              \
  };                                                                  \
  EXTERN_C_END

#define NAPI_MODULE_INIT()                                            \
  EXTERN_C_START                                                      \
  NAPI_MODULE_EXPORT napi_value napi_register_module_v1(              \
      napi_env env, napi_value exports);                              \
  EXTERN_C_END                                                        \
  NAPI_MODULE_EXPORT napi_value napi_register_module_v1(              \
      napi_env env, napi_value exports)


// These types must be declared before the API function signatures that use them.
typedef enum {
    napi_undefined,
    napi_null,
    napi_boolean,
    napi_number,
    napi_string,
    napi_symbol,
    napi_object,
    napi_function,
    napi_external,
    napi_bigint,
} napi_valuetype;

typedef void (*napi_finalize)(napi_env env, void* finalize_data, void* finalize_hint);
typedef struct napi_ref__ *napi_ref;

typedef struct napi_async_work__ *napi_async_work;
typedef void (*napi_async_execute_callback)(napi_env env, void* data);
typedef void (*napi_async_complete_callback)(napi_env env, napi_status status, void* data);

typedef struct napi_threadsafe_function__ *napi_threadsafe_function;
typedef enum { napi_tsfn_release, napi_tsfn_abort } napi_threadsafe_function_release_mode;
typedef enum { napi_tsfn_nonblocking, napi_tsfn_blocking } napi_threadsafe_function_call_mode;
typedef void (*napi_threadsafe_function_call_js)(napi_env env, napi_value js_callback, void* context, void* data);

#ifdef CURICA_RUNTIME
// --- Value Creation ---
napi_status napi_get_undefined(napi_env env, napi_value* result);
napi_status napi_get_null(napi_env env, napi_value* result);
napi_status napi_get_boolean(napi_env env, bool value, napi_value* result);
napi_status napi_create_int32(napi_env env, int32_t value, napi_value* result);
napi_status napi_create_int64(napi_env env, int64_t value, napi_value* result);
napi_status napi_create_uint32(napi_env env, uint32_t value, napi_value* result);
napi_status napi_create_double(napi_env env, double value, napi_value* result);
napi_status napi_create_string_utf8(napi_env env, const char* str, size_t length, napi_value* result);
napi_status napi_create_object(napi_env env, napi_value* result);
napi_status napi_create_array(napi_env env, napi_value* result);
napi_status napi_create_array_with_length(napi_env env, size_t length, napi_value* result);
napi_status napi_create_function(napi_env env, const char* utf8name, size_t length, napi_callback cb, void* data, napi_value* result);
napi_status napi_create_buffer(napi_env env, size_t size, void** data, napi_value* result);
napi_status napi_create_buffer_copy(napi_env env, size_t size, const void* data, void** result_data, napi_value* result);
napi_status napi_create_external_buffer(napi_env env, size_t byte_length, void* data, napi_finalize finalize_cb, void* finalize_hint, napi_value* result);
napi_status napi_create_error(napi_env env, napi_value code, napi_value msg, napi_value* result);

// --- Value Retrieval ---
napi_status napi_get_value_int32(napi_env env, napi_value value, int32_t* result);
napi_status napi_get_value_int64(napi_env env, napi_value value, int64_t* result);
napi_status napi_get_value_uint32(napi_env env, napi_value value, uint32_t* result);
napi_status napi_get_value_double(napi_env env, napi_value value, double* result);
napi_status napi_get_value_bool(napi_env env, napi_value value, bool* result);
napi_status napi_get_value_string_utf8(napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result);
napi_status napi_get_buffer_info(napi_env env, napi_value value, void** data, size_t* length);
napi_status napi_get_array_length(napi_env env, napi_value value, uint32_t* result);
napi_status napi_get_element(napi_env env, napi_value object, uint32_t index, napi_value* result);

// --- Object Operations ---
napi_status napi_set_named_property(napi_env env, napi_value object, const char* utf8name, napi_value value);
napi_status napi_get_named_property(napi_env env, napi_value object, const char* utf8name, napi_value* result);
napi_status napi_set_element(napi_env env, napi_value object, uint32_t index, napi_value value);
napi_status napi_define_properties(napi_env env, napi_value object, size_t property_count, const napi_property_descriptor* properties);
napi_status napi_wrap(napi_env env, napi_value js_object, void* native_object, napi_finalize finalize_cb, void* finalize_hint, napi_ref* result);
napi_status napi_unwrap(napi_env env, napi_value js_object, void** result);
napi_status napi_remove_wrap(napi_env env, napi_value js_object, void** result);

// --- Type Checking ---
napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype* result);
napi_status napi_is_array(napi_env env, napi_value value, bool* result);
napi_status napi_is_buffer(napi_env env, napi_value value, bool* result);
napi_status napi_is_error(napi_env env, napi_value value, bool* result);

// --- Callback & Error ---
napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo, size_t* argc, napi_value* argv, napi_value* this_arg, void** data);
napi_status napi_throw(napi_env env, napi_value error);
napi_status napi_throw_error(napi_env env, const char* code, const char* msg);
napi_status napi_coerce_to_string(napi_env env, napi_value value, napi_value* result);

// --- Async Work ---
napi_status napi_create_async_work(napi_env env, napi_value async_resource, napi_value async_resource_name, napi_async_execute_callback execute, napi_async_complete_callback complete, void* data, napi_async_work* result);
napi_status napi_queue_async_work(napi_env env, napi_async_work work);
napi_status napi_delete_async_work(napi_env env, napi_async_work work);

// --- Thread-safe Functions ---
napi_status napi_create_threadsafe_function(napi_env env, napi_value func, napi_value async_resource, napi_value async_resource_name, size_t max_queue_size, size_t initial_thread_count, void* thread_finalize_data, napi_finalize thread_finalize_cb, void* context, napi_threadsafe_function_call_js call_js_cb, napi_threadsafe_function* result);
napi_status napi_call_threadsafe_function(napi_threadsafe_function func, void* data, napi_threadsafe_function_call_mode is_blocking);
napi_status napi_release_threadsafe_function(napi_threadsafe_function func, napi_threadsafe_function_release_mode mode);

void napi_sweep_wraps(napi_env env);
#endif

typedef struct NapiApiTable {
    // --- Value Creation ---
    napi_status (*get_undefined)(napi_env env, napi_value* result);
    napi_status (*get_null)(napi_env env, napi_value* result);
    napi_status (*get_boolean)(napi_env env, bool value, napi_value* result);
    napi_status (*create_int32)(napi_env env, int32_t value, napi_value* result);
    napi_status (*create_int64)(napi_env env, int64_t value, napi_value* result);
    napi_status (*create_uint32)(napi_env env, uint32_t value, napi_value* result);
    napi_status (*create_double)(napi_env env, double value, napi_value* result);
    napi_status (*create_string_utf8)(napi_env env, const char* str, size_t length, napi_value* result);
    napi_status (*create_object)(napi_env env, napi_value* result);
    napi_status (*create_array)(napi_env env, napi_value* result);
    napi_status (*create_array_with_length)(napi_env env, size_t length, napi_value* result);
    napi_status (*create_function)(napi_env env, const char* utf8name, size_t length, napi_callback cb, void* data, napi_value* result);
    napi_status (*create_buffer)(napi_env env, size_t size, void** data, napi_value* result);
    napi_status (*create_buffer_copy)(napi_env env, size_t size, const void* data, void** result_data, napi_value* result);
    napi_status (*create_error)(napi_env env, napi_value code, napi_value msg, napi_value* result);
    // --- Value Retrieval ---
    napi_status (*get_value_int32)(napi_env env, napi_value value, int32_t* result);
    napi_status (*get_value_int64)(napi_env env, napi_value value, int64_t* result);
    napi_status (*get_value_uint32)(napi_env env, napi_value value, uint32_t* result);
    napi_status (*get_value_double)(napi_env env, napi_value value, double* result);
    napi_status (*get_value_bool)(napi_env env, napi_value value, bool* result);
    napi_status (*get_value_string_utf8)(napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result);
    napi_status (*get_buffer_info)(napi_env env, napi_value value, void** data, size_t* length);
    napi_status (*get_array_length)(napi_env env, napi_value value, uint32_t* result);
    napi_status (*get_element)(napi_env env, napi_value object, uint32_t index, napi_value* result);
    // --- Object Operations ---
    napi_status (*set_named_property)(napi_env env, napi_value object, const char* utf8name, napi_value value);
    napi_status (*get_named_property)(napi_env env, napi_value object, const char* utf8name, napi_value* result);
    napi_status (*set_element)(napi_env env, napi_value object, uint32_t index, napi_value value);
    napi_status (*define_properties)(napi_env env, napi_value object, size_t property_count, const napi_property_descriptor* properties);
    napi_status (*wrap)(napi_env env, napi_value js_object, void* native_object, napi_finalize finalize_cb, void* finalize_hint, napi_ref* result);
    napi_status (*unwrap)(napi_env env, napi_value js_object, void** result);
    napi_status (*remove_wrap)(napi_env env, napi_value js_object, void** result);
    // --- Type Checking ---
    napi_status (*typeof_value)(napi_env env, napi_value value, napi_valuetype* result);
    napi_status (*is_array)(napi_env env, napi_value value, bool* result);
    napi_status (*is_buffer)(napi_env env, napi_value value, bool* result);
    napi_status (*is_error)(napi_env env, napi_value value, bool* result);
    // --- Callback & Error ---
    napi_status (*get_cb_info)(napi_env env, napi_callback_info cbinfo, size_t* argc, napi_value* argv, napi_value* this_arg, void** data);
    napi_status (*throw_error)(napi_env env, const char* code, const char* msg);
    napi_status (*coerce_to_string)(napi_env env, napi_value value, napi_value* result);
    // --- Async Work ---
    napi_status (*create_async_work)(napi_env env, napi_value async_resource, napi_value async_resource_name, napi_async_execute_callback execute, napi_async_complete_callback complete, void* data, napi_async_work* result);
    napi_status (*queue_async_work)(napi_env env, napi_async_work work);
    napi_status (*delete_async_work)(napi_env env, napi_async_work work);
    // --- Thread-safe Functions ---
    napi_status (*create_threadsafe_function)(napi_env env, napi_value func, napi_value async_resource, napi_value async_resource_name, size_t max_queue_size, size_t initial_thread_count, void* thread_finalize_data, napi_finalize thread_finalize_cb, void* context, napi_threadsafe_function_call_js call_js_cb, napi_threadsafe_function* result);
    napi_status (*call_threadsafe_function)(napi_threadsafe_function func, void* data, napi_threadsafe_function_call_mode is_blocking);
    napi_status (*release_threadsafe_function)(napi_threadsafe_function func, napi_threadsafe_function_release_mode mode);
} NapiApiTable;

struct napi_env__ {
    NapiApiTable* api;
};

#ifndef CURICA_RUNTIME
// Addon-side macro dispatch — redirects each call through the vtable pointer.
#define napi_get_undefined(e,r)            (e)->api->get_undefined((e),(r))
#define napi_get_null(e,r)                 (e)->api->get_null((e),(r))
#define napi_get_boolean(e,v,r)            (e)->api->get_boolean((e),(v),(r))
#define napi_create_int32(e,v,r)           (e)->api->create_int32((e),(v),(r))
#define napi_create_int64(e,v,r)           (e)->api->create_int64((e),(v),(r))
#define napi_create_uint32(e,v,r)          (e)->api->create_uint32((e),(v),(r))
#define napi_create_double(e,v,r)          (e)->api->create_double((e),(v),(r))
#define napi_create_string_utf8(e,s,l,r)   (e)->api->create_string_utf8((e),(s),(l),(r))
#define napi_create_object(e,r)            (e)->api->create_object((e),(r))
#define napi_create_array(e,r)             (e)->api->create_array((e),(r))
#define napi_create_array_with_length(e,l,r) (e)->api->create_array_with_length((e),(l),(r))
#define napi_create_function(e,n,l,c,d,r)  (e)->api->create_function((e),(n),(l),(c),(d),(r))
#define napi_create_buffer(e,s,d,r)        (e)->api->create_buffer((e),(s),(d),(r))
#define napi_create_buffer_copy(e,s,d,rd,r) (e)->api->create_buffer_copy((e),(s),(d),(rd),(r))
#define napi_create_error(e,c,m,r)         (e)->api->create_error((e),(c),(m),(r))
#define napi_get_value_int32(e,v,r)        (e)->api->get_value_int32((e),(v),(r))
#define napi_get_value_int64(e,v,r)        (e)->api->get_value_int64((e),(v),(r))
#define napi_get_value_uint32(e,v,r)       (e)->api->get_value_uint32((e),(v),(r))
#define napi_get_value_double(e,v,r)       (e)->api->get_value_double((e),(v),(r))
#define napi_get_value_bool(e,v,r)         (e)->api->get_value_bool((e),(v),(r))
#define napi_get_value_string_utf8(e,v,b,bs,r) (e)->api->get_value_string_utf8((e),(v),(b),(bs),(r))
#define napi_get_buffer_info(e,v,d,l)      (e)->api->get_buffer_info((e),(v),(d),(l))
#define napi_get_array_length(e,v,r)       (e)->api->get_array_length((e),(v),(r))
#define napi_get_element(e,o,i,r)          (e)->api->get_element((e),(o),(i),(r))
#define napi_set_named_property(e,o,n,v)   (e)->api->set_named_property((e),(o),(n),(v))
#define napi_get_named_property(e,o,n,r)   (e)->api->get_named_property((e),(o),(n),(r))
#define napi_set_element(e,o,i,v)          (e)->api->set_element((e),(o),(i),(v))
#define napi_define_properties(e,o,c,p)    (e)->api->define_properties((e),(o),(c),(p))
#define napi_wrap(e,o,n,f,h,r)             (e)->api->wrap((e),(o),(n),(f),(h),(r))
#define napi_unwrap(e,o,r)                 (e)->api->unwrap((e),(o),(r))
#define napi_remove_wrap(e,o,r)            (e)->api->remove_wrap((e),(o),(r))
#define napi_typeof(e,v,r)                 (e)->api->typeof_value((e),(v),(r))
#define napi_is_array(e,v,r)               (e)->api->is_array((e),(v),(r))
#define napi_is_buffer(e,v,r)              (e)->api->is_buffer((e),(v),(r))
#define napi_is_error(e,v,r)               (e)->api->is_error((e),(v),(r))
#define napi_get_cb_info(e,i,c,v,t,d)      (e)->api->get_cb_info((e),(i),(c),(v),(t),(d))
#define napi_throw_error(e,c,m)            (e)->api->throw_error((e),(c),(m))
#define napi_coerce_to_string(e,v,r)       (e)->api->coerce_to_string((e),(v),(r))
#define napi_create_async_work(e,r,n,x,c,d,w) (e)->api->create_async_work((e),(r),(n),(x),(c),(d),(w))
#define napi_queue_async_work(e,w)         (e)->api->queue_async_work((e),(w))
#define napi_delete_async_work(e,w)        (e)->api->delete_async_work((e),(w))
#define napi_create_threadsafe_function(e,f,r,n,mq,it,tfd,tfc,c,cjs,res) (e)->api->create_threadsafe_function((e),(f),(r),(n),(mq),(it),(tfd),(tfc),(c),(cjs),(res))
#define napi_call_threadsafe_function(f,d,m) ((NapiApiTable*)((struct napi_env__*)0)->api)->call_threadsafe_function((f),(d),(m)) // hack for tsfn
#define napi_release_threadsafe_function(f,m) ((NapiApiTable*)((struct napi_env__*)0)->api)->release_threadsafe_function((f),(m))
#endif

#ifdef __cplusplus
}
#endif

#endif // CURICA_NAPI_H
