---
name: WebAssembly (WASM) Integration
description: Guidelines for running WebAssembly within Curica, WASI implementation constraints, and /bin WASM executable orchestration.
---

# WebAssembly Integration in Curica

Curica treats WebAssembly (WASM) as first-class native executables in the OS environment. Unlike standard Node.js where WASM is just a module, in Curica, WASM files act as actual system binaries located in the Virtual File System under `/bin` or `/lib`.

## 1. WASM as Executables
- Built-in WASM binaries (like SQLite, standard utilities) reside in the VFS under `/bin` and `/lib`.
- WASM is spawned similar to native OS child processes. Standard output/input are mapped via IPC pipes through the `event_loop`.
- **Zero-Bloat Rule**: C-level kernel bloat should be minimized. Large dependencies (like database engines or complex libraries) must be compiled to WASM and executed dynamically in user-space, rather than linking them statically into the C microkernel.

## 2. Capability Restrictions for WASM
- All WASM modules spawned by the JS shell inherit the exact Capability Flags (`vm->allow_net`, `vm->allow_read`, etc.) of the parent process.
- Before launching any WASM binary, the runtime must verify `vm->allow_run`. If false, the kernel must explicitly reject execution.

## 3. WASI implementation
- Curica provides its own custom POSIX-compliant WASI (WebAssembly System Interface) bridge logic.
- WASI calls (`fd_read`, `fd_write`, `path_open`) MUST be routed through Curica's Virtual File System (VFS).
- A WASM module calling `path_open` for `/etc/hosts` will read the virtualized FHS tree, NOT the host machine's physical file.

## 4. Shared Memory and Zero-Copy
- Inter-process communication between WASM, the JS Shell, and the C Thread Pool should prefer `SharedArrayBuffer` for zero-copy memory access.
- When loading gigantic files (e.g., GGUF AI models for WASM inference engines), memory should be mapped directly into the WASM linear memory using the proxy `mmap` syscall to stream the file efficiently without causing OOMs in the host OS.

## 5. Garbage Collection & WASM
- References to WASM instance memory from the JS environment must be properly rooted if used in C-level callbacks.
- Never hardcode C pointers to WASM linear memory in async C callbacks, as the WASM linear memory array buffer might be relocated or swept by the JS engine's GC. Always resolve the pointer dynamically from the root ArrayBuffer reference at execution time.
