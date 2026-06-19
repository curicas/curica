# Curica Environment OS

**Curica** has evolved beyond a simple JavaScript runtime. It is a highly optimized, zero-dependency, C-based **Virtual Operating System Kernel** designed to execute JavaScript, Curica Bytecode, and WebAssembly natively inside a hermetically sealed POSIX environment.

Leveraging Cosmopolitan libc, Curica produces **Actually Portable Executables (APEs)** that run natively on Linux, macOS, and Windows with absolutely zero external dependencies.

## 🚀 The Microkernel OS Architecture

Curica acts as a secure OS kernel, orchestrating processes and providing system abstractions without exposing the underlying host OS.

### 1. POSIX Virtual File System (VFS)
Curica boots an isolated Virtual File System strictly compliant with the POSIX Filesystem Hierarchy Standard (FHS):
- **`/bin`**: Pre-populated with builtin, pre-compiled WASM executables providing core environment utilities.
- **`/home/user`**: The native developer workspace.
- **Pseudo-Filesystems (`/dev`, `/proc`, `/sys`)**: Dynamically generated in-memory interfaces. Features include `/dev/null`, process trees in `/proc/<pid>`, and raw PCM audio output via `/dev/dsp`.
- **In-Memory `tmpfs`**: Ephemeral ramdisk directories (`/tmp`, `/dev/shm`) ensuring blazingly fast intermediate compilation without host-disk thrashing.

### 2. JavaScript as the Native Shell Language
Instead of Bash, Curica uses **JavaScript (ES2025)** as its native orchestration and shell scripting language. JS processes can seamlessly spawn WebAssembly binaries, pipe standard I/O streams natively via the Event Loop, and manage background jobs.

### 3. Declarative Environments & Package Management
Environments are defined declaratively via a `curica.env.json` configuration. The native package manager guarantees deterministic environments for all users:
1. **Remote Binary Check:** Curica securely downloads verified pre-compiled WASM binaries from the official GitHub `packages` repository.
2. **Source-to-WASM Fallback:** If a binary is missing, Curica automatically downloads the source code and compiles it into WebAssembly locally via an embedded toolchain.

### 4. Immutable APE Freezing
When a developer runs `curica build`, the active VFS (including all configured packages, custom `--attach` host overlays, and internal pseudo-filesystems) is completely frozen and baked into the final APE bundle. The result is a single binary that instantly deserializes its own virtual OS upon execution.

## 🛡️ Zero-Bloat Security & Capabilities

Curica is designed as an impenetrable sandbox capable of safely executing untrusted AI models and logic:

- **Capability-Based Matrix:** Complex multi-user (UID/GID) systems are replaced by explicit, zero-bloat JSON capability vectors (`allow_net`, `allow_read`). Syscalls validate permissions instantly via bitwise bitmask checks.
- **Dynamic Host Permissions (JIT):** External dependencies (like massive `.gguf` AI models) can be mounted securely at runtime via CLI grants (`--grant-read`) or interactive Deno-style JIT prompts.
- **Host-Proxied Memory Mapping (`mmap`):** Massive external files are mapped directly into WASM linear memory using the host kernel's page faults, guaranteeing zero-copy streaming without C-runtime memory bloat.
- **WASM FUSE (User-Space Filesystems):** WASM modules can take ownership of VFS paths (e.g., `/mnt/s3`), intercepting POSIX reads natively without adding C code.
- **Virtual Networking & Proxies:** WASM processes cannot touch host network interfaces. Curica acts as a secure proxy router, redirecting virtual outbound TCP connections natively or mocking loopback traffic via IPC channels.

## 🧠 Core Execution Engine

- **Hermetic Memory Allocator:** A single, contiguous memory arena guarantees safety. NaN-Boxing represents all dynamic JS variables in 64 bits.
- **Register-Based Virtual Machine:** Curica's Ahead-Of-Time (AOT) compiler builds Register-Based Bytecode, eliminating transient heap pressure.
- **POSIX-Mapped Workers:** Web Workers map directly to POSIX pthreads. Utilizing `SharedArrayBuffer` and `Atomics`, threads share linear memory with zero structured-cloning overhead.
- **DAP Debugging Stub:** Developers can attach external IDEs (like VSCode) to a minimal TCP stub for full-stack debugging without embedding massive V8 UI code.
- **Native WebCrypto Bridge:** Cryptographic hashes are bridged to host-native hardware APIs natively, preventing heavy WASM library bundling.

## ✨ Supported ES2025 Features
- **Iterator Helpers:** Native lazy evaluation (`.map()`, `.filter()`).
- **Explicit Resource Management:** `using` and `await using` keywords with `Symbol.dispose`.
- **Set Methods:** O(1) mathematical operations (`Set.intersection`).

## 🛠️ Building the Kernel
To build the Curica OS APE binary:
```bash
make
```
The build process automatically retrieves the Cosmopolitan libc toolchain.

## 💻 Usage
To boot a standalone JS orchestrator:
```bash
./curica run script.js
```
To freeze the active environment into a portable binary:
```bash
./curica build curica.env.json my_app
```

---
## 📚 Documentation
For a deep dive into the microkernel architecture, refer to:
- **[Curica Environment OS Development Plan](references/development/plan/curica_environment_os_development_plan.md)**: The comprehensive roadmap outlining VFS, IPC, and Package Manager mechanics.
- **[Architecture Overview](docs/01_Architecture_Overview.md)**: Hermetic execution mechanics.
- **[Memory Management](docs/02_Memory_Management.md)**: NaN-boxing and Arena allocators.
- **[Virtual Machine & Coroutines](docs/03_Virtual_Machine.md)**: Bytecode dispatcher and async context swapping.
