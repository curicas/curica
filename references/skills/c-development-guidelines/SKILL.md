---
name: C Development Guidelines
description: Guidelines and constraints for writing C code in the Curica Environment OS Kernel, including Cosmopolitan Libc rules and VFS compliance.
---

# C Development Guidelines for Curica OS Kernel

When modifying or adding C code to the Curica Environment OS Kernel, LLM Agents must adhere to the following rules to maintain zero-bloat and strict security:

## 1. Cosmopolitan Libc & APE Rules
- **No OS-Specific Code**: Curica compiles into an Actually Portable Executable (APE) using Cosmopolitan libc. It runs natively across Linux, macOS, and Windows. You MUST avoid OS-specific APIs (`#ifdef __linux__`, `<windows.h>`, etc.) unless strictly necessary. Rely on standard POSIX APIs, as Cosmopolitan bridges them perfectly across platforms.
- **Built-in Third Party Libraries**: Cosmopolitan ships with built-in versions of several libraries, including `zlib` and `sqlite3`. Check if Cosmopolitan has the header before downloading external dependencies.

## 2. Standard C99
- Use strictly standard C99 (`-std=gnu99`).
- Do NOT use C++ features.
- Avoid variable-length arrays (VLAs) in critical paths to prevent stack overflows.

## 3. Microkernel Zero-Bloat Philosophy
- The C codebase acts as a microkernel. Do NOT add massive libraries (like UI frameworks, database engines, or complex audio processors) to C. Provide thin, secure bridges (like `/dev/fb0` or `/dev/dsp`) and delegate the complex logic to WASM/JS user-space.

## 4. File System Operations & Security
- **Strict VFS Binding**: C code executing JS requests must NEVER touch the host file system directly using `fopen` or `open` with arbitrary strings. ALL paths must be resolved through the `vfs_resolve_path` interceptor to enforce the POSIX FHS sandbox.
- **Capability Checks**: Always validate I/O against the VM's capability boundaries (e.g., `vm->allow_net`, `vm->allow_read`) before executing POSIX syscalls.

## 5. Headers and Error Handling
- Always use header guards (`#ifndef MODULE_H`, `#define MODULE_H`).
- Prefer forward declarations of `struct VM;` over including `vm.h` in headers whenever possible.
- Never use `exit(1)` inside native module functions. Use `vm_throw_error(vm, ...)` or pass the error through to a JavaScript callback.

## 6. Build System Integration
- Whenever a new `.c` file is added to `src/`, it MUST be appended to the `SRCS` variable in the `Makefile`.
- Always verify compilation with `make clean && make`.
