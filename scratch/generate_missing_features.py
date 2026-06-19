import os

base_dir = "references/development/features/approved"

features = {
    "ECMAScript_Array_Methods": {
        "Array_Iteration_Methods": ("Hard", "Implement forEach, map, reduce, every, some, filter to support standard data manipulation."),
        "Array_Search_Methods": ("Medium", "Implement indexOf, findIndex, includes for efficient searching within arrays."),
        "Array_Manipulation_Methods": ("Medium", "Implement concat, fill, pop, reverse, sort, splice to mutate or combine arrays natively."),
        "Array_Utility_Methods": ("Medium", "Implement at, flat, flatMap, from, isArray, of, toString for array creation and flattening.")
    },
    "ECMAScript_String_Methods": {
        "String_Search_Methods": ("Easy", "Implement endsWith, includes, lastIndexOf, startsWith for native string comparison and searching."),
        "String_Formatting_Methods": ("Easy", "Implement padEnd, padStart, trim, trimEnd, trimStart, toLowerCase, toUpperCase for text formatting."),
        "String_Extraction_Methods": ("Medium", "Implement at, charAt, charCodeAt, replace, replaceAll, concat for character extraction and manipulation.")
    },
    "ECMAScript_Math_Object": {
        "Math_Trigonometric_Functions": ("Medium", "Bind acos, asin, atan, atan2, cos, sin, tan to native <math.h> libraries for performance."),
        "Math_Rounding_And_Utility": ("Easy", "Bind abs, cbrt, ceil, clz32, exp, f16round, floor, hypot, imul, log, log10, log2, max, min, pow, random, round, sign, sqrt, trunc.")
    },
    "ECMAScript_Object_And_Number": {
        "Object_Utilities": ("Hard", "Implement assign, create, entries, freeze, fromEntries, values natively to manage object properties efficiently."),
        "Number_And_Global_Utilities": ("Easy", "Implement Number.isFinite, isInteger, isNaN, isSafeInteger, and global parseFloat, isFinite, isNaN."),
        "Set_Methods": ("Medium", "Implement Set.prototype.forEach, isDisjointFrom, isSubsetOf, isSupersetOf, symmetricDifference, values.")
    },
    "ECMAScript_RegExp_And_Errors": {
        "RegExp_Methods": ("Hard", "Implement exec, test, and the standard RegExp object constructor bridging to a native C regex engine."),
        "Standard_Errors": ("Easy", "Standardize throw structures and constructors for RangeError, ReferenceError, SyntaxError, TypeError, and SuppressedError.")
    },
    "Foreign_Function_Interface": {
        "Dynamic_libffi_Bridging": ("Extreme", "Implement dynamic libffi bridging to support arbitrary argument counts, nested C-structs, and callback pointers across the C/JS boundary.")
    },
    "WebAssembly_And_WASI": {
        "WASM_Export_Parsing": ("Hard", "Parse WASM exported functions fully into callable JS wrappers rather than relying on stubs."),
        "SIMD_128_bit_Instructions": ("Extreme", "Complete 128-bit SIMD instruction mappings for high-performance WebAssembly math operations."),
        "Secure_WASI_Configuration": ("Medium", "Implement secure WASI capability configuration storage to manage file and network access strictly.")
    },
    "Concurrency": {
        "Atomics_Synchronization": ("Extreme", "Implement cross-thread memory allocation and synchronization primitives (Mutex/Futex) for Atomics compliance."),
        "Worker_Threads_MessagePort": ("Hard", "Implement standard MessagePort and MessageChannel routing for thread-to-thread communication.")
    },
    "Standard_Library": {
        "SQLite_SystemError_Throwing": ("Easy", "Standardize error boundaries by throwing actual SystemError objects in SQLite bindings rather than returning VAL_UNDEFINED."),
        "RFC_3986_URL_Encoding": ("Medium", "Upgrade encodeURI/decodeURI from naive string replacements to complete RFC 3986 encoding algorithms.")
    },
    "Language_Syntax": {
        "Asynchronous_Iteration": ("Extreme", "Implement the AST parsing and bytecode emission for the 'for await (... of ...)' loop structure.")
    }
}

for cat, items in features.items():
    cat_dir = os.path.join(base_dir, cat)
    os.makedirs(cat_dir, exist_ok=True)
    for name, (diff, desc) in items.items():
        file_path = os.path.join(cat_dir, f"{name}.md")
        content = f"""# {name.replace('_', ' ')}

**State**: Approved
**Difficulty**: {diff}

{desc}
"""
        with open(file_path, "w") as f:
            f.write(content)
        print(f"Created {file_path}")

