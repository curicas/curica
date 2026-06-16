# Bytecode Execution Optimization

**Category**: Core_VM
**Status**: Completed
**Difficulty**: Extreme
To push single-thread performance, the VM features advanced compiler optimizations. It utilizes Threaded Code dispatch via GCC's computed `goto` extension, bypassing standard `switch` statement overhead. Furthermore, it implements dynamic Inline Caching; when a property is accessed on an object, the VM caches the memory offset directly into the bytecode instruction stream, massively increasing instruction throughput on hot loops.
