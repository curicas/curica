# Curica Compiler Pipeline

The Compiler (`src/compiler.c` / `src/compiler.h`) transforms raw JavaScript source code into optimized binary bytecode (`CBC`) format. Curica strictly separates parsing from execution, ensuring that identical code paths are not repeatedly tokenized across process boundaries.

## 1. Tokenization (Lexing)
The tokenizer (`tokenize()`) performs a single, linear pass over the UTF-8 source string.
- It identifies keywords, identifiers, string literals, and numbers.
- **Escape Sequences**: Standard C-style escapes (`\n`, `\t`, `\r`, `\"`) inside strings are intercepted and expanded natively during the lexing phase before AST generation.

## 2. Pratt Parsing (AST Generation)
Curica utilizes a top-down operator precedence (Pratt) parser to generate an Abstract Syntax Tree (AST).
- Each token is assigned a `ParseRule` consisting of a `prefix` parsing function, an `infix` parsing function, and an execution `precedence` level.
- This recursive-descent strategy elegantly handles complex JavaScript expressions, operator precedence (e.g., `1 + 2 * 3`), and associativity without requiring heavy parser-generator dependencies like Bison or Yacc.

## 3. Bytecode Emission (Code Generation)
The AST is immediately walked by the code generator (`emit_bytecode()`).
- The compiler maps variables to flat register indices to populate the VM's sliding register window, preventing the need for costly dynamic dictionary lookups for local variables at runtime.
- **Constant Pool**: String literals, numbers, and dynamically defined JS functions are hoisted into a discrete `const_pool`.
- **Bytecode Layout**: The compiled bytecode (`CBC` buffer) and the constant pool are fused into a `CompiledProgram` structure. 
- Because bytecode offsets use pure 32-bit integer indexes rather than raw memory pointers, the entire `CompiledProgram` can be serialized, persisted to disk (`.jbc`), and memory-mapped later across different architectures natively.
