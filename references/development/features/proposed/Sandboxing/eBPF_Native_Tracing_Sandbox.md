# eBPF Native Tracing Sandbox

**State**: Proposed
**Difficulty**: Extreme
Integrating an eBPF tracing hook that constantly monitors the Curica process from the kernel side. If the runtime begins exhibiting anomalous syscall patterns (e.g., executing `/bin/sh`), the kernel forcibly terminates it.
