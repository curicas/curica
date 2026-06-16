# Curica Environment & WASI Developer Environments

**Category**: Advanced_Features
**Status**: Completed

**State**: Approved
**Difficulty**: High
**Architecture**: WAMR Sandboxed WASI Execution + Virtual Filesystem

Expanding Curica into a full-fledged "Docker-less" container environment. By leveraging WebAssembly System Interface (WASI) and the WAMR fast interpreter, Curica allows developers to spawn entirely isolated, cross-platform development environments using a single APE binary. 

## 1. The "Docker-less" Container Concept

Modern development environments rely heavily on Virtual Machines, WSL, or Docker to isolate dependencies. These solutions are extremely heavy, requiring gigabytes of disk space and operating-system-level virtualization.
Curica solves this by running toolchains purely in user-space via WebAssembly. A developer on Windows, Linux, or macOS will get the exact same "Linux-like" isolated WASI environment without needing to install Docker or any virtualization software.

## 2. The Configuration Protocol (`curica.env.json`)

To spawn a dev environment, a developer defines their required toolchain in a declarative JSON file placed in their project root.

```json
{
  "name": "My Python/Rust Project",
  "tools": {
    "python": "3.11",
    "rustc": "1.72",
    "clang": "16.0"
  }
}
```

When Curica detects this file, it automatically reaches out to a centralized WASM registry (like WAPM or a custom Curica CDN) and downloads pre-compiled `.wasm` binaries (e.g., `python.wasm`, `rustc.wasm`) into a local project cache. By downloading pre-compiled WebAssembly binaries instead of compiling from C-source, environment startup is virtually instantaneous.

## 3. Curica Environment (REPL) Integration

The interactive dual-mode Curica REPL (Curica Environment) acts as the primary interface for this container. It functions simultaneously as a JavaScript evaluator and a pseudo-bash shell.

- **Command Interception**: When a developer types `python main.py` or `rustc build`, the shell intercepts the command.
- **WASM Dispatch**: Instead of looking at the host OS's `PATH`, Curica loads the cached `python.wasm` binary directly into the high-performance WAMR engine.
- **Instant Execution**: The WASM binary executes at near-native speeds, parsing and running the user's scripts just as a native tool would.

## 4. Sandboxing & The Virtual Filesystem (VFS)

Security and isolation are guaranteed through Curica's strict implementation of WASI.

- **Zero-Trust Isolation**: The WASM tools run inside the WAMR sandbox. They have absolutely no access to the host machine's root filesystem, network stack, or hardware unless explicitly bridged by Curica.
- **VFS Mapping**: Curica utilizes its native Virtual Filesystem router (`src/vfs_module.c`) to map the current project workspace directory into the WASM environment as the virtual `/workspace` root. 
- **The Result**: A developer can run `python main.py` and the Python interpreter can read and write files perfectly within the project folder. However, if the Python script attempts to read the host's `~/.ssh/id_rsa` or `/etc/passwd`, it will instantly fail with a `Permission Denied` error, as those paths simply do not exist within the WASI virtual container.

This creates a perfect, lightweight, and incredibly secure development environment entirely contained within the Curica runtime.

## 5. On-the-Fly WASM Compilation (`clang.wasm`)

To maintain the ultra-lightweight footprint of the Cosmopolitan APE binary, Curica does *not* statically bundle a massive 150MB+ LLVM/Clang compiler directly into the executable. Instead, it leverages the precise container ecosystem outlined above to provide native, on-the-fly C/C++ to WebAssembly compilation.

### The Compilation Workflow
- **Provisioning**: To compile a C project (e.g., the `blink` x86 emulator) into WASM, a developer simply declares `"clang": "latest"` in their `curica.env.json`. Curica automatically provisions the `clang.wasm` compiler and the required WASI libc headers.
- **Execution**: The developer drops into the Curica REPL and types a standard build command: `clang -target wasm32-wasi blink.c -o blink.wasm`.
- **The Magic**: Curica intercepts this shell command and loads `clang.wasm` into the WAMR engine. The Clang compiler runs entirely inside the WASM sandbox, reads `blink.c` via the Virtual Filesystem, and outputs the compiled `.wasm` binary directly into the project workspace.
- **Zero-Install**: Developers can compile massive C/C++ codebases into WebAssembly without ever installing GCC, MSVC, or Xcode on their host machine. The entire build pipeline is self-contained inside the Curica APE binary.
