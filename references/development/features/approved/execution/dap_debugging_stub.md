# Feature: DAP / GDB Debugging Stub

## Overview
Developers require robust debugging capabilities, but embedding a large V8-style inspector engine would massively bloat the runtime. The Curica OS will provide a minimalistic stub that integrates natively with external IDEs.

## Requirements
1. **Lightweight Protocol:** Implement a tiny C-based stub that speaks either the Debug Adapter Protocol (DAP) or the GDB Remote Serial Protocol (RSP).
2. **Socket Communication:** When the runtime is booted with a `--debug` flag, it pauses execution and binds to a local TCP socket or UNIX domain socket, waiting for an IDE (e.g., VSCode) to attach.
3. **VM Hooks:** The C kernel must provide minimal hooks into the JS and WASM engines to support pausing execution, reading/writing memory, and inspecting call stacks, without rendering UI.

## Implementation Details
- Hook into the main Event Loop to allow a dedicated debugging thread to process incoming socket commands without blocking the paused VM.
- Map the parsed commands directly to the low-level `wasm_env` or JS engine inspection APIs.
