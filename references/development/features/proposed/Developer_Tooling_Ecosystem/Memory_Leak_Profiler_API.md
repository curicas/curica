# Memory Leak Profiler API

**State**: Proposed
**Difficulty**: High
Exposing `Curica.memory.getHeapSnapshot()` natively. The VM pauses execution, traverses the GC Arena, and formats the memory graph into a `.heapsnapshot` JSON file compatible with Chrome Memory Profiler.
