# AOT Bytecode Compilation & ES2025 Primitives

**Category**: Core_VM
**Status**: Completed
**Difficulty**: High
The runtime implements a robust Ahead-Of-Time (AOT) compiler that tokenizes, parses, and converts JS source code into Curica Bytecode (`CBC`) before execution begins. It fully supports modern ES2025 features including Iterator Helpers, Set composition methods, and Explicit Resource Management (`using` keywords), ensuring the engine is fully compliant with the latest ECMAScript specification without requiring Babel transpilation.
