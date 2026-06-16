# Curica Event Loop & Asynchronous I/O

Curica integrates a phase-based event loop (`src/event_loop.c`) modeled directly after libuv, bridging the gap between blocking POSIX operations and non-blocking JavaScript execution.

## Phase Execution Order
The `el_run()` function dictates the strict order of operations for asynchronous callbacks, guaranteeing parity with the Node.js event loop schedule:
1. **Timers Phase**: Executes callbacks scheduled via `setTimeout()` and `setInterval()` whose expiration thresholds have elapsed.
2. **Pending Callbacks Phase**: (Reserved for deferred I/O callbacks).
3. **Idle / Prepare Phase**: (Reserved for internal handle preparation).
4. **Poll Phase**: The event loop blocks here using POSIX `poll()`, waiting for network sockets (e.g., from Native `fetch` or `WebSocket` connections), file system descriptors, or internal wakeup pipes to trigger. The loop blocks indefinitely unless a Timer is pending.
5. **Check Phase**: Executes callbacks scheduled by `setImmediate()`.
6. **Close Callbacks Phase**: Executes teardown handlers for closed resources.

## Web APIs and Networking
Curica natively integrates network I/O (`fetch`, `WebSocket`) directly into the libuv-style `poll()` phase, bypassing expensive polyfills or background threading when unnecessary:
- **Non-blocking Sockets**: Native network modules (`http_module.c`, `websocket_module.c`) register their POSIX file descriptors directly with the core event loop (`el_watch_fd`).
- **Immediate Dispatch**: When a socket receives data, the `poll()` phase awakens and synchronously dispatches a C callback. For `fetch`, this triggers stream parsing or Promise settlement; for WebSockets, this decodes RFC6455 frames and enqueues JS MessageEvent handlers on the microtask queue.

## Microtask & NextTick Queues
At the conclusion of every C-to-JS boundary, or when returning from an asynchronous I/O completion, the VM immediately "drains" two distinct task queues:
- **`process.nextTick` Queue**: Executes natively before any other scheduled event loop phase.
- **Microtask Queue**: Executes Promise `.then()` continuations and `async/await` yields immediately following the `nextTick` queue but before yielding back to the overarching libuv phase.

## Thread Pool Offloading (`thread_pool.c`)
To prevent heavy CPU tasks or blocking filesystem I/O from pausing the main VM event loop, Curica maintains a localized POSIX Thread Pool (`src/thread_pool.c`).
- `tp_submit()` dispatches C-level `WorkItem` blocks to a fleet of concurrent background threads.
- Upon completion, the worker threads trigger a non-blocking `wakeup_pipe` injected directly into the main event loop's Poll Phase.
- The event loop reads this pipe during its Poll/Drain phase and subsequently schedules the `after_cb` callback back onto the main JavaScript execution thread.
- **Coroutine Integration**: When an asynchronous operation resolves (e.g., via `fs.readFile` or N-API promises), the thread pool triggers a Promise resolution. This resolution invokes the microtask queue, which seamlessly resumes suspended Stackful Coroutines (`VMCoroutine`), restoring the `async/await` execution context dynamically on the main thread.
- **N-API Integration**: The `napi_create_async_work` API routes directly into this Thread Pool, ensuring native C++ addons safely execute on background threads and only interact with the V8/Curica engine when safely synchronized back to the main thread via `tp_drain_completions`.
