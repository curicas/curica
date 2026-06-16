# Synchronous File System & Thread Pool

**Category**: Standard_Library
**Status**: Completed
**Difficulty**: Medium
The `fs` module implements all POSIX synchronous file system APIs (`fs.readFileSync`, `fs.writeFileSync`). Because these operations block the thread, Curica introduces a native POSIX `pthread` thread-pool dispatcher. Blocking I/O tasks are packaged into structs and offloaded to background worker threads, freeing the main execution loop to continue processing JavaScript instructions.
