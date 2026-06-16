# Curica Node.js Compatibility Layer

Curica is designed as a drop-in replacement execution engine for standard server-side JavaScript, featuring deep architectural alignment with Node.js primitives without retaining any dependencies on the massive V8 binary blob.

## CommonJS Module Resolution
The `require()` implementation (`src/builtins.c`) perfectly matches the algorithmic lookup heuristics utilized by Node.js.
- **`node_modules` Traversal**: Upon requiring an external package dependency, Curica recursively ascends directory structures looking for `node_modules` folders, matching the package resolution spec.
- **Native JSON parsing**: If the target package directory contains a `package.json`, the native C JSON parser isolates the `"main"` field to identify the correct entry file.
- **Index resolution**: Curica implicitly falls back to `index.js` or directly searches for `.js` extensions dynamically if the exact file is not specified.
- Loaded modules are correctly cached in the VM's `module_cache` to ensure singleton instantiation per process.

## The Global Process Object
Curica populates the global namespace with the standard `process` variable context expected by modern codebases.
- Supported fields natively injected via C: `process.version`, `process.platform`, `process.arch`, `process.argv`, `process.env`.
- Process-level queuing mechanisms: `process.nextTick()`.
- Standard POSIX outputs: `process.stdout.write()`, `process.stderr.write()`.

## Node-API (N-API) Integration
A major feature of Curica is its native support for third-party `node-gyp` compiled C/C++ addons without demanding a rebuild against Curica-specific headers.
- **N-API v1 Interface**: Curica exports the exact memory signatures and vtable symbols (`napi_register_module_v1`) mandated by the Node-API ABI spec. 
- **Thread-Safe Work**: Implemented APIs like `napi_create_async_work` seamlessly map directly onto Curica's internal Thread Pool (`thread_pool.c`).
- **Object Wrapping**: Custom C++ classes exposed by third-party packages dynamically hook into Curica's Mark-and-Sweep garbage collector via `napi_wrap`, automatically executing destructor finalizers without leaking memory.

## Standard Library Features
Curica aims to provide parity with the core structural APIs used heavily across the NPM ecosystem. Supported native modules include:
- **`fs`**: Offers fully asynchronous POSIX file system APIs utilizing `Promises` and background thread pool offloading (`fs_module.c`).
- **`child_process`**: Native implementation of `spawn()` via `fork/execvp` allowing non-blocking STDIO piping through stream wrappers (`child_process_module.c`).
- **`crypto`**: Embedded, standalone SHA-256 and CSPRNG integration via `getrandom` (`crypto_module.c`).
- **`net`**: Event-loop integrated TCP socket abstractions (`net_module.c`).
- **`events` / `stream`**: Complete JS implementations bridging EventEmitter architecture into native readable/writable streams.
