# Feature: Dynamic Host Permissions (JIT Grants)

## Overview
While the Curica OS is hermetically sealed, users executing distributed APE apps must be able to securely inject massive external host files (like AI models or databases) into the sandbox dynamically without recompiling the executable.

## Requirements
1. **CLI Grants:** Support runtime flags (e.g., `--grant-read=<host_path>`) allowing the user to map specific external files or directories into a safe VFS path like `/mnt/host/`.
2. **JIT Interactive Prompts:** If a WASM or JS process attempts to `open()` a path outside the VFS, the runtime must intercept the `EACCES` failure, pause the event loop, and prompt the user in the terminal (e.g., `Allow read access to /host/model.gguf? [y/N]`).
3. **Session Memory:** If the user approves, the runtime dynamically mounts the file into the VFS for the duration of the session and resumes the paused process.

## Implementation Details
- Update the VFS path resolver to trigger a synchronous TTY prompt fallback when a host path is requested.
- Ensure capability flags (`allow_prompt=true`) can be toggled by the developer in `curica.env.json` so headless environments fail fast instead of hanging on invisible prompts.
