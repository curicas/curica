---
name: Module Creation Workflow
description: Step-by-step workflow for creating native kernel modules, binding C logic to the JS shell, and enforcing strict security capabilities.
---

# Kernel Module Creation Workflow for Curica

To maintain the zero-bloat architectural integrity of the Curica Microkernel, adding a new native capability requires a dual-layer approach: a low-level Native C binding, and a high-level JavaScript OS wrapper.

## Step 1: Security & VFS Validation First
Before creating a native module, ensure:
- It does not bypass the POSIX FHS Sandbox. Use `vfs_resolve_path` for all file I/O.
- It explicitly checks security boundaries (e.g., `if (!vm->allow_net) return vm_throw_error(...);`) before accessing host resources.

## Step 2: The Native C Module (`src/feature_module.c`)
1. Create `src/feature_module.c` and `src/feature_module.h`.
2. Implement native functions matching the signature `static Value js_feature_method(VM* vm, Value this_val, int arg_count, Value* args)`.
3. Create a module builder function `Value build_feature_module(VM* vm)` that returns a JavaScript object populated with the native methods via `create_native_function`.
4. Prefix the native module name with an underscore when exposing it (e.g., `_feature`).

## Step 3: The JavaScript Wrapper (`src/js/feature.js`)
1. Create a `.js` file in the `src/js/` directory. This acts as the standard library for user-space.
2. Require the native module: `var _feature = require('_feature');`
3. Wrap the native methods with JavaScript classes, handle argument validation, and expose standard POSIX/Node.js compliant APIs.

## Step 4: Registration in `src/builtins.c`
1. `#include "feature_module.h"` at the top of `src/builtins.c`.
2. In the `js_process_require` routing tree, add an intercept for the native module:
   ```c
   if (strcmp(path_str->data, "_feature") == 0) {
       return build_feature_module(vm);
   }
   ```
3. Add an intercept for the JS wrapper module. The build system converts `src/js/` into C arrays inside `build/scripts.h`.
   ```c
   } else if (strcmp(path_str->data, "feature") == 0) {
       script_data = src_js_feature_js;
       script_len = src_js_feature_js_len;
   }
   ```

## Step 5: Garbage Collection Tracing
If your native module stores callbacks or promises, you MUST define a GC trace function.
1. Define `void feature_mark_gc_roots(GCTraceFn trace)` in `feature_module.c`.
2. Register this function in `src/alloc.c` and `src/event_loop.c` to prevent use-after-free sandbox escapes.
