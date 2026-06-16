# Memory Management Rules for Curica Runtime

Curica utilizes a heavily optimized memory system with strict invariants. Disregarding these rules will result in immediate Segmentation Faults or `use-after-free` bugs.

## 1. NaN-Boxing
Every JavaScript value (`Value`) in Curica is represented as a 64-bit floating-point number.
- **Pointers**: If a `Value` represents an object, string, or array, the actual C pointer is hidden within the upper 16 bits of a quiet NaN.
- **Checking Types**: Always use macros from `vm.h` to type-check: `IS_POINTER(val)`, `IS_NUMBER(val)`, `IS_INTEGER(val)`.
- **Extracting Pointers**: Always use `get_pointer(val)` to retrieve the raw C pointer.
- **Object Header**: The raw pointer returned points *directly to the payload*. The garbage collector metadata (`BlockHeader`) is located *immediately before* the pointer (`BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));`). Do NOT attempt to read `BlockHeader` offsets incorrectly.

## 2. Arena Allocator
Curica does **not** use the standard `malloc` for JavaScript objects.
- It uses a custom Arena Allocator (bump-pointer).
- Objects created via `create_object()`, `create_string()`, etc., are allocated from this arena.
- **NEVER** use standard `free()` on a pointer returned by an arena creation function.
- C-level structs (e.g., handles for native modules like `uv_tcp_t`, `WorkItem`, etc.) CAN be allocated with standard `malloc()`/`calloc()`, but their lifecycle must be managed explicitly (e.g., freeing them inside a GC sweep hook or connection close event).

## 3. Garbage Collection (GC) Roots
Curica uses a Mark-and-Sweep garbage collector. The GC runs frequently during event loop tick drains and memory bounds checks.
- If a C native module stores a JavaScript `Value` (like a callback function, a promise resolution function, or an EventEmitter instance) inside a C struct, **the GC cannot see it automatically**.
- If a GC cycle runs, the JS object might be swept, and the next time the native module tries to invoke that callback, the VM will crash with a `use-after-free`.

**The Solution: `mark_gc_roots`**
1. Every native module storing `Value` callbacks MUST expose a function: `void mymodule_mark_gc_roots(GCTraceFn trace)`.
2. This function must iterate over all active internal C structs, calling `trace(&struct->callback_value)` on any stored `Value`.
3. This function MUST be registered in both `alloc.c` and `event_loop.c` to be invoked during major/minor GC sweeps.

## 4. Handles and Resource Leaks
Native resources (file descriptors, sockets, zlib streams) must be properly released.
- Use the `close` callbacks properly in JS wrappers.
- For asynchronous operations via `tp_submit(WorkItem*)`, always ensure the `after` function frees the `WorkItem` and any dynamically allocated C memory payloads (`input_data`, `output_data`).
