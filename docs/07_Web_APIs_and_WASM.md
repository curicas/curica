# Web APIs, WebAssembly, & Embedded SQLite

Curica provides zero-dependency, native implementations of high-level Web APIs, bridging the gap between Node.js server environments and browser standards.

## Web Platform APIs
Unlike traditional Node.js versions which required heavy third-party packages or complex polyfills, Curica supports isomorphic Web Platform primitives natively.

### 1. The `fetch` API (`src/http_module.c`)
- Implements `fetch()`, `Request`, `Response`, and `Headers` interfaces directly in C.
- Interacts synchronously with Curica's event loop via `el_watch_fd` using POSIX non-blocking sockets.
- Eliminates context-switching overhead compared to thread-based HTTP clients.

### 2. WebSockets (`src/websocket_module.c`)
- Native implementation of the RFC6455 WebSocket protocol.
- Directly handles framing, masking, and continuation protocols in C.
- Binds to the event loop's Poll phase for instantaneous bidirectional communication.

### 3. Web Workers (`src/worker_module.c`)
- Emulates the browser `Worker` specification by spinning up entirely isolated Curica Virtual Machines in background POSIX threads.
- Message passing (`postMessage`) securely transfers data structures across VM boundaries without shared memory locking overhead, using thread-safe IPC queues that wake the main event loop dynamically.

### 4. Node.js `worker_threads` Parity (`src/worker_threads_module.c`)
- Provides strict backwards compatibility with Node.js ecosystem packages relying on the `worker_threads` core module.
- Scaffolds the `Worker` and `MessageChannel` APIs seamlessly over Curica's native POSIX thread pools, enabling standard Node patterns without external polyfills.

## WebAssembly (WASM) Integration (`src/wasm_module.c`)
Curica embeds the lightweight `Wasm3` interpreter to natively execute `.wasm` bytecode files.
- Provides the standard `WebAssembly.instantiate` API.
- **Memory Bridging**: Curica exposes Wasm3's linear memory instance using a zero-copy external `JSBuffer` bound to `result.instance.exports.memory.buffer`. The GC explicitly ignores external buffers, allowing bidirectional, zero-overhead mutation of WebAssembly slabs directly from JavaScript typed arrays.
- **Advanced SIMD & Threads (`src/wasm_ext.c`)**: Scaffolding for `WebAssembly.SIMD` and `WebAssembly.threads` enables execution of high-performance 128-bit vector workloads directly mapped across multiple POSIX threads.

## WASI (WebAssembly System Interface) Execution Layer (`src/wasi_module.c`)
- Exposes a native `WASI` global constructor.
- The `start(instance)` method allows Curica to securely bridge standard POSIX OS capabilities (stdin, stdout, fs access) to pre-compiled Rust, C, and C++ Wasm binaries.

## Embedded SQLite (`src/sqlite_module.c`)
To provide immediate persistence, the SQLite amalgamation is statically linked into the Curica binary.
- Accessible via the global `Database` constructor.
- Provides a synchronous, extremely fast embedded database API.
- Leverages explicit GC root pinning (`vm_push_root`) to ensure that database queries generating hundreds of temporary JavaScript objects during row extraction remain perfectly memory-safe against aggressive Nursery Garbage Collection compactions.
