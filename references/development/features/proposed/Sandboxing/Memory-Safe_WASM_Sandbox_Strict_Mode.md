# Memory-Safe WASM Sandbox Strict Mode

**State**: Proposed
**Difficulty**: High
Isolating the embedded Wasm3 interpreter into an unprivileged OS ring with intensely restricted bounds checking. Guarantees malicious WebAssembly binaries absolutely cannot break out of the linear memory buffer.
