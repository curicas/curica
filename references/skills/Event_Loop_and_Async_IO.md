# Event Loop and Async IO in Curica Runtime

Curica manages a single-threaded Javascript event loop wrapped around an internal C polling mechanism, mimicking Node.js behavior. 

## 1. The Core Loop (`src/event_loop.c`)
- Uses standard POSIX `poll()` (or `epoll`) to multiplex non-blocking file descriptors.
- JavaScript execution runs between poll ticks.
- Before polling, Curica completely drains the microtask queue (Promises) via `vm_drain_microtasks`.
- Curica also supports `setTimeout` and `setInterval` execution inside the event loop tick.

## 2. Non-Blocking I/O
If your native module handles Sockets or Pipes:
1. Use `fcntl(fd, F_SETFL, O_NONBLOCK)` to make the file descriptor non-blocking.
2. Allocate an `IOHandle` struct (from `event_loop.h`).
3. Set the `cb` function pointer to handle readable/writable events.
4. Call `el_add_io(loop, handle, fd, POLLIN | POLLOUT)`.
5. When `poll()` triggers, your `cb` function will be invoked on the main thread, where you can safely call `vm_call_function` to resolve Promises or trigger JS callbacks.

## 3. The Thread Pool (`src/thread_pool.h`)
For CPU-bound or blocking I/O tasks that cannot use non-blocking `POLLIN` (such as disk `stat()`, large `zlib` compression, or `crypto` hashing):
1. **Never block the main thread!**
2. Use `tp_submit(WorkItem* item)`.
3. Set `item->work` to the function executed by the background thread. **NO VM INTERACTION IS ALLOWED HERE.** Do not allocate Curica Strings or call JS functions inside `item->work`.
4. Set `item->after` to the callback executed on the main thread when the work finishes. Here, you can safely create Curica Buffers/Strings and execute JS callbacks.
5. Set `item->gc_mark` to trace any callbacks stored in `item->data`. The thread pool natively hooks into the garbage collector.

## 4. Timers and `nextTick`
- The `process.nextTick` array is executed before I/O events are polled.
- `setTimeout` and `setInterval` are natively hooked into the `event_loop` as `TimerHandle`s.

By maintaining strict boundaries between the single-threaded VM and POSIX threads/poll waitstates, Curica prevents race conditions and ensures flawless event-driven performance.
