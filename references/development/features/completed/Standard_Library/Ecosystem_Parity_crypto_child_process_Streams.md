# Ecosystem Parity (`crypto`, `child_process`, Streams)

**Category**: Standard_Library
**Status**: Completed
**Difficulty**: Medium
The standard library has been extended to match Node's capabilities. The `crypto` module links against native OpenSSL/BoringSSL for highly optimized hashing and encryption. The `child_process` module safely forks processes and pipes standard streams using POSIX `fork`/`execvp`. Finally, a robust, native implementation of the Node Streams API handles backpressure and pipelining for massive data throughput.
