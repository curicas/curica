# Lock-Free Atomic Memory Allocator

**State**: Proposed
**Difficulty**: Extreme
Rewriting the underlying memory arena allocator to be completely lock-free, ensuring that multiple VMs concurrently modifying data structures across SharedArrayBuffers require no POSIX mutexes.
