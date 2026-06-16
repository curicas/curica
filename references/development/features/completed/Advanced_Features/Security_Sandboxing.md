# Security Sandboxing

**Category**: Advanced_Features
**Status**: Completed
**Difficulty**: Medium
Curica implements a Deno-style permission-based execution mode. Using capability flags (e.g., `--allow-net`, `--allow-read`), the engine intercepts all OS-level calls at the C-boundary. Unapproved actions are instantly rejected with `SecurityError`s, allowing developers to execute untrusted scripts in strictly confined Secure Enclaves.
