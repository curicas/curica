# Feature: Source-to-WASM Local Compilation Pipeline

## Overview
To support the "Source Compilation Fallback" mechanism, the Curica Runtime must be capable of orchestrating C/C++ source code compilation directly into WebAssembly internally.

## Requirements
1. **Embedded Toolchain**: The runtime must distribute or dynamically fetch a lightweight WASI-SDK or Clang-based compiler backend.
2. **Automated Invocation**: When a package lacks a pre-compiled WASM file in the official repository, the runtime must pull the source, invoke the compiler with the correct WASM/WASI target flags, and capture the output artifact.
3. **Deterministic Output**: The compilation pipeline must produce strictly reproducible and hermetic WASM binaries, relying entirely on the internal VFS structure to prevent host contamination.

## Implementation Details
- Evaluate distributing a minimal Cosmopolitan or WASI-SDK `clang` binary alongside Curica.
- Implement a `fork()`/`exec()` or internal threading mechanism to run the compiler toolchain across the fetched source tree silently.
