# Distributed Actor Model APIs

**State**: Proposed
**Difficulty**: High
Erlang-style process spawning where invoking `spawn(function)` may instantiate a VM worker on a completely different physical machine in the cluster, hiding the network layer for resilient compute.
