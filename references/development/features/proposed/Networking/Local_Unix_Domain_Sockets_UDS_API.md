# Local Unix Domain Sockets (UDS) API

**State**: Proposed
**Difficulty**: Medium
Exposing `net.createConnection({ path: '/tmp/sock' })` for blazing-fast local IPC between Curica processes. Reduces TCP/IP loopback overhead for microservices running on the exact same physical machine.
