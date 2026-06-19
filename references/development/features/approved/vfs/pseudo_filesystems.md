# Feature: Pseudo-Filesystems (/dev, /proc, /sys)

## Overview
To provide a truly hermetic and highly compatible POSIX environment, the Curica Runtime must emulate standard Unix pseudo-filesystems dynamically in memory. This allows WASM binaries and JS scripts to interface with "hardware" and system states without ever piercing the sandbox to reach the host OS.

## Requirements
1. **`/dev` (Device Interfaces):**
   - Implement virtualized null, zero, and random devices: `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`. Reading/writing these must be handled securely in C memory.
   - Map standard I/O to virtual TTYs: `/dev/tty`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`.
2. **`/proc` (Process Information):**
   - The runtime kernel must dynamically generate directories for active processes (e.g., `/proc/<pid>`).
   - Allow processes to read their own memory limits and status via `/proc/self`.
3. **`/sys` (Runtime Parameters):**
   - Expose core runtime configuration as readable files, such as `/sys/cpu/count` (number of allocated thread-pool threads) or `/sys/memory/limit`.
4. **`/lib` & `/usr/lib` (Shared Libraries):**
   - Store dynamically linkable `.wasm` modules. The WASI engine must be configured to resolve dynamic imports from these directories.
5. **`/etc` (Virtual Configuration):**
   - Mock system-wide configuration files like `/etc/hosts` to allow the JS orchestrator to override network routing internally before it reaches the host's actual network stack.

## Implementation Details
- The VFS `open()` and `read()` interceptors must check if a path begins with `/dev`, `/proc`, or `/sys`. If so, instead of querying an attached overlay or physical file, they should trigger internal C callback functions that dynamically generate the file byte-streams on the fly.
