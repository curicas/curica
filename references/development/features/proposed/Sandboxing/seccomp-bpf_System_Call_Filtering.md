# seccomp-bpf System Call Filtering

**State**: Proposed
**Difficulty**: Extreme
A Linux-specific extremely strict sandbox. Curica injects custom BPF filters directly into the OS kernel, dropping any unauthorized `syscall` execution from the thread, providing perfect defense-in-depth.
