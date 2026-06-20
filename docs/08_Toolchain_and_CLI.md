# Curica Toolchain & CLI

Curica is not just a runtime; it ships with an integrated, zero-dependency toolchain to provide a cohesive developer experience. Because everything is built into the single APE binary, developers don't need external node modules or formatters.

## Built-in Formatter
Curica includes a highly optimized JavaScript code formatter (`src/formatter.c`). 
- **Usage**: `./curica fmt [file.js]`
- The formatter utilizes a fast, naive lexical pass that traverses the JavaScript source string, keeping track of block scope (braces) and string literals, to enforce consistent indentation and line breaks, bypassing the need for Prettier or external Node.js tooling.

## Built-in Test Runner
The test runner natively handles execution and assertion of test scripts.
- **Usage**: `./curica test [dir/]`
- It securely isolates each test script into its own hermetic VM instance, ensuring that global mutations or prototype pollutions in one test do not leak into another. Memory footprints are tracked per test to prevent regression leaks.

## TypeScript Stripper
Curica natively executes JavaScript, but it features an incredibly fast Type Stripper (`src/ts_stripper.c`) to allow execution of TypeScript files without `tsc` compilation.
- **Mechanics**: The stripper runs ahead of the AOT compiler, performing a lightweight, state-machine-based scan to erase `interface` and `type` declarations. By replacing erased characters with space characters, it ensures that source maps and error line numbers remain completely accurate.
- **Execution**: The resulting pure-JavaScript string is fed directly into the parser. This occurs entirely in memory with zero disk I/O, providing instantaneous execution of `.ts` and `.mts` files.

## Package Manager & Source Fallback (Phase 3.3)
Curica ensures reproducible builds and hermetic package management via declarative `curica.env.json` configuration.
- **Source Compilation Fallback**: When `package_manager.js` fails to locate a WebAssembly binary (either locally or via the remote registry), it initiates Phase 3.3. It retrieves the original C source and invokes the host compiler natively via `child_process.execSync`. This powerful mechanism—strictly gated by the `allow_run` capability—guarantees that missing dependencies are deterministically rebuilt and dynamically injected back into the active VFS environment.

## IPC Attachments (Phase 1.5)
Developers can attach foreign processes or external sandboxes directly to the Curica runtime at boot using the `--attach` flag.
- **Mechanics**: By mapping host Unix Sockets to virtual endpoints, Curica opens secure, high-performance inter-process communication channels. These sockets are presented to the JS environment exclusively through `process.ipcSocket`, enabling tightly controlled bridging without exposing full host-level networking.
