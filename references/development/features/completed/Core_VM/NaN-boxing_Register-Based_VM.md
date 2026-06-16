# NaN-boxing Register-Based VM

**Category**: Core_VM
**Status**: Completed
**Difficulty**: Extreme
The core engine is built around a custom Register-Based Virtual Machine. Instead of a traditional stack, it utilizes an infinite sliding register window. All JavaScript types (Objects, Strings, Numbers) are encoded directly into 64-bit IEEE 754 floating-point numbers via NaN-boxing. By carving out the unused NaN space, Curica achieves zero-allocation type checking and perfectly isolates the JavaScript execution context from the underlying C-stack, eliminating entire classes of memory corruption vulnerabilities.
