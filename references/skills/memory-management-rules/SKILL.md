---
name: Memory Management Rules
description: Rules for handling memory in Curica, including NaN-boxing, arena allocation, mmap proxies, and garbage collection tracing.
---

# Memory Management Rules for Curica Kernel

Curica utilizes a heavily optimized memory system with strict invariants. Disregarding these rules will result in immediate Segmentation Faults or sandbox escapes.

## 1. NaN-Boxing
Every JavaScript value (`Value`) in Curica is represented as a 64-bit floating-point number.
- **Pointers**: If a `Value` represents an object, string, or array, the actual C pointer is hidden within the upper 16 bits of a quiet NaN.
- **Checking Types**: Always use macros from `vm.h` to type-check: `IS_POINTER(val)`, `IS_NUMBER(val)`, `IS_INTEGER(val)`.
- **Extracting Pointers**: Always use `get_pointer(val)` to retrieve the raw C pointer.
- **Object Header**: The garbage collector metadata (`BlockHeader`) is located *immediately before* the pointer (`BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));`). Do NOT attempt to read `BlockHeader` offsets incorrectly.

## 2. The Custom Arena Allocator
Curica does **not** use the standard `malloc` for JavaScript objects.
- It uses a custom bump-pointer Arena Allocator.
- **NEVER** use standard `free()` on a pointer returned by an arena creation function.
- C-level structs (e.g., handles for native modules like `uv_tcp_t`, `WorkItem`, etc.) CAN be allocated with standard `malloc()`/`calloc()`, but their lifecycle must be managed explicitly.

## 3. Host-Proxied `mmap` and Zero-Copy
For massive files (e.g., AI Models >10GB), do NOT use `read()` to buffer the file into standard `malloc` arrays.
- Always use the kernel's `mmap()` proxy logic to map the file directly into WASM linear memory, allowing the host OS hardware page-faults to stream the file efficiently without causing C-runtime OOM errors.
- Ensure all threaded memory sharing occurs via zero-copy `SharedArrayBuffer` mapping.

## 4. Garbage Collection (GC) Roots
Curica uses a Mark-and-Sweep garbage collector. The GC runs frequently during event loop tick drains.
- If a C native module stores a JavaScript `Value` inside a C struct, **the GC cannot see it automatically**.
- If a GC cycle runs, the JS object might be swept, resulting in a `use-after-free` crash.

**The Solution: `mark_gc_roots`**
1. Every native module storing `Value` callbacks MUST expose a function: `void mymodule_mark_gc_roots(GCTraceFn trace)`.
2. This function must iterate over all active internal C structs, calling `trace(&struct->callback_value)` on any stored `Value`.
3. This function MUST be registered in both `alloc.c` and `event_loop.c`.
