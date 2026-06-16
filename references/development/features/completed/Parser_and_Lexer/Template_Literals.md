# Template Literals

**Category**: Parser_and_Lexer
**Status**: Completed

## Description

The lexer in `scan_token()` has no backtick branch. Need to scan backticks, handle `${` interpolation, emit `TOK_TEMPLATE_LITERAL`, and lower to `OP_CONCAT` in bytecode.
