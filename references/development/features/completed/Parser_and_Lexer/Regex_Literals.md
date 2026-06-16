# Regex Literals

**Category**: Parser_and_Lexer
**Status**: Completed

## Description

Add context-sensitive disambiguation for `/`. Scan forward until unescaped `/`, consume optional flags. Emit `TOK_REGEX`. Lower to `RegExp` object construction.
