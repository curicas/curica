# Feature: Capability-Based Security Matrix

## Overview
To provide impenetrable security without the heavy bloat of a multi-user permission system (UIDs, GIDs, ACLs), the Curica Runtime will implement a capability-based security matrix driven directly by the environment's JSON configuration.

## Requirements
1. **Explicit Grants:** The runtime operates on a default-deny security posture. Operations are only permitted if explicitly granted in the configuration.
2. **Capability Vectors:**
   - `allow_read`: Array of permitted VFS paths.
   - `allow_write`: Array of permitted VFS paths.
   - `allow_net`: Array of permitted domains/IPs.
   - `allow_spawn`: Boolean allowing child process orchestration.
   - `allow_env`: Array of permitted environment variables the process can read from the host.
3. **Zero-Bloat Enforcement:** The C kernel will perform a bitwise/string-prefix check against the capability matrix during sensitive system calls (`open`, `socket`, `exec`).

## Implementation Details
- Embed the capability matrix into the `Process` or `VM` struct.
- Intercept the syscall layer inside the VFS and Networking subsystems. If a capability check fails, the runtime must instantly return a `EACCES` POSIX error to the WASM or JS process.
