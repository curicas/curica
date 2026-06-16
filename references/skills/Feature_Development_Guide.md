# Curica Runtime Feature Development Guide

The Curica Runtime is a high-performance JavaScript engine and operating environment written entirely in C. It is designed to act as a drop-in replacement for Node.js, providing an Actually Portable Executable (APE) format using Cosmopolitan Libc.

This guide outlines the core architecture and capabilities of Curica to aid in future feature proposals and developments.

## Architecture & Core Mechanics

### 1. Actually Portable Executable (APE)
Curica relies heavily on Cosmopolitan Libc to output an executable format that natively runs across Linux, macOS, and Windows. When building new features, the standard POSIX API provided by Cosmopolitan should be utilized, avoiding OS-specific APIs when possible.

### 2. Custom JavaScript VM
The engine uses a custom C-based interpreter with zero-latency startup through memory snapshot booting (`curica.snap`). It uses a structured bytecode representation `CBC` (Curica Bytecode) and avoids JIT delays, supporting advanced functionality:
- Full prototypical inheritance.
- Closure capture and scope chains.
- Asynchronous Promise mapping directly into the C event loop.

### 3. VFS Bundling
Curica can bundle `.js`, `.wasm`, and `.curi` (compiled bytecode) artifacts directly into the tail of its APE executable using a Virtual File System mapping (`vfs`). When developers run `curica build`, it automatically zips up standard `vfs/` directory resources. This makes Curica entirely self-contained.

### 4. Memory Management & Garbage Collection
Curica uses a Mark-and-Sweep Garbage Collector (GC) alongside a custom arena allocator.
- **GC Roots**: Native modules must be extremely careful when saving callbacks or promises. Any internal pointers MUST be tracked by exposing a `mark_gc_roots` function that is registered within `alloc.c` and `event_loop.c` to prevent `use-after-free` access violations.
- **Microtasks**: The GC safely executes between tick drains and macro-task evaluation.

## Key Capabilities

### The Event Loop
A custom event loop (`src/event_loop.c`) maps epoll/poll interfaces, running single-threaded for I/O bound callbacks while maintaining `setTimeout`/`setInterval`.

### Built-in Thread Pool
For blocking operations (such as cryptography, file I/O, compression), a POSIX thread pool (`src/thread_pool.h`) allows C native modules to offload intensive tasks, signaling back completion to the main event loop thread via pipe wake-ups.

### WAMR (WebAssembly Micro Runtime)
Curica contains a built-in WASM engine allowing direct instantiation and execution of WebAssembly binaries, enabling execution of low-level C/Rust modules bundled into the runtime.

### Typescript Stripping
Native, fast TypeScript type-stripping inside the C core, meaning `.ts` files execute out-of-the-box exactly like `.js` files without `tsc` compilation.

## Standard Library Modules
Curica implements standard Node.js APIs to ensure drop-in compatibility.
* **Network & Sockets**: `net` (TCP), `dgram` (UDP), `http`, `websocket`.
* **File System**: `fs` (sync & async callbacks), `path`.
* **OS & System**: `os`, `child_process` (with `execSync` & WebAssembly exec bindings).
* **Cryptographic**: `crypto` (AES, RSA, hashing) wrapped using `mbedTLS`.
* **Concurrency**: `worker_threads` (for parallel JS execution via `pthread`).
* **Compression**: `zlib` (Deflate/Inflate leveraging Cosmopolitan's zlib toolchain).
* **Database**: `sqlite` (In-memory and file-based data storage).
* **Native WebView**: `webview` (Cross-platform webview bindings for desktop applications).

## Creating New Native Modules
When proposing features involving C implementations:
1. Implement the API logic in `src/feature_module.c`.
2. Wrap it in a Javascript wrapper `scripts/feature.js`.
3. Register it inside `src/builtins.c` for module resolution.
4. If doing async work, use `tp_submit(WorkItem*)` for safe thread-pool dispatching, and implement GC tracking hooks if storing callback `Value` pointers.
