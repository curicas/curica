# Native Server-Sent Events (SSE) Engine

**State**: Proposed
**Difficulty**: Low
Built-in highly optimized SSE streams mapped directly to epoll file descriptors. Eliminates the massive memory overhead of standard Node implementations when holding thousands of concurrent SSE connections open.
