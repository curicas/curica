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
