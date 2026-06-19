# Curica Environment OS: Full Development Plan

## Executive Summary
The Curica Environment is evolving beyond a simple JavaScript runtime into a full-fledged, autonomous, POSIX-like virtual operating system. Under this paradigm, the Curica Runtime acts as the "kernel" orchestrating processes (JavaScript scripts, Curica Bytecode, and WASM binaries) within a hermetically sealed environment. This document outlines the comprehensive development plan to implement the VFS, Execution Engine, and Package Manager.

---

## Phase 1: Virtual File System (VFS) Architecture

### 1.1 POSIX FHS Compliance
The foundation of the Curica OS is an isolated Virtual File System strictly compliant with the POSIX Filesystem Hierarchy Standard (FHS). 
- **The FHS Base Environment:** The base VFS includes standard directories (`/bin`, `/usr`, `/etc`, `/var`, `/tmp`, `/lib`). The `/bin` directory is pre-populated with builtin, pre-compiled WASM executables that provide the core utilities of the environment (e.g., standard Unix coreutils).
- **The `/home/user` Workspace:** The primary workspace inside the VFS is `/home/user`. Everything defined in the user's development environment JSON is populated here, precisely mirroring a real Linux OS.
- **Implementation:** Intercept all low-level `open()`, `stat()`, `read()`, and `write()` calls in C, mapping them safely to the internal FHS memory tree.

### 1.2 Pseudo-Filesystems (`/dev`, `/proc`, `/sys`)
To fully emulate a POSIX kernel securely in memory, the VFS will generate dynamic pseudo-filesystems on the fly.
- **Virtual Devices (`/dev`):** Secure, in-memory implementations of `/dev/null`, `/dev/random`, and `/dev/stdout` ensure WASM binaries can access standard POSIX devices without touching the host OS.
- **Process and System Info (`/proc`, `/sys`):** The runtime will dynamically populate `/proc/<pid>` with live process states and `/sys` with runtime configuration (e.g., available CPU threads), allowing processes to introspect their sandbox boundaries natively.
- **Virtual Audio (`/dev/dsp`):** A raw PCM ring buffer that WASM apps can `write()` to, which the runtime natively forwards to host audio hardware, providing zero-bloat sound output.

### 1.3 In-Memory Volatile Storage (Virtual `tmpfs`)
To optimize performance and eliminate host disk thrashing, ephemeral directories are strictly memory-backed.
- **Ramdisk Directories:** Paths like `/tmp` and `/dev/shm` are mounted as dynamic memory buffers in the C runtime.
- **High-Speed Compilation:** The package manager's source-to-WASM pipeline will utilize `/tmp` for intermediate artifacts, boosting build speeds dramatically and instantly freeing memory upon environment shutdown.

### 1.4 VFS Overlay Attachments
Developers require access to their host files while executing safely within the Curica OS.
- **Dynamic Overlays:** Real host folders can be overlaid or mapped onto the VFS via the `--attach` flag (e.g., `--attach /host/project:/home/user/project`).
- **Complete Isolation:** Processes executing inside the Curica Environment will *only* be able to view and interact with the overlayed VFS. Path traversal (`../`) outside the attached root is strictly prohibited.

### 1.5 Foreign Sandbox Communication (Host IPC)
While hermetic, the VFS can safely proxy trusted external host services.
- **Socket Mounting:** The `--attach` flag supports mapping host-level UNIX sockets or Named Pipes into the VFS (e.g., mapping a host database socket to `/tmp/db.sock`).
- **Secure Proxies:** Bypasses complex network setups by allowing safe, file-based IPC to the host.

### 1.7 User-Space Filesystems (WASM FUSE)
Developers can implement custom filesystems without modifying the C runtime.
- **Delegated Mounts:** A WASM module can register ownership of a VFS path (e.g., `/mnt/s3`).
- **IPC Routing:** VFS operations on that path are seamlessly paused by the C kernel and routed to the WASM process to resolve the bytes dynamically.

### 1.8 Host-Proxied Memory Mapping (`mmap`)
To handle massive external files (like 40GB AI models) without Out-Of-Memory crashes.
- **Zero-Copy Streaming:** `mmap()` calls from WASM are delegated directly to the host OS kernel, mapping the file directly into WASM linear memory via page faults.
- **Zero C-Bloat:** The runtime entirely avoids buffering or allocating memory for the file, allowing limitless file size processing within the sandbox constraints.

### 1.9 Immutable APE Freezing
Once an environment and application are finalized, the state is frozen for distribution.
- **APE Bundling:** When a developer runs the `curica build` command, the runtime captures the active VFS (the base FHS structure, pseudo-filesystems, + all `--attach` overlays).
- **Self-Bootstrapping:** This entire VFS state is baked into the final self-contained Actually Portable Executable (APE) payload. Upon execution on any target machine, the payload is deserialized, perfectly reconstructing the VFS in memory before the entrypoint process begins.

---

## Phase 2: Execution Engine & IPC

### 2.1 JavaScript as Native OS Shell Scripting
JavaScript (`.js`) is elevated to serve as the primary orchestration and shell scripting language for the Curica OS, analogous to Bash on Linux.
- **First-Class Process Management:** The JS API will provide robust abstractions for piping, redirection, and background job management.
- **Spawning Processes:** Scripts can trivially spawn WASM executables located in the `/bin` VFS directory (e.g., `const ls = system.spawn('ls', ['-la', '/home/user']);`).
- **Implementation:** Expand the `child_process` / `system` native modules in `src/builtins.c` to support streamlined WASM execution.

### 2.2 Capability-Based Security Matrix (Sandboxing)
To maintain an impenetrable sandbox without the bloat of multi-user tracking (UIDs/GIDs), the runtime relies on explicit JSON-driven capabilities.
- **Strict Boundaries:** Processes are governed by vectors such as `allow_read`, `allow_write`, `allow_net`, and `allow_spawn`.
- **Zero-Bloat Verification:** The C kernel enforces these boundaries via instantaneous bitwise checks during critical syscalls (`open`, `socket`, `exec`).

### 2.3 Dynamic Host Permissions (JIT Grants)
Securely piercing the sandbox for massive external user payloads (e.g., AI models).
- **CLI Grants:** Users can pass `--grant-read=/host/file` to mount specific external files into the VFS dynamically at runtime.
- **JIT Prompts:** If an app requests external access, the runtime pauses execution and interactively prompts the user (`Allow read access to /host/file? [y/N]`) before temporarily mounting it.

### 2.4 Virtual Networking (Network Namespaces)
WASM and JS processes never access the host OS network stack directly.
- **Proxy Routing:** The runtime intercepts all socket API requests. Authorized requests are proxied via the runtime; unauthorized requests instantly throw `EACCES`.
- **Internal Loopback:** Developers can mock network topologies natively. A request to `localhost:8080` can be mapped entirely in memory to another WASM process's `stdin` via IPC.

### 2.4 Multi-Process IPC Coordination
A true OS requires robust Inter-Process Communication (IPC) to broker data safely.
- **Communication Channels:** Processes will communicate via standard Unix-like mechanisms (Pipes, SharedBuffers, MessageChannels).
- **Implementation:** Hook POSIX pipes directly into the `event_loop.c` asynchronous polling layer for zero-copy data streaming.

### 2.5 WASM Component Model (Dynamic Linking)
To prevent binary bloat, the runtime supports lightweight dynamic linking for WASM.
- **Shared Modules:** Heavy, ubiquitous libraries (like `sqlite` or `libc`) are stored as shared modules in `/lib`.
- **Memory Efficiency:** The WASI engine dynamically links imports at runtime, allowing multiple distinct processes to execute shared logic without ballooning memory or download sizes.

### 2.6 Native Cryptography Bridge (WebCrypto)
To avoid bundling heavy crypto libraries inside user-space WebAssembly, the runtime acts as a bridge.
- **Hardware Acceleration:** The runtime exposes a thin C-bridge to the host system’s hardware-accelerated cryptographic primitives.
- **API Standardization:** These primitives are cleanly exposed to JS/WASM via the standard ECMAScript `crypto.subtle` API.

### 2.7 Virtual Graphics & UI (Framebuffer)
Supporting graphical applications without bundling heavy UI frameworks into the C kernel.
- **Virtual `/dev/fb0`:** WASM binaries write pixel data directly to a shared memory framebuffer within the VFS.
- **Host Blitting:** The runtime paints this buffer natively to the host OS window, passing input events back via a virtual `/dev/input` interface.

### 2.8 Environment Memory Snapshots (Instant Wake)
Eliminating boot times and JIT overhead for rapid-execution serverless workloads.
- **Binary Dumping:** The entire WASM linear memory, JS Heap, and VFS state can be serialized to disk.
- **Instant Resumption:** When invoked, the runtime maps the snapshot directly into memory, skipping initialization entirely.

### 2.9 Native Daemonization & Scheduled Jobs
A native mechanism to execute headless services and tasks without external cron daemons.
- **Declarative Scheduling:** The JSON configuration dictates chron schedules.
- **Asynchronous Waking:** The C event loop wakes up, spawns the necessary processes inside the VFS, and cleans them up upon completion, minimizing idle resource consumption.

### 2.10 DAP / GDB Debugging Stub
Providing robust developer tooling without embedding a massive V8-style inspector UI.
- **Lightweight Socket Stub:** The runtime exposes a tiny TCP/UNIX socket speaking the standard Debug Adapter Protocol (DAP).
- **Native IDE Integration:** Developers connect VSCode or GDB directly to the socket, allowing step-through debugging natively with zero runtime UI bloat.

### 2.11 POSIX-Mapped Workers (Zero-Serialization Threads)
High-performance parallel computing without the overhead of standard Web Workers.
- **Thread Pool Mapping:** JS/WASM worker instances map directly to the C runtime's POSIX pthread pool.
- **Shared Memory:** Relying entirely on `SharedArrayBuffer` and `Atomics`, threads share linear memory with zero data-copying or structured serialization overhead, perfectly suited for heavy AI or physics calculations.

---

## Phase 3: Package Manager & Bootstrapping

### 3.1 Declarative JSON Environments
Developers will define their environments using a simple JSON configuration file (e.g., `curica.env.json`).
- **Configuration Layout:** The JSON defines `packages` (required WASM utility binaries), `env` (environment variables), and an `entrypoint` (the initial JS "shell script").
- **Dynamic Bootstrapping:** Upon boot, the runtime parses the JSON configuration, resolves all packages, and prepares the `/home/user` workspace before passing control to the JS entrypoint.

### 3.2 Package Resolution and Fallback Compilation
The runtime ensures a unified, deterministic package resolution pipeline for all users:
1. **Remote Binary Check:** For a defined package, the runtime checks if a pre-compiled WASM executable exists in the official Curica GitHub repository (`https://github.com/curicas/curica/tree/main/packages`).
2. **Direct Download:** If the official pre-compiled WASM exists, it is securely downloaded into a local `packages` cache directory on the host.
3. **Source Compilation Fallback:** If the pre-compiled WASM does *not* exist remotely, the runtime downloads the package's raw source code, automatically compiles it into WASM locally via an embedded toolchain, and saves the binary to the local `packages` cache.

### 3.3 Source-to-WASM Embedded Toolchain
To fulfill the Source Compilation Fallback, the Curica Runtime orchestrates internal C/C++ compilation.
- **Toolchain:** The runtime will invoke a lightweight embedded toolchain (like WASI-SDK or Cosmopolitan) capable of building WASM.
- **Hermetic Builds:** The compilation pipeline produces strictly reproducible WASM binaries, relying entirely on the VFS to prevent host OS contamination.

### 3.4 Official Repository Updates
Because the runtime behaves identically for official developers and regular users, the only distinction is commit access. When official Curica Developers define new packages, their runtime compiles them from source into their local `packages` folder. By committing those WASM files to the official GitHub repository, they instantly become verified binaries available to the global ecosystem.

---

## Conclusion
By adopting the POSIX FHS, elevating JavaScript to a systems scripting language, and embedding a robust, secure, compile-from-source package manager, the Curica Runtime transcends being a simple interpreter. It becomes a universally portable, mathematically secure virtual operating system capable of running anywhere.
