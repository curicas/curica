# Curica ES2025 Compliance Status

Curica aims to provide robust compliance with modern JavaScript specifications, explicitly targeting the ECMAScript 2025 (ES16) standard.

## 1. Promises and Async/Await
- **`Promise`**: Full support for native Promises, including `.then()`, `.catch()`, `.finally()`.
- **Promise Combinators**: `Promise.all()`, `Promise.race()`, `Promise.allSettled()`, `Promise.any()`, `Promise.withResolvers()`, and **`Promise.try()`** (ES2025).
- **`async`/`await`**: Implemented fundamentally using stackful `ucontext_t` coroutines. Await completely suspends the execution frame natively without relying on AST transpilation or generators.

## 2. Iterators and Generators
- **Iterator Helpers**: Native implementation of `Iterator.prototype` methods (e.g., `.map()`, `.filter()`, `.take()`, `.drop()`, `.flatMap()`, `.reduce()`, `.toArray()`, `.forEach()`, `.some()`, `.every()`, `.find()`).
- **Generators**: Supported via `function*` and `yield`.

## 3. Advanced Data Structures
- **`Set` & `Map`**: High-performance linear probing hash tables.
- **Set Methods (ES2025)**: Supported mathematically: `.intersection()`, `.union()`, `.difference()`, `.symmetricDifference()`, `.isSubsetOf()`, `.isSupersetOf()`, `.isDisjointFrom()`.

## 4. RegExp Enhancements
- **Regex `v` flag**: (Pending) Curica relies on SLRE (Super Light Regular Expression) parser, lacking native support for Unicode property escapes (`\p{...}`) currently.

## 5. Primitive Additions
- **`Symbol`**: Native support for globally unique Symbol tokens and the global registry (`Symbol.for()`). Well-known symbols like `Symbol.iterator`, `Symbol.toStringTag`, and `Symbol.hasInstance` are intercepted securely across the VM.
- **`Float16Array` (ES2025)**: Full support for the IEEE-754 16-bit floating point array type, complete with fast-path memory property accesses.

## 6. ECMAScript Modules (ESM)
- **Top-Level Await**: Natively transpiled and executed within the runtime using Coroutine abstractions, allowing `await` at the root of `.mjs` modules without wrapping functions.
- **`import` / `export` Syntax**: Fully supported syntax structures with interoperability caching bindings that seamlessly bridge Node.js CJS `require()` and ES6 `import` algorithms.

## 7. Parser and Syntax Features
- **Arrow Functions**: Support for concise and block bodies, lexical `this` binding, and `async` arrows.
- **Template Literals**: Native multi-mode lexer handling for backticks, string interpolations (`${expr}`), and proper escape sequence processing.
- **Classes**: Fully parsed `class`, `extends`, `constructor`, `super()`, `static`, and `get`/`set` methods.
- **Destructuring**: Array (`[a, b] = arr`) and Object (`{x, y} = obj`) pattern matching in declarations and assignments.
- **Operators**: Complete support for Optional Chaining (`?.`), Nullish Coalescing (`??`), Exponentiation (`**`), and all bitwise and compound assignment operators.
- **Loops & Control Flow**: Native support for `for...of`, `for...in`, and `switch`/`case` block evaluation.
- **Regex Literals**: Context-sensitive lexing for inline `/pattern/flags` initialization.
