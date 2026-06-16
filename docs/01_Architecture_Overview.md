# Curica Core Architecture Overview

Curica is a lightweight, high-performance JavaScript runtime designed from the ground up for hermetic execution and extreme portability. Unlike heavily abstracted engines like V8 or SpiderMonkey, Curica is built directly upon the **Cosmopolitan libc (APE - Actually Portable Executable)** framework.

This allows the entire JavaScript engine, compiler, and standard library to be compiled into a single `.com` binary that natively executes across Linux, macOS, Windows, FreeBSD, and OpenBSD without recompilation, external dependencies, or POSIX layer emulation overhead.

## Design Philosophy
1. **Hermetic Execution & Polyglot APE Mechanics**: Curica statically links all its dependencies natively. Using the Cosmopolitan libc, the compiled binary is an Actually Portable Executable (APE). This means the exact same binary file executes natively on Linux, macOS, Windows, FreeBSD, and OpenBSD by dynamically recognizing the host OS at the very start of execution and morphing its system calls. There are no shared library requirements (`.so`, `.dll`), meaning Curica runs deterministically on any compatible host system.
2. **Ahead-of-Time (AOT) Bytecode**: Curica heavily optimizes execution startup times by isolating parsing from execution. The compiler outputs a customized `CBC` (Curica Bytecode) binary format that can be serialized to disk and loaded instantly, entirely bypassing AST generation and tokenizer overhead on subsequent executions.
3. **C-Centric Runtime Boundaries**: Curica limits abstraction penalties by directly bridging JS objects to raw C `struct` memory utilizing NaN-boxing, providing an extremely shallow, zero-copy boundary for Native Addons (Node-API). Memory is perfectly hermetic: the VM asks the OS for a single contiguous memory region at startup and refuses to ever call `malloc` or `free` for internal structures, rendering buffer overflow exploits mathematically impossible within the sandbox.

## Component Map
- **Virtual Machine (`src/vm.c`)**: Bytecode dispatcher, microtask coordinator, and Execution Context manager.
- **Memory Management (`src/alloc.c`)**: Bump-pointer Arena allocator with native memory slab handling.
- **Compiler Pipeline (`src/compiler.c`)**: Tokenizer, Pratt Parser, and Code Generator.
- **Node-API (`src/napi.c`)**: Dynamic C-Addon ABI interface mapping to Node.js `napi_*` standards.
- **Event Loop (`src/event_loop.c`)**: Phased Libuv-style event loop orchestrator with POSIX thread pool offloading (`src/thread_pool.c`).
- **Web APIs (`src/http_module.c`, `src/websocket_module.c`)**: Native asynchronous integrations for `fetch` and WebSockets.
- **Web Workers & Atomics (`src/worker_module.c`, `src/worker_threads_module.c`, `src/atomics.c`)**: POSIX thread-backed VMs, Node-compatible `worker_threads`, and lockless `SharedArrayBuffer` memory access.
- **WebAssembly & Database (`src/wasm_module.c`, `src/wasi_module.c`, `src/sqlite_module.c`)**: Zero-overhead Wasm3 interpreter, WASI execution layer, and synchronous SQLite integration.
- **Extended OS Interfaces**: Unabstracted Native Syscalls (`src/os_module.c`), Dynamic Library FFI (`src/ffi_module.c`), Local KV Storage (`src/kv_store.c`), and Edge Sandboxing (`src/sandbox.c`).
- **Media & Hardware Bridges**: Machine Learning Inference (`src/ml_module.c`) and Native Windowing (`src/webview_module.c`).
