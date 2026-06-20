# Feature: JavaScript as Native OS Shell Scripting

## Overview
Just as Bash serves as the connective tissue for Linux processes, JavaScript (`.js`) must serve as the primary orchestration and shell scripting language for the Curica Environment OS.

## Requirements
1. **First-Class Process Management**: The JS API must provide robust abstractions analogous to bash pipes, redirection, and background jobs.
2. **Spawning WASM**: JavaScript must be able to trivially spawn WASM executables located in the `/bin` VFS directory (e.g., `const ls = system.spawn('ls', ['-la', '/home/user']);`).
3. **Standard I/O Redirection**: The JS engine must allow redirecting `stdin`, `stdout`, and `stderr` streams between spawned processes effortlessly.
4. **Environment Context**: Scripts should have full access to the declarative environment variables defined in the JSON configuration.

## Implementation Details
- Expand the `process` and `child_process` (or a newly minted `system` namespace) native modules in `src/builtins.c` to support streamlined WASM execution.
- Implement internal `pipe()` mechanisms that hook directly into the Event Loop (`src/event_loop.c`) to stream data between processes asynchronously without blocking the main JS thread.
