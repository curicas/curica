# Feature: Immutable APE Freezing

## Overview
When a developer finishes setting up their environment and application, the `curica build` command should freeze the current state of the VFS into a standalone Actually Portable Executable (APE).

## Requirements
1. **State Capture**: The build command must recursively traverse the live VFS, including the base Linux-like structure (`/bin`, `/etc`) and any active `--attach` overlays (`/home/user/project`).
2. **Binary Injection**: The traversed files and directories must be serialized into an efficient archive format (e.g., ZIP) and injected into the target APE binary payload.
3. **Self-Bootstrapping**: When the bundled APE is executed on a target machine, the runtime must deserialize the archive payload and reconstruct the exact same VFS in memory before kicking off the entrypoint process.

## Implementation Details
- Extend the current bytecode serialization logic in `src/main.c` to support full directory archiving.
- Optimize the VFS in-memory boot time to prevent high latency when running bundled APE executables.
