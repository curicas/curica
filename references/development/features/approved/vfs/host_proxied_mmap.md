# Feature: Host-Proxied Memory Mapping (mmap)

## Overview
Loading massive user-provided files (such as 40GB LLM `.gguf` weights) directly into WebAssembly linear memory using standard `read()` calls will cause Out-Of-Memory (OOM) crashes. The runtime must proxy `mmap` calls directly to the host OS.

## Requirements
1. **Zero-Copy Architecture:** When a WASM process invokes `mmap()` on a dynamically granted host file, the C kernel must delegate the mapping directly to the host OS (Linux/macOS).
2. **Page Fault Delegation:** The host kernel will handle paging the physical file from the SSD into the WASM linear memory window in tiny chunks, completely bypassing the C runtime's memory allocator.
3. **Security Constraints:** The `mmap` proxy must enforce read-only mapping unless the user explicitly granted write permissions (`--grant-write`), ensuring host files are not accidentally corrupted by the sandbox.

## Implementation Details
- Hook the WASI `mmap` syscall handler in the WASM engine.
- Validate that the requested VFS file descriptor is backed by a `NODE_ATTACHED` host file.
- Invoke the native `mmap()` syscall on the underlying host file descriptor, mapping it into the specific WASM linear memory offset requested.
