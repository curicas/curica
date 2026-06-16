# Node-API (N-API) Implementation Status

Curica strives for absolute ABI compatibility with the Node.js N-API standard, enabling `.node` plugins to load without recompilation.

## Implemented APIs (v1 & v2 baseline)
The following subset of the `napi_env` vtable is fully supported natively by Curica.

### Value Creation
- `napi_get_undefined`
- `napi_get_null`
- `napi_get_boolean`
- `napi_create_int32`, `napi_create_int64`, `napi_create_uint32`, `napi_create_double`
- `napi_create_string_utf8`
- `napi_create_object`, `napi_create_array`, `napi_create_array_with_length`
- `napi_create_function`
- `napi_create_error`

### Buffer Management
- `napi_create_buffer`, `napi_create_buffer_copy`, `napi_create_external_buffer`
- `napi_get_buffer_info`

### Value Retrieval & Type Checking
- `napi_typeof`, `napi_is_array`, `napi_is_buffer`, `napi_is_error`
- `napi_get_value_int32`, `napi_get_value_int64`, `napi_get_value_uint32`, `napi_get_value_double`, `napi_get_value_bool`
- `napi_get_value_string_utf8`

### Object Operations
- `napi_get_named_property`, `napi_set_named_property`
- `napi_get_element`, `napi_set_element`
- `napi_define_properties`

### Object Wrap Lifecycle
- `napi_wrap`, `napi_unwrap`, `napi_remove_wrap`
- Wraps are integrated directly into Curica's Garbage Collector Mark-and-Sweep arena for automatic C++ destructor execution.

### Async & Threading
- `napi_create_async_work`, `napi_queue_async_work`, `napi_delete_async_work` (Backed by Curica's POSIX Thread Pool)
- `napi_create_threadsafe_function`, `napi_call_threadsafe_function`, `napi_release_threadsafe_function` (Backed by the Thread Pool wakeup pipe to the Main VM Event Loop)
- `napi_create_promise`, `napi_resolve_deferred`, `napi_reject_deferred` (Fully integrated with VM Microtask Queue)

## Callbacks & Error Handling
- `napi_get_cb_info`
- `napi_throw`, `napi_throw_error`
- `napi_coerce_to_string`

## VM State Boundary Interoperability
- **WebAssembly (WASM)**: Wasm3 engine hooks internally map Host/Guest bindings alongside `napi_create_external_buffer` memory regions safely.
- **Coroutines**: N-API asynchronous wrappers correctly trigger microtask resolutions compatible with ES6 Top-Level Await suspensions.
