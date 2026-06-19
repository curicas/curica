# Feature: User-Space Filesystems (WASM FUSE)

## Overview
To allow developers to mount custom filesystems (like an S3 bucket, a remote FTP server, or a `.zip` archive) without bloating the C runtime with network/archive libraries, Curica will support WASM-driven VFS nodes.

## Requirements
1. **Delegated I/O:** When a WASM module registers a user-space filesystem, it takes ownership of a specific VFS path (e.g., `/mnt/s3`).
2. **IPC Routing:** When the C runtime intercepts a POSIX `read()`, `write()`, or `stat()` on that path, it suspends the caller and routes the request via IPC to the registered WASM module.
3. **Resolution:** The WASM module executes the custom logic (e.g., fetching bytes over the virtual network) and returns the payload to the C runtime, which resumes the caller.

## Implementation Details
- Extend the VFS `Node` struct to support a `NODE_FUSE` type, containing a reference to the owning WASM instance and callback pointers.
- Ensure the event loop handles these asynchronous I/O delegations efficiently without deadlocking.
