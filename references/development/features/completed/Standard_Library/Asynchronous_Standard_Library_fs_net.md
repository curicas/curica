# Asynchronous Standard Library (`fs` & `net`)

**Category**: Standard_Library
**Status**: Completed
**Difficulty**: High
Leveraging the event loop, Curica provides full non-blocking support for file system operations (`fs.promises`) and TCP/UDP socket networking. Network sockets are configured with `O_NONBLOCK` and watched by the OS kernel, instantly waking up the Curica VM when bytes are ready to be read or written, allowing the engine to function as a highly concurrent web server capable of handling thousands of simultaneous connections.
