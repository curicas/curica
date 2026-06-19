---
name: Event Loop and Async I/O
description: Guidelines for interfacing native C modules with the custom event loop, non-blocking polling, and POSIX thread pools within the Curica OS Kernel.
---

# Event Loop and Async IO in Curica OS Kernel

Curica acts as an OS kernel managing a single-threaded execution loop wrapped around an internal C polling mechanism, mimicking high-performance asynchronous execution.

## 1. The Core Loop (`src/event_loop.c`)
- Uses standard POSIX `poll()` (or `epoll`) to multiplex non-blocking file descriptors.
- JavaScript shell processes, WASM child process pipes, and IPC mechanisms run between poll ticks.
- Before polling, the kernel drains microtask queues natively.

## 2. Non-Blocking I/O & Networking
If your native C module handles Sockets or Pipes (such as reading from a spawned WASM binary in `/bin`):
1. **Capability Checks First**: Validate `vm->allow_net` or `vm->allow_run` before granting the FD.
2. Use `fcntl(fd, F_SETFL, O_NONBLOCK)`.
3. Allocate an `IOHandle` struct (from `event_loop.h`) and bind a `cb` callback.
4. Call `el_add_io(loop, handle, fd, POLLIN | POLLOUT)`.
5. The `cb` callback will be invoked on the main thread safely.

## 3. POSIX Thread Pool (`src/thread_pool.h`)
For CPU-bound tasks that cannot use non-blocking `POLLIN` (such as cryptographic hashing or WASM compilation):
1. **Never block the main thread!**
2. Use `tp_submit(WorkItem* item)`.
3. `item->work` executes in the background thread. **NO VM INTERACTION IS ALLOWED HERE.** Do not allocate Curica Strings or call JS functions inside `item->work`.
4. `item->after` executes on the main thread when work finishes. Safe for JS interaction.
5. Set `item->gc_mark` to trace callbacks for the Garbage Collector.

## 4. POSIX-Mapped Workers (Zero-Copy)
When spinning up full JS or WASM worker threads:
- They map directly to POSIX pthreads via the C runtime.
- Use `SharedArrayBuffer` for communication. It shares the exact same linear C memory pointer across threads, enabling zero-copy gigabyte parallel processing. Do NOT use structured cloning for large data.
