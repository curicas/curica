# Curica Memory Management

Memory management in Curica (`src/alloc.c`, `src/alloc.h`) avoids heavy abstraction layers, providing deterministic allocation guarantees through Arena strategies and optimized Value representations.

## NaN-Boxing (The `Value` Type)
All JavaScript variables in Curica are stored within a 64-bit IEEE 754 floating-point standard representation known as **NaN-Boxing** (`src/value.h`).
- Native floats use the standard bit representation (if they are $< 0xfff8000000000000$).
- Integers, Booleans, and Pointers are stuffed into the unused mantissa bits of "Quiet NaNs" (`>= 0xfff8000000000000`).
- The type tag (Pointer, Integer, Boolean, Symbol, Null, Undefined) is mapped exactly to the upper 16 bits of the word (`GET_TAG(v) ((v) >> 48)`).
- This eliminates the need for bulky `struct` boxing, passing simple 64-bit primitive `Value` tokens directly via CPU registers, significantly accelerating the VM's register-based operations. Type checking resolves to a single bitwise AND mask logic.

## Bump-Pointer Arena Allocator
Curica does not use standard dynamic `malloc/free` for individual JavaScript objects. It utilizes a bulk **Arena allocator**.
- The `Arena` grabs a large contiguous memory chunk from the OS (via `mmap` or similar bulk allocations) during `arena_init()`.
- `arena_alloc` acts as an extremely fast bump-pointer allocator for the "Nursery" (New Space). Allocating a `JSObject` or `JSString` is just a pointer addition (`g_nursery.bump += size`).
- **Block Headers**: Every object allocated in the arena is prefixed with an 8-byte `BlockHeader` containing its `size`, `is_free` flag, `obj_type`, and a `gc_mark` bit.

## Mark-and-Sweep Garbage Collection
Memory is managed by a native generational Garbage Collector:
- **Phase 1 (Mark)**: The VM recursively traces roots, starting from `vm->global_obj`, the execution stack (`CallFrame` array), the active register window, and the microtask queues. It sets `block->gc_mark = 1` for all reachable structures.
- **Phase 1.5 (N-API & Roots)**: Iterates over the `NAPIWrap` linked list and explicitly traces objects temporarily pinned in the `vm->gc_roots` array (used by native modules). If a wrapped JS object lacks a `gc_mark`, its native finalizer is invoked safely destructing third-party native C++ objects.
- **Phase 2 (Minor GC & Compaction)**: The nursery (young generation) is swept. Live objects are compacted and moved to the old space. All references must be properly rooted via `vm_push_root()` during C-level instantiation to prevent moving pointers from causing segmentation faults.
- **Phase 3 (Major Sweep & Coalesce)**: The collector linearly scans the contiguous arena. Any `BlockHeader` lacking a `gc_mark` is flagged as `is_free`. Adjacent free blocks are physically coalesced, preventing heap fragmentation without invoking OS-level `free()`.

## Native Roots Pinning
Native modules (e.g., `sqlite_module.c`, `wasm_module.c`) frequently instantiate complex nested objects during callbacks. Because object instantiation (`create_object()`, `create_string()`) can spontaneously trigger a Nursery GC compaction phase, local C variables pointing to the heap would instantly become invalid.
To counteract this, Curica provides a rigid root stack: `vm_push_root(vm, val)` and `vm_pop_root(vm)`. Native extensions explicitly push all temporary objects to `vm->gc_roots` indices before further allocations, ensuring the GC updates their pointers dynamically during compaction.

## Native Buffer Slabs
For `Uint8Array` and `Buffer` operations, allocating tiny `malloc` chunks leads to high syscall overhead. Curica implements a memory slab allocator:
- When a `JSBuffer` is created, it dynamically chunks memory from pre-allocated 8KB `OBJ_BUFFER_DATA` slabs located within the VM arena.
- This creates strong synergy with the garbage collector; when no `JSBuffer` references a slab (traced via `buf->slab_ref`), the entire slab is natively collected during the Sweep phase, preventing memory leaks securely and efficiently.
