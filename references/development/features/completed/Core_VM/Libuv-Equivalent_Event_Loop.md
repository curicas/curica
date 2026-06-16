# Libuv-Equivalent Event Loop

**Category**: Core_VM
**Status**: Completed
**Difficulty**: High
Curica features a custom, phased event loop orchestrator that exactly mirrors Node.js's libuv execution phases. It manages `epoll` (Linux) or `kqueue` (macOS) polling for network sockets, processes `setTimeout`/`setInterval` timers, flushes I/O callbacks from the thread pool, and executes microtask queues (`Promise` resolutions) in a strict, deterministic order to guarantee ecosystem compatibility.
