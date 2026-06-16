# Direct POSIX Syscall Bridge

**Category**: Interoperability
**Status**: Completed
**Difficulty**: Medium
For extreme low-level system control, the native `os` module exposes a direct bridge to POSIX syscalls (`read`, `write`, `ioctl`, `mmap`). Utilizing Cosmopolitan libc's variadic dispatch, this allows JavaScript developers to build system-level hardware drivers and bypass all abstract runtime networking layers when maximum throughput is necessary.
