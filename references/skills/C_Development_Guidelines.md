# C Development Guidelines for Curica Runtime

When modifying or adding C code to the Curica JavaScript Runtime, LLM Agents must adhere to the following rules:

## 1. Cosmopolitan Libc & APE Rules
- **No OS-Specific Code**: Curica compiles into an Actually Portable Executable (APE) using Cosmopolitan libc. It runs natively across Linux, macOS, and Windows. You MUST avoid OS-specific APIs (`#ifdef __linux__`, `<windows.h>`, etc.) unless strictly necessary. Rely on standard POSIX APIs, as Cosmopolitan bridges them perfectly across platforms.
- **Built-in Third Party Libraries**: Cosmopolitan ships with built-in versions of several libraries, including `zlib` and `sqlite3`. Check if Cosmopolitan has the header before downloading external dependencies (e.g., `#include "third_party/zlib/zlib.h"` works out-of-the-box).

## 2. Standard C99
- Use strictly standard C99 (`-std=gnu99`).
- Do NOT use C++ features.
- Avoid variable-length arrays (VLAs) in critical paths to prevent stack overflows. Use `malloc` or arena allocations for dynamic sizing.

## 3. Headers and Circular Dependencies
- Always use header guards (`#ifndef MODULE_H`, `#define MODULE_H`).
- Prefer forward declarations of `struct VM;` over including `vm.h` in headers whenever possible to reduce circular dependency compilation errors.

## 4. Error Handling
- Never use `exit(1)` inside native module functions. If a function fails, use `vm_throw_error(vm, create_error("ErrorName", create_string("msg", len)))` or pass the error through to a JavaScript callback.
- Always check pointers returned by `malloc`, `realloc`, and file operations.

## 5. File System Operations
- The root of execution for bundled executables may be virtualized (`vfs/`). 
- When building file operations, ensure they are compatible with both absolute host paths and relative virtualized paths if interacting with sandboxed environments (like WAMR).

## 6. Build System Integration
- Whenever a new `.c` file is added to `src/`, it MUST be appended to the `SRCS` variable in the `Makefile`.
- Always verify compilation with `make clean && make`.
