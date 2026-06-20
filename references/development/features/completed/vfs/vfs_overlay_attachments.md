# Feature: VFS Overlay Attachments

## Overview
To allow developers to edit code on their host machine while executing it safely within the hermetic Curica OS, the runtime must support folder overlay attachments via the `--attach` flag.

## Requirements
1. **Command Line Interface**: Support `--attach <host_path>:<vfs_path>` syntax.
2. **Dynamic Overlay**: When `--attach` is provided, the VFS subsystem must lazily map the designated host directory over the designated VFS path. 
3. **Workspace Population**: A typical use case will involve mapping a host project folder directly into `/home/user/project` inside the VFS.
4. **Strict Isolation**: A process running inside the environment must **never** be able to traverse `../` outside of its attached root. The rest of the host file system must remain invisible and inaccessible.

## Implementation Details
- Update the VFS path resolver to check if a requested path falls within the prefix of an `--attach` mapping.
- If it does, translate the VFS path to the absolute host path before executing the underlying OS-level I/O operation.
- Implement security boundaries to prevent path traversal attacks (e.g., resolving all symlinks securely).
