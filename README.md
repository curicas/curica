# Curica JavaScript Runtime

**Curica** is a highly optimized, zero-dependency, C-based JavaScript runtime designed to execute ECMAScript 2025 code in a hermetically sealed environment.

## 🚀 Overview
Curica prioritizes security, speed, and cross-platform compatibility. It features an integrated Ahead-Of-Time (AOT) compiler that converts JavaScript source code into a custom register-based bytecode. This bytecode is then executed by the virtual machine without generating intermediate OS-level allocations or incurring massive structural overhead.

### 3. VFS Bundling
Curica can bundle `.js`, `.wasm`, and `.curi` (compiled bytecode) artifacts directly into the tail of its APE executable using a Virtual File System mapping (`vfs`). When developers run `curica build`, it automatically zips up standard `vfs/` directory resources. This makes Curica entirely self-contained.

The entire execution runtime is encapsulated into a single **Actually Portable Executable (APE)** leveraging Cosmopolitan libc, meaning the compiled `curica` binary runs natively across Linux, macOS, and Windows with absolutely zero dependencies.

## 🧠 Core Architecture
Curica's internal architecture breaks away from standard V8/SpiderMonkey conventions to achieve extreme lightweight execution.

- **Hermetic Memory Allocator:** The runtime breaks from standard libc, using a custom memory allocator that requests a single, contiguous memory block from the OS at initialization. All strings, arrays, objects, and execution stacks are sub-allocated exclusively from this arena. If execution exceeds this boundary, the VM instantly traps the operation, entirely eliminating buffer overflow vulnerabilities.
- **NaN-Boxing Representation:** All JS variables are strictly represented via 64-bit IEEE 754 NaN-boxing. Pointers, Integers, Booleans, and Symbols fit elegantly within the unused upper 16 bits of a quiet NaN structure. This allows instantaneous type identification via single bitwise masks.
- **Register-Based Virtual Machine:** Curica utilizes a Register-Based Instruction Set Architecture (ISA). During AOT compilation, liveness analysis defines a "register window" size for the function. Execution registers are pre-allocated in bulk, removing the transient heap pressure seen in traditional stack-based VMs.
- **Stackful Coroutines:** Curica manages asynchronous operations and promises directly on the C stack utilizing POSIX `ucontext_t` (`makecontext`/`swapcontext`), perfectly emulating ES2025 `async`/`await` suspension mechanics.
- **Instantaneous VM Snapshotting:** The entire memory Arena can be serialized to disk because it utilizes `MAP_FIXED` to lock virtual memory addresses. A subsequent boot maps the `.snap` file, skipping compilation and initialization completely for zero-latency startups.

## ✨ Supported ES2025 Features
Curica is continuously updated to achieve parity with the absolute latest ECMAScript specifications. Key ES2025 implementations include:

- **Iterator Helpers:** Native, lazily evaluated functional patterns like `Iterator.prototype.map()`, `.filter()`, and `.take()`.
- **Set Methods:** O(1) mathematical Set operations (`Set.intersection`, `Set.difference`, `Set.symmetricDifference`).
- **Explicit Resource Management:** Guaranteed block-scoped teardown logic via `using` and `await using` keywords with native `Symbol.dispose` and `SuppressedError` support.
- **High-Performance Numerics:** Full software-emulated `Float16Array` support.
- **Regex engine:** Powered by the Super Lightweight Regex Engine (SLRE), featuring named capture groups (`(?<name>...)`).

## 🔋 Extended Native APIs
Beyond standard ECMAScript, Curica provides experimental ultra-low-level bindings without requiring N-API addons:
- **Curica.WebView**: A Zero-Bloat Native Desktop Windowing System. Instead of compiling massive frameworks like Electron into the executable, Curica uses `cosmo_dlopen` to dynamically link the host's GTK/WebKit libraries at runtime. The UI loop is asynchronously isolated via `fork()`.
- **Curica Environment (WASI Sandbox)**: A "Docker-less" WebAssembly System Interface. Curica intercepts standard shell commands and executes pre-compiled `.wasm` toolchains (like `clang` or `python`) inside the WAMR Sandbox, securely mapping the host's `.` to a virtual `/workspace` root.
- **Curica.ML**: Built-in Machine Learning execution capabilities. LLM inference (`llama.cpp`) executes inside the WASM container natively, reading massive `.gguf` weights directly from the sandboxed Virtual Filesystem.
- **Curica.FFI**: Direct `dlopen` bridge for calling C functions from `.so`/`.dylib` libraries dynamically.
- **Curica.os**: Raw POSIX system call execution via `Curica.os.syscall`.
- **Curica.KV**: Zero-configuration log-structured persistent Key-Value store.
- **Curica.Sandbox**: Multi-tenant edge compute isolation locking down all POSIX capabilities.
- **worker_threads**: Full Node.js backwards compatibility for POSIX-backed multithreading.
- **Hot Module Replacement**: In-memory patching of execution modules using `Curica.reloadModule(path)`.
- **Standard Library Modules**: Full compatibility with standard APIs such as `fs`, `net` (TCP), `dgram` (UDP Sockets), `crypto`, `http`, `events`, `child_process`, `readline`, `stream`, and `zlib` (Compression).

## 🛠️ Building the Project

To build the highly portable **APE (Actually Portable Executable)** binary capable of running unmodified on Linux, macOS, and Windows, simply run:
```bash
make
```
The build process will automatically download the Cosmopolitan libc toolchain if you do not have it, and produce the `curica` and `vm_base` polyglot binaries.

## 💻 Usage
To execute a JavaScript file using the runtime, use the `run` command:

```bash
./curica run script.js
```

During execution, Curica operates in **Target Mode**, immediately decoding the AOT bytecode and executing instructions natively.

## 🧪 Testing
The Curica test suite is written natively in JavaScript and validates engine features against ES2025 behaviors.
To run the memory auditing Garbage Collection test (which ensures zero leaks within the hermetic arena):
```bash
./curica run tests/test_gc.js
```

To run the asynchronous stackful coroutine stress test:
```bash
./curica run tests/test_coro_stress.js
```

---
## 📚 Documentation

For an extensive technical breakdown of the engine, please refer to the following guides:

- **[Architecture Overview](docs/01_Architecture_Overview.md)**: A high-level look at the hermetic execution and polyglot APE mechanics.
- **[Memory Management](docs/02_Memory_Management.md)**: A deep dive into NaN-boxing, bump-pointer Arena allocator, and generational GC.
- **[Virtual Machine & Coroutines](docs/03_Virtual_Machine.md)**: Breakdown of the bytecode dispatcher, register-based CallFrames, closures, and async `ucontext_t` mechanics.
- **[Toolchain & CLI](docs/08_Toolchain_and_CLI.md)**: Details on the built-in formatter, test runner, and TypeScript stripper.
- **[Future Roadmap](references/development/planned/Development_Proposal.md)**: A detailed look at Phase 19 (JIT Compilation, Advanced Networking, and the Native Package Manager), along with 400 advanced exploration topics spanning Computer Vision to Quantum Computing Simulation.
