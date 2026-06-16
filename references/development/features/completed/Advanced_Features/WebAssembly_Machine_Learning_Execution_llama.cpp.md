# WebAssembly Machine Learning (llama.cpp) Execution

**Category**: Advanced_Features
**Status**: Completed

**State**: Approved
**Difficulty**: Medium
**Architecture**: Containerized WASM Execution via WAMR + VFS Model Sandboxing

Integrating high-performance Machine Learning frameworks into Curica. Instead of statically linking to `llama.cpp` directly via complex C wrappers, Curica natively executes `llama.cpp` as a standardized, containerized WebAssembly (`wasm32-wasi`) application.

## 1. Container Integration

`llama.cpp` is not treated as a special edge case; it integrates identically to our standard developer tools (like Python or Rust). 
To add AI capabilities to a project, a developer simply adds it to their `curica.env.json` configuration file:

```json
{
  "name": "My AI Application",
  "tools": {
    "llama.cpp": "latest"
  }
}
```

Upon initialization, Curica downloads the pre-compiled `llama-cli.wasm` binary into the local project cache, meaning developers get instant LLM inference capabilities without ever needing to install heavy C++ toolchains, MSVC, or Xcode.

## 2. JavaScript Interoperability

Developers interact with the Machine Learning models directly through JavaScript using asynchronous child processes.

- **Process Spawning**: A JavaScript app uses standard system primitives to spawn `llama.cpp` entirely within the WAMR fast interpreter.
- **Secure Asynchronous Streaming**: Curica captures the generated tokens asynchronously via the WASM binary's `stdout`.
- **Fault Isolation**: Because inference happens inside the WebAssembly sandbox, memory safety is guaranteed. If a corrupted model causes `llama.cpp` to segfault, the WAMR sandbox simply terminates the isolated process, leaving the main Curica JS engine perfectly stable.

```javascript
// Example API Concept
const llama = Curica.spawn('llama-cli', ['-m', '/models/llama3.gguf', '-p', 'Explain WebAssembly']);

llama.stdout.on('data', (token) => {
    process.stdout.write(token);
});
```

## 3. VFS Model Sandboxing

Machine Learning models in the `.gguf` format are massive (often 5GB to 50GB). WAMR handles these efficiently by leveraging memory-mapped files (`mmap`), but security is crucial.

- **Strict Path Mapping**: Curica's Virtual Filesystem router (`src/vfs_module.c`) mounts a specific host folder or file directly into the WASM container inside the VFS. This allows the WASM binary to access the model file as if it were a local file within its own filesystem.
- **Read-Only Access**: The WASM binary is granted explicit, read-only access to the massive weights. It remains completely sandboxed and is physically blocked from reading any other data on the host OS.

## 4. Performance & Hardware Acceleration

- **SIMD128 Optimization**: By compiling `llama.cpp` to WebAssembly with 128-bit SIMD vector instructions enabled, the LLM inference executes purely on the CPU at near-native speeds.
- **Future WASI-NN Roadmap**: While the initial phase relies entirely on CPU/SIMD for maximum zero-config portability across all devices, future phases will explore **WASI-NN** (Neural Network) bindings. This will eventually allow the WAMR engine to pass matrix multiplication workloads through the WASM boundary directly to the host's GPU (CUDA/Metal) while maintaining strict sandbox isolation.
