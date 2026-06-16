# Curica Virtual Machine

The Virtual Machine (`src/vm.c` / `src/vm.h`) is the execution heart of Curica. It consumes compiled CURI bytecode and dispatches instructions using a high-speed mechanism optimized for embedded and server workloads.

## Execution Flow
1. **Instruction Decoding**: Instructions are 32-bit aligned. The opcode is extracted via bitmasking (`*pc & 0xFF`), and registers are resolved using `INST_A`, `INST_B`, and `INST_C` macros.
2. **Computed Goto (Threaded Code)**: A static `dispatch_table` array holds pointers to label addresses (e.g., `&&do_op_add`, `&&do_op_call`). The `DISPATCH()` macro invokes a direct branch prediction jump to the next instruction, heavily optimizing the CPU pipeline compared to a traditional large `switch` statement.

## The CallFrame Stack & Closures
Curica explicitly does not use the native C-stack for executing JavaScript functions. This fundamental design choice completely prevents C-stack overflows.
- Instead, the VM manages a heap-allocated array of `CallFrame` structures.
- **Pushing Frames**: When `OP_CALL` is invoked, `vm_call_function` dynamically allocates a new `CallFrame`.
- **Environment Context**: Each frame tracks its own `ip` (Instruction Pointer relative to the module bytecode), `reg_base` (Base offset into the VM's flat sliding register window), and `env` (Lexical Environment). 
- **Closures**: When `OP_NEW_FUNCTION` is executed, the VM captures the current active lexical environment. If variables escape their scope, they are stored dynamically in the `env` array attached to the `JSFunction` object, ensuring robust high-order function support without traditional GC rooting complexities.
- **Module Transitions**: Through `vm_switch_program`, the VM can seamlessly transition its constant-pool execution context across independent CommonJS modules without breaking pointer validity.

## Asynchronous Coroutines (`ucontext_t`)
To support native `async`/`await` functions without blocking the main event loop, Curica relies on POSIX `ucontext_t`.
- When an `async` function is invoked, the VM dynamically allocates a 256KB isolated C-stack (`VMCoroutine`).
- When `OP_AWAIT` is encountered, the VM captures the async state, links the coroutine to a `JSPromise`, and uses `swapcontext` to yield execution back to the primary VM loop (`vm_coro_yield`), suspending the function frame natively.
- Once the awaited promise settles (either from an async I/O worker or standard microtask), the coroutine is queued back onto the event loop and natively resumed (`vm_coro_resume_now`).

## Instantaneous VM Snapshotting
To eliminate cold-start times completely, Curica implements VM Snapshotting. 
- The contiguous memory Arena enforces allocation at a fixed virtual memory address (`MAP_FIXED` via `mmap`).
- By maintaining static pointers across execution contexts, the entire state of the Virtual Machine—including compiled bytecode (`CompiledProgram`), the global object, and active lexical environments—is serialized directly to disk in milliseconds via `arena_snapshot_save` and `vm_snapshot_save`.
- The application can be woken up instantaneously by mapping the file back into memory without invoking parsing or GC sweeps.

## VM-Level Hot Module Replacement (HMR)
Curica natively supports hot-swapping module code blocks in-memory. 
- Utilizing `Curica.reloadModule(filepath)`, the VM bypasses standard build wrappers, directly re-invoking `compile_source()` on the modified file. 
- The newly compiled `CompiledProgram` is mapped to the VM's active execution thread natively. Because the global `JSEnvironment` properties are retained via separate heap allocations, closures and active bindings remain fully intact during the transparent bytecode swap.
