# Instantaneous VM Snapshotting

**Category**: Advanced_Features
**Status**: Completed
**Difficulty**: Extreme
To achieve zero cold-start times, Curica serializes its entire memory state. By forcing the contiguous memory Arena to allocate at a fixed virtual memory address (`MAP_FIXED`), the VM's active lexical environments, compiled bytecode, and global objects can be dumped linearly to disk. Subsequent executions simply memory-map this snapshot file, waking the application up instantaneously.
