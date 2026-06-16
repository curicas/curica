# Pluggable Garbage Collector Architectures

**State**: Proposed
**Difficulty**: Extreme
Refactoring the allocator to switch between generational GC and a concurrent Mark-Sweep algorithm depending on whether the application is latency-sensitive (web servers) or throughput-sensitive (batch processing).
