# Module Creation Workflow for Curica Runtime

To maintain the architectural integrity of Curica, adding a new standard library module (like `zlib`, `net`, `dgram`, `fs`) requires a dual-layer approach: a low-level Native C binding, and a high-level JavaScript wrapper.

## Step 1: The Native C Module (`src/feature_module.c`)
1. Create `src/feature_module.c` and `src/feature_module.h`.
2. Implement native functions matching the signature `static Value js_feature_method(VM* vm, Value this_val, int arg_count, Value* args)`.
3. Create a module builder function `Value build_feature_module(VM* vm)` that returns a JavaScript object populated with the native methods using `create_native_function`.
4. Prefix the native module name with an underscore when exposing it (e.g., `_feature`).

## Step 2: The JavaScript Wrapper (`src/js/feature.js`)
1. Create a `.js` file in the `src/js/` directory.
2. Require the native module: `var _feature = require('_feature');`
3. Wrap the native methods with JavaScript classes, handle argument validation, extend `EventEmitter` if necessary, and expose standard Node.js compliant APIs.
4. Export the wrapper: `module.exports = { ... };`.

## Step 3: Registration in `src/builtins.c`
1. `#include "feature_module.h"` at the top of `src/builtins.c`.
2. In the `js_process_require` routing tree, add an intercept for the native module:
   ```c
   if (strcmp(path_str->data, "_feature") == 0) {
       return build_feature_module(vm);
   }
   ```
3. Add an intercept for the JavaScript wrapper module. The build system automatically converts files in `src/js/` into C arrays inside `build/scripts.h` (e.g., `src_js_feature_js`).
   ```c
   } else if (strcmp(path_str->data, "feature") == 0) {
       script_data = src_js_feature_js;
       script_len = src_js_feature_js_len;
   }
   ```

## Step 4: Garbage Collection Tracing
If your native module stores callbacks or promises, you MUST define a GC trace function.
1. Define `void feature_mark_gc_roots(GCTraceFn trace)` in `feature_module.c`.
2. Open `src/alloc.c` and `src/event_loop.c`.
3. Locate the other GC root markings and append:
   ```c
   extern void feature_mark_gc_roots(void*);
   feature_mark_gc_roots((void*)minor_copy_value); // or gc_mark_value_ptr / trace
   ```

## Step 5: Makefile & Compilation
1. Append `src/feature_module.c` to the `SRCS` list in the `Makefile`.
2. Verify with `make clean && make`. The script conversion step runs first automatically, building `scripts.h`, followed by C compilation.
