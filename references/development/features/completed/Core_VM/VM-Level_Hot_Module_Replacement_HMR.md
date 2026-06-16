# VM-Level Hot Module Replacement (HMR)

**Category**: Core_VM
**Status**: Completed
**Difficulty**: High
Instead of complex JS bundlers injecting wrapper code, Curica natively supports hot-swapping module code blocks directly in-memory. By monitoring the file system, the VM dynamically recompiles changed files and seamlessly patches the bytecode offset pointers for live functions without losing the global lexical environment, enabling instantaneous reload-free development.
