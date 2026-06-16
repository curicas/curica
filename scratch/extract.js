import fs from 'fs';
import path from 'path';

const features = [
    // Lexer Gaps
    { title: 'Template Literals', cat: 'Parser_and_Lexer', desc: 'The lexer in `scan_token()` has no backtick branch. Need to scan backticks, handle `${` interpolation, emit `TOK_TEMPLATE_LITERAL`, and lower to `OP_CONCAT` in bytecode.' },
    { title: 'Regex Literals', cat: 'Parser_and_Lexer', desc: 'Add context-sensitive disambiguation for `/`. Scan forward until unescaped `/`, consume optional flags. Emit `TOK_REGEX`. Lower to `RegExp` object construction.' },
    { title: 'Optional Chaining and Nullish Coalescing', cat: 'Parser_and_Lexer', desc: 'Add `TOK_QUESTION`, `TOK_OPTIONAL_CHAIN` (`?.`), `TOK_NULLISH` (`??`). Wire `OP_JUMP_IF_NULLISH` from parser for conditional access and coalescing.' },
    { title: 'Spread and Rest', cat: 'Parser_and_Lexer', desc: 'Add `TOK_SPREAD` (`...`). Parse rest parameters, spread in calls (`OP_SPREAD_CALL`), array literals, and object literals.' },
    
    // Parser Gaps
    { title: 'Arrow Functions', cat: 'Parser_and_Lexer', desc: 'Add `TOK_ARROW` (`=>`). Parse single parameter, parenthesised parameters with concise/block body. Add `is_arrow` flag to `JSFunction` to capture lexical `this`.' },
    { title: 'Ternary Operator', cat: 'Parser_and_Lexer', desc: 'Add ternary operator parse rule using `TOK_QUESTION`. Lower to `OP_JUMP_IF_FALSE` / `OP_JUMP`.' },
    { title: 'Compound Assignment Operators', cat: 'Parser_and_Lexer', desc: 'Parse `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&&=`, `||=`, `??=`. Emit `OP_LOAD → OP_ADD → OP_STORE` sequences.' },
    { title: 'for of and for in Loops', cat: 'Parser_and_Lexer', desc: 'Add `TOK_OF` and `TOK_IN`. Parse `for...of` using iterator protocol (`OP_ITER_NEXT`). Parse `for...in` using `OP_FOR_IN_NEXT`.' },
    { title: 'typeof instanceof in void delete', cat: 'Parser_and_Lexer', desc: 'Wire up `typeof`, `instanceof`, `in`, `void`, and `delete` to parser rules. Add `OP_DELETE_PROP` opcode.' },
    { title: 'Prefix and Postfix Operators', cat: 'Parser_and_Lexer', desc: 'Add `TOK_PLUSPLUS` and `TOK_MINUSMINUS`. Parse prefix and postfix forms. Lower to load + add/subtract 1 + store.' },
    { title: 'Bitwise and Shift Operators', cat: 'Parser_and_Lexer', desc: 'Add tokens for `&`, `|`, `^`, `~`, `<<`, `>>`, `>>>`. Insert bitwise parse levels.' },
    { title: 'Exponentiation Operator', cat: 'Parser_and_Lexer', desc: 'Add `TOK_STARSTAR` (`**`). Insert exponentiation parse rule. Lower to `Math.pow` or `OP_POW`.' },
    
    // Syntax Sugar
    { title: 'Destructuring Assignment and Binding', cat: 'Parser_and_Lexer', desc: 'Extend `var` declaration to accept array/object patterns. Lower to sequence of `OP_LOAD_PROP`. Handle default values and rest elements.' },
    { title: 'Shorthand Object Properties and Methods', cat: 'Parser_and_Lexer', desc: 'Accept shorthand properties, method shorthands, and `get`/`set` accessors in object literals.' },
    { title: 'Computed Property Keys', cat: 'Parser_and_Lexer', desc: 'Accept `TOK_LBRACKET` in key position for object literals, parse expression, mark property as computed.' },
    { title: 'Class Declarations and Expressions', cat: 'Parser_and_Lexer', desc: 'Add `TOK_CLASS`, `TOK_EXTENDS`, `TOK_SUPER`, `TOK_STATIC`. Lower to prototype chain setup.' },
    { title: 'switch case break continue', cat: 'Parser_and_Lexer', desc: 'Add `TOK_SWITCH`, `TOK_CASE`, `TOK_BREAK`, `TOK_CONTINUE`, `TOK_DEFAULT`. Add break/continue target stack.' },
    { title: 'Labelled Statements', cat: 'Parser_and_Lexer', desc: 'Add support for labelled statements and break/continue with labels.' },
    { title: 'Generator Functions and yield', cat: 'Parser_and_Lexer', desc: 'Add `TOK_YIELD`. Add `is_generator` flag. Use `ucontext_t` to suspend/resume generator coroutines via `OP_YIELD`.' },
    
    // Bugs / Regressions
    { title: 'String Escape Sequences', cat: 'Parser_and_Lexer', desc: "Process escape sequences `\\n`, `\\t`, `\\r`, `\\\\`, `\\'`, `\\\"`, `\\0`, `\\xNN`, `\\uNNNN` in string literal scanner." },
    { title: 'Strict Equality Disambiguation', cat: 'Parser_and_Lexer', desc: 'Disambiguate `==` vs `===` and `!=` vs `!==` in lexer. Wire to `OP_EQ` and `OP_EQ_LOOSE`.' },

    // Node.js Compat
    { title: 'Readline Module', cat: 'Standard_Library', desc: 'Implement `readline` module for interactive prompt libraries.' },
    { title: 'TTY Module', cat: 'Standard_Library', desc: 'Implement `tty` module and related `process.stdout.isTTY` checks for terminal size and raw mode.' },
    { title: 'Process Stdin Stream', cat: 'Standard_Library', desc: 'Implement fully featured Readable Stream for `process.stdin`.' },
    { title: 'Synchronous FS Descriptors', cat: 'Standard_Library', desc: 'Implement synchronous file descriptor operations `fs.openSync` and `fs.readSync`.' },
    { title: 'Zlib Module', cat: 'Standard_Library', desc: 'Implement `zlib` compression module.' },
    { title: 'Dgram Module', cat: 'Standard_Library', desc: 'Implement `dgram` UDP socket networking module.' }
];

const APPROVED_DIR = path.resolve('references/development/features/approved');

if (!fs.existsSync(APPROVED_DIR)) {
    fs.mkdirSync(APPROVED_DIR, { recursive: true });
}

features.forEach(f => {
    const safeTitle = f.title.replace(/[^a-zA-Z0-9_-]/g, '_');
    const catDir = path.join(APPROVED_DIR, f.cat);
    
    if (!fs.existsSync(catDir)) {
        fs.mkdirSync(catDir, { recursive: true });
    }
    
    const filePath = path.join(catDir, `${safeTitle}.md`);
    const content = `# ${f.title}\n\n**Category**: ${f.cat}\n**Status**: Approved\n\n## Description\n\n${f.desc}\n`;
    
    fs.writeFileSync(filePath, content);
    console.log(`Created: ${filePath}`);
});
