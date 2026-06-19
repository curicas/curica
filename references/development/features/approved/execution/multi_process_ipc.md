# Feature: Multi-Process IPC Coordination

## Overview
A true operating system requires robust Inter-Process Communication (IPC). The Curica Runtime kernel must safely broker communication between JS instances, native Curica Bytecode VMs, and WASM binaries.

## Requirements
1. **Secure Memory Boundaries**: Processes must be strictly isolated. WASM linear memory and JS garbage-collected heaps must not intersect.
2. **Communication Channels**: Provide standard Unix-like mechanisms for processes to communicate:
   - **Pipes**: For streaming byte data between processes (e.g., JS piping data into a WASM grep utility).
   - **SharedBuffers**: For zero-copy data exchange, utilizing Atomics for thread-safe locking.
   - **MessageChannels**: For structured, object-based communication between distinct JS/Curi processes.
3. **Event Loop Integration**: The main kernel event loop must poll these IPC handles efficiently alongside networking and file I/O operations.

## Implementation Details
- Implement a kernel-level IPC routing table.
- Use POSIX pipes `pipe()` locally under the hood when bridging WASM standard streams.
- Ensure that `MessageChannel` (previously stubbed in Worker Threads) correctly serializes and deserializes payloads across distinct VM instances.
