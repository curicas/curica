# Curica OS Kernel Agent Skills Index

This directory contains skills and guidelines for developing the Curica Javascript Runtime (Microkernel OS). As an AI Agent working on Curica, you must strictly follow these architectural constraints.

* **[C Development Guidelines](c-development-guidelines/SKILL.md)**: Rules for writing Cosmopolitan Libc compliant, zero-bloat C code.
* **[Architecture & Feature Development](feature-development-guide/SKILL.md)**: High-level overview, VFS mapping, FHS compliance, and how features are built.
* **[Event Loop & Async I/O](event-loop-and-async-io/SKILL.md)**: Integrating native C modules with the single-threaded custom poll loop and thread pools.
* **[Memory Management Rules](memory-management-rules/SKILL.md)**: Rules for NaN-boxing, arena allocators, host-proxied `mmap`, and avoiding GC-related use-after-free crashes.
* **[Module Creation Workflow](module-creation-workflow/SKILL.md)**: The lifecycle of binding low-level C logic to high-level JavaScript OS wrappers.
* **[WebAssembly Integration](wasm-integration/SKILL.md)**: Guidelines for running WASM within Curica, WASI boundaries, and `/bin` WASM executable orchestration.
* **[Capability-Based Security](capability-based-security/SKILL.md)**: Enforcing the `allow_net`, `allow_read`, and execution permissions for a hermetic runtime.
