# Feature: Virtual Networking (Network Namespaces)

## Overview
To prevent WASM and JS processes from establishing unchecked, arbitrary TCP/UDP connections on the host machine, the Curica Runtime will act as a secure proxy router.

## Requirements
1. **Syscall Interception:** Intercept all BSD socket API calls (`socket`, `connect`, `bind`) originating from WASM and JS.
2. **Capability Routing:** Validate outbound connections against the `allow_net` capability matrix.
3. **Internal Loopback Mocking:** Allow the environment to define virtual network routes. For example, requests to `localhost:8080` can be intercepted by the runtime and routed directly to a different WASM process's standard input via IPC, completely bypassing the host's networking stack.
4. **Lightweight Design:** Avoid building a full TCP/IP stack in C. Delegate to the host's stack only *after* validation.

## Implementation Details
- Refactor `src/net_module.c` to wrap the host socket creation logic inside the capability boundary checks.
- Implement a lightweight virtual routing table for inter-process socket communication.
