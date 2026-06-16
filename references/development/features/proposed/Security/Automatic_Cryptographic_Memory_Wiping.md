# Automatic Cryptographic Memory Wiping

**State**: Proposed
**Difficulty**: Medium
A massive security enhancement for the Garbage Collector. Memory regions in the GC nursery that temporarily held sensitive structures (like private keys) are forcefully zeroed-out (`explicit_bzero`) upon deallocation.
