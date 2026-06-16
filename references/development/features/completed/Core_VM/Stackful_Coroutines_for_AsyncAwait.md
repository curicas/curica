# Stackful Coroutines for Async/Await

**Category**: Core_VM
**Status**: Completed
**Difficulty**: Extreme
Instead of compiling `async`/`await` into complex state machines (like V8 or Babel), Curica implements native stackful coroutines. By leveraging POSIX `ucontext_t` (`makecontext`/`swapcontext`), the VM can physically pause the C-execution stack of a running function, yield control back to the main event loop, and perfectly resume the execution later without blocking. This results in incredibly linear and fast asynchronous execution.
