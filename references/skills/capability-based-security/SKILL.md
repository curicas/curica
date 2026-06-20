---
name: Capability-Based Security
description: Guidelines for enforcing the hermetic sandbox through bitwise capability vectors instead of traditional multi-user UID/GID tracking.
---

# Capability-Based Security in Curica

Curica is designed as a secure, hermetic microkernel OS environment. It eliminates the traditional Unix concept of UID/GID user boundaries, replacing them entirely with a Capability-Based Security model.

## 1. The Sandbox Principle
By default, Curica has **zero** access to the host machine. It cannot read the host disk, access the host network, or run host binaries. Everything operates strictly within the virtualized OS environment unless explicitly granted a capability.

## 2. The Capability Vector
The `VM` struct (usually defined in `vm.h`) contains boolean flags (or bitwise vectors) determining access rights:
- `vm->allow_read`: Read access to mapped host directories.
- `vm->allow_write`: Write access to mapped host directories.
- `vm->allow_net`: Ability to open sockets or resolve DNS.
- `vm->allow_env`: Access to the host machine's environment variables.
- `vm->allow_run`: Ability to spawn sub-processes.
- `vm->allow_ffi`: Ability to use Dynamic Foreign Function Interfaces to load `.so`/`.dll`/`.dylib` files.

## 3. Enforcing Boundaries in C Code
Whenever you create a new native module (e.g., networking, file system I/O, subprocesses), you **MUST** validate the capability before performing the POSIX syscall.

```c
// Example: Attempting to open a TCP socket
static Value js_net_connect(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!vm->allow_net) {
        return vm_throw_error(vm, "Permission Denied: Requires --allow-net capability.");
    }
    // Proceed with creating the socket...
}
```

## 4. Transitive Trust & Subprocesses
- When the JS shell spawns a WebAssembly executable or a JS Web Worker, it **cannot** grant capabilities it does not possess.
- Security constraints are strictly inherited downwards.
- A Worker thread or WASM process spawned from a VM with `allow_net = false` will also have `allow_net = false`.

## 5. Bypassing the VFS is a Security Flaw
Never use raw paths directly from JS strings in standard `fopen()` or `open()`. All paths must pass through the VFS resolver (e.g., `vfs_resolve_path(raw_path)`), which enforces directory traversal (`../`) boundaries and ensures the path is legally mapped within the virtual system, strictly respecting `allow_read` and `allow_write`.
