# CommonJS Module Cache & Resolution

**Category**: Standard_Library
**Status**: Completed
**Difficulty**: Medium
Curica implements the complex Node.js module resolution algorithm natively. It recursively crawls up directory trees parsing `package.json` files to determine main entry points and exact version constraints. It utilizes POSIX memory-mapped I/O (`mmap`) to read files instantaneously and caches the compiled `CBC` bytecode in a secure, hashed memory map, ensuring that massive dependency trees (`node_modules`) are only parsed once.
