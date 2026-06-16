# Curica Runtime Architecture Reference

This document provides an overview of the internal components, design philosophy, and execution model of the Curica Runtime. Built natively upon the Cosmopolitan libc, Curica provides a hermetic, cross-platform JavaScript engine independent of external dependencies like V8 or libuv.

## 1. Memory Model & Data Representation

Curica utilizes **NaN-Boxing** to achieve highly memory-efficient representations of JavaScript `Value` primitives.
- All JavaScript primitives are represented as standard IEEE 754 double-precision 64-bit floating point numbers.
- **Pointers** (Objects, Arrays, Functions, Promises, Strings) and specific bit flags (Integers, Booleans) are stored inside the NaN space.
- This design eliminates pointer indirection overheads for standard numeric operations, creating ultra-fast arithmetic processing directly within the VM loop.

### Garbage Collection
A customized **Mark-and-Sweep Garbage Collector (GC)** manages dynamic allocation memory.
- Root identification scans the VM stack frame, the core Global Object, and unresolved microtask queues.
- `vm_run_gc(vm)` initiates memory reclamation when allocation thresholds trigger pressure limits.
- Phase 17 secured sensitive native buffers into isolated POSIX memory zones that are scrubbed aggressively upon de-referencing.

---

## 2. Compilation Pipeline

### Lexical Analysis & Pratt Parsing
Source text is initially fed to `compiler.c`'s Lexer, which synthesizes standard ES2025 tokens including Top-Level Awaits (`import`/`export`). The AST generation is powered by a recursive descent parser heavily utilizing the **Pratt Parsing** algorithmic technique for highly efficient operator precedence tracking.

### Scope Resolution
Prior to bytecode generation, `resolve_identifiers` performs a deep AST traversal to identify variable capturing requirements, explicitly dictating whether functions possess closure boundaries or not (`is_captured`).

- **ESM Blocks**: Block nodes marked `is_inline` bypass child scope instantiation, ensuring dynamic `import` identifiers are propagated strictly into their parent context.

### CBC (Curica Bytecode) Emission
The AST translates directly into linear **Curica Bytecode (CBC)**. Unlike standard stack machines, CBC leverages a **Register-Based Virtual Machine** architecture. Instruction generation operates through allocation sequences mimicking CPU registers, dramatically reducing VM instruction branching overhead during loop dispatch.

---

## 3. Virtual Machine (VM) Execution Loop

The execution core uses **Direct Threading via Computed Gotos**.
- By mapping byte-codes straight into static jump tables (`&&do_op_...`), the VM eliminates heavy switch statements, jumping straight from one execution logic block to the next via `DISPATCH()` and `NEXT()`.

### Asynchronous Execution & Coroutines
Curica achieves high-concurrency without complex threading locks through **Stackful Coroutines** (`ucontext_t`).
- **`vm_call_function`**: Determines evaluation trajectory. If a function (`__wrapper__`) is `is_async`, it dynamically spins up an isolated `VMCoroutine` block containing an independent register array and hardware execution context stack.
- `OP_AWAIT`: Executes `vm_coro_suspend(vm, await_val)`, instructing the environment to switch context (`swapcontext`) back to the parent orchestrator loop while pushing resumption to the microtask queue.

### Native-to-JS Calling Convention
Invoking JavaScript from native C code (such as during test runner initialization or `require()` module wrapping) uses `vm_call_function`. 
- To avoid corrupting the active local execution stack during nested module resolutions, `vm_call_function` dynamically calculates available register offsets based on the top-most active frame (`vm->frames[vm->frame_count - 1]`) rather than static unmanaged VM counters.

### Loose Equality Evaluation
Curica implements ECMAScript loose equality (`==`) carefully through `compare_values`. 
- `null == undefined` evaluates accurately.
- `NaN`-boxed object pointers (such as closures or arrays) do not naively cast to `0.0` when compared to primitives, ensuring strict JS evaluation semantics during high-order functional executions like callback verifications (`fn == null`).

---

## 4. The Event Loop & Standard Library

Built manually, without `libuv`, Curica replicates Node.js's event loop semantics entirely via POSIX `poll()` structures inside `event_loop.c`.
- **Timers (`el_set_timeout`)**: Heap-managed priority execution lists for `setTimeout` resolution.
- **Microtask Queues**: Drained unconditionally following every functional call stack exhaustion, handling Promises securely.
- **Thread Pool**: Employs blocking tasks (such as intensive cryptographic operations in `crypto_module.c` or complex `fs` algorithms) to POSIX worker threads operating independently, unblocking the VM. 

## 5. Extensibility

Curica provides near 1:1 parity with the core Node.js backend.
- Node.js APIs (`fs`, `net`, `http`, `crypto`, `child_process`, `worker_threads`) are polyfilled securely via direct mapping onto the host platform.
- **Node-API (N-API)**: A C bridge layer providing seamless binary interoperability. Rust/C++ external libraries compiled against Node.js headers successfully execute natively under Curica.
- **Web APIs**: Integrated `fetch`, `WebSocket`, and WASM runtimes ensure browser isomorphic logic executes securely server-side without external dependencies.
