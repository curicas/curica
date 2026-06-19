# Feature: POSIX-Mapped Workers (Zero-Serialization Threads)

## Overview
Parallel compute is essential, but standard Web Workers rely on heavy "Structured Cloning" to pass messages. Curica will implement high-performance, zero-copy threading.

## Requirements
1. **Direct Thread Mapping:** Map JavaScript/WASM worker instances directly to the host's underlying POSIX pthread pool.
2. **Shared Memory Maximization:** Emphasize the use of `SharedArrayBuffer` and `Atomics` to allow multiple isolated threads to safely interact with the exact same block of linear memory.
3. **Zero-Copy Architecture:** By avoiding structured cloning, developers can pass gigabytes of data (e.g., matrices for AI inference) between threads instantaneously.

## Implementation Details
- Expose a minimal `Worker` API in JS that delegates instantly to a `pthread_create` equivalent in the C runtime.
- Ensure the Garbage Collector and WASM linear memory bounds are thread-safe and properly locked when `SharedArrayBuffer` references are passed across process boundaries.
