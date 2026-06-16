# Advanced Multithreading with `SharedArrayBuffer`

**Category**: Advanced_Features
**Status**: Completed
**Difficulty**: High
To support high-performance computing, Curica allows multiple isolated VM worker contexts to concurrently read and write to the same underlying C-memory slabs. Implementing `SharedArrayBuffer` along with native lockless `Atomics` APIs utilizing C11 `<stdatomic.h>` unlocks true parallel processing capabilities, bypassing the overhead of standard message passing.
