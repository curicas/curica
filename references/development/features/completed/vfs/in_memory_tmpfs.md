# Feature: In-Memory Volatile Storage (Virtual tmpfs)

## Overview
To optimize performance and avoid thrashing the host's physical storage, the Curica VFS will support strictly volatile, in-memory directories.

## Requirements
1. **Volatile Mounts:** Directories like `/tmp` and `/dev/shm` must be mounted as pure ramdisks.
2. **High-Performance:** Read/Write operations inside these directories should map directly to dynamic memory allocations in the C runtime rather than hitting the host OS filesystem APIs.
3. **Ephemeral Lifecycle:** When the Curica Environment shuts down or the entrypoint process terminates, all data within these mounts is instantly and cleanly destroyed via memory deallocation.
4. **Toolchain Optimization:** The Package Manager's internal source-to-WASM compilation pipeline should default to using `/tmp` for intermediate object files to dramatically speed up build times.

## Implementation Details
- Extend the VFS node structure to differentiate between `NODE_ATTACHED` (host overlay) and `NODE_VOLATILE` (heap-backed).
- Implement dynamic chunk allocation for volatile file writing to prevent out-of-memory crashes on large files.
