# Feature: POSIX FHS Compliance

## Overview
The Curica Runtime's Virtual File System (VFS) must be strictly compliant with the POSIX Filesystem Hierarchy Standard (FHS). This ensures that standard C/C++ programs compiled to WASM can interact with the file system exactly as they would on a traditional Linux kernel.

## Requirements
1. **Standard Directories**: Upon booting, the runtime must initialize a virtual, in-memory directory tree containing standard FHS paths:
   - `/bin` (Essential command binaries)
   - `/etc` (Host-specific system-wide configuration)
   - `/usr` (Secondary hierarchy for read-only user data)
   - `/var` (Variable data)
   - `/tmp` (Temporary files)
   - `/home/user` (The primary developer workspace)
2. **The `/bin` Base**: The `/bin` folder must be pre-populated with essential pre-compiled WASM binaries distributed by the runtime (e.g., coreutils analogues).
3. **The `/home/user` Workspace**: The runtime will set the default working directory for user operations and process spawning to `/home/user`.

## Implementation Details
- The C-level VFS subsystem must intercept all low-level `open()`, `stat()`, `read()`, and `write()` calls and remap them to the internal FHS memory tree.
- Permission bits (`chmod`, `chown`) should be mocked or strictly enforced based on the sandboxing model.
