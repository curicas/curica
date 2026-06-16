# Web Platform API Interoperability

**Category**: Interoperability
**Status**: Completed
**Difficulty**: High
Moving beyond Node APIs, Curica implements modern standard Web APIs natively in C. This includes the `fetch` API for asynchronous HTTP requests, a `WebSocket` client/server natively handling RFC6455 masking and framing, and Web Worker multithreading that spins up entirely isolated Curica Virtual Machines in background POSIX threads, bridging the gap between browser standards and server environments.
