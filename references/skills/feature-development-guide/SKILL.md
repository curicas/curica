---
name: Curica Architecture and Feature Development
description: A high-level architectural overview of the Curica Environment OS Kernel, its VFS, security capabilities, and package manager.
---

# Curica Environment OS Feature Development Guide

The Curica Runtime is no longer just a JavaScript engine; it is a high-performance, zero-bloat **Microkernel Operating System** written entirely in C. It executes JavaScript, Curica Bytecode, and WebAssembly natively inside a hermetically sealed POSIX environment.

This guide outlines the core architecture to aid LLM Agents in future feature proposals and developments.

## Architecture & Core Mechanics

### 1. The Virtual File System (VFS) & FHS Compliance
Instead of exposing the host file system, Curica boots an isolated memory tree enforcing the POSIX Filesystem Hierarchy Standard (FHS):
- **`/bin`**: Holds builtin WASM executables.
- **`/home/user`**: The native developer workspace populated by the `curica.env.json` configuration.
- **Pseudo-Filesystems**: Features like `/dev/null`, `/dev/dsp` (Virtual Audio), and `/proc/<pid>` are dynamically generated in C memory, bypassing the host OS entirely.
- **mmap Proxy**: Massive external files (like AI `.gguf` models) are safely proxied directly to the host OS `mmap` syscall for zero-copy linear memory mapping, preventing OOM crashes.

### 2. Actually Portable Executable (APE) & Immutable Freezing
Curica relies on Cosmopolitan Libc to output an executable format that natively runs across Linux, macOS, and Windows. 
When developers run `curica build`, the runtime freezes the entire active VFS state and zips it into the APE payload, creating a completely self-contained bootable OS image.

### 3. JavaScript as Native Shell Scripting
The engine uses JavaScript (ES2025) as the primary systems scripting language. It bypasses bash to natively spawn WASM processes from `/bin` and orchestrate IPC via standard pipes.

### 4. Zero-Bloat Execution Engine
- **Hermetic Memory Allocator**: A Mark-and-Sweep Garbage Collector alongside a custom bump-pointer arena allocator.
- **POSIX-Mapped Workers**: JS Workers map directly to POSIX pthreads, sharing linear memory via `SharedArrayBuffer` for zero-copy parallel processing.
- **DAP Debugging Stub**: Curica acts as a tiny debug server via a socket, deferring complex debugging UI to external IDEs.
- **Capability-Based Security**: Security boundaries are enforced via simple bitwise capability vectors (e.g., `allow_net`, `allow_read`), entirely removing the bloat of multi-user UID/GID tracking.

## Standard Native Modules
Curica implements standard APIs adapted securely for the VFS sandbox:
* **Network**: `net` (TCP), `dgram` (UDP), `http`. Proxied via Virtual Network Namespaces.
* **File System**: `fs` (sync & async callbacks), `path`. Strictly bounded to the VFS.
* **OS & System**: `os`, `child_process` (for spawning WASM).
* **Cryptographic**: `crypto` exposed via WebCrypto mappings bridged to hardware acceleration.
* **Database**: Handled dynamically in user-space via dynamically linked WASM SQLite binaries from `/lib`.

## Creating New Native Features
When proposing features involving C implementations:
1. Ensure the feature does not bloat the C kernel. If it can be implemented in user-space WASM or JS, it should be.
2. Implement the API logic in `src/feature_module.c`.
3. Wrap it in a Javascript wrapper `src/js/feature.js`.
4. Register it inside `src/builtins.c` for module resolution.
5. If doing async work, use `tp_submit(WorkItem*)` for safe thread-pool dispatching, and implement GC tracking hooks if storing callback `Value` pointers.
