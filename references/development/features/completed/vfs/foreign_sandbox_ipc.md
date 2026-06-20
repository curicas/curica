# Feature: Foreign Sandbox Communication (Host IPC)

## Overview
While the Curica OS is hermetically sealed, developers occasionally need to interface with trusted host applications (like a local Docker daemon or PostgreSQL database).

## Requirements
1. **UNIX Socket Mounting:** Extend the `--attach` flag to support mounting host-level UNIX Domain Sockets or Named Pipes directly into the VFS (e.g., `--attach /var/run/docker.sock:/var/run/docker.sock`).
2. **Transparent Proxies:** Processes inside the sandbox interact with the mounted file exactly like a standard file descriptor, while the C runtime securely proxies the byte stream to the host socket.
3. **Security:** Only sockets explicitly mapped via the `--attach` overlay or defined in the declarative JSON environment are accessible.

## Implementation Details
- Update the VFS path resolver to identify `NODE_SOCKET` types when processing the `--attach` parameters.
- Bridge POSIX socket read/write calls from the WASM guest to the host socket natively, avoiding the virtual network stack entirely.
