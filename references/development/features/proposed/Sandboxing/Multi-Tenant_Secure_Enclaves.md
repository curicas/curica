# Multi-Tenant Secure Enclaves

**State**: Proposed
**Difficulty**: Extreme
Hard-partitioning the Garbage Collection Arena at the OS level using `mprotect`. If an untrusted script attempts a malicious memory access violation, it instantly segfaults the isolated worker thread, protecting the host.
