/**
 * @file compiler.c
 * @brief JavaScript to CBC Bytecode Compiler — ES2025 Compatible.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "alloc.h"
#include "compiler.h"
#include "ast.h"
#include "bytecode.h"

// ─── Lexer Token Types ───────────────────────────────────────────────────────
typedef enum {
    TOK_EOF,
    TOK_ERROR,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_REGEX,
    TOK_TEMPLATE_HEAD,    // `...${
    TOK_TEMPLATE_MIDDLE,  // }...${
    TOK_TEMPLATE_TAIL,    // }...`  or `...` (no interpolation)
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_UNDEFINED,
    TOK_LET, TOK_CONST, TOK_VAR, TOK_NEW,
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_DO,
    TOK_FUNCTION, TOK_RETURN,
    TOK_IMPORT, TOK_FROM, TOK_WITH, TOK_EXPORT, TOK_DEFAULT, TOK_AS,
    TOK_ASYNC, TOK_AWAIT,
    TOK_USING, TOK_TRY, TOK_CATCH, TOK_FINALLY, TOK_THROW,
    TOK_SWITCH, TOK_CASE, TOK_BREAK, TOK_CONTINUE,
    TOK_CLASS, TOK_EXTENDS, TOK_SUPER, TOK_STATIC,
    TOK_TYPEOF, TOK_INSTANCEOF, TOK_IN, TOK_OF, TOK_VOID, TOK_DELETE,
    TOK_YIELD, TOK_GET, TOK_SET,
    TOK_DEBUGGER,

    // Arithmetic
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_STARSTAR,          // **

    // Comparison
    TOK_ASSIGN,            // =
    TOK_EQ,                // ===
    TOK_NE,                // !==
    TOK_EQ_LOOSE,          // ==
    TOK_NE_LOOSE,          // !=
    TOK_LT, TOK_LE, TOK_GT, TOK_GE,

    // Logical
    TOK_AND,               // &&
    TOK_OR,                // ||
    TOK_NULLISH,           // ??
    TOK_NOT,               // !

    // Bitwise
    TOK_AMPERSAND,         // &
    TOK_PIPE,              // |
    TOK_CARET,             // ^
    TOK_TILDE,             // ~
    TOK_SHL,               // <<
    TOK_SHR,               // >>
    TOK_USHR,              // >>>

    // Compound assignment
    TOK_PLUS_ASSIGN,       // +=
    TOK_MINUS_ASSIGN,      // -=
    TOK_STAR_ASSIGN,       // *=
    TOK_SLASH_ASSIGN,      // /=
    TOK_PERCENT_ASSIGN,    // %=
    TOK_STARSTAR_ASSIGN,   // **=
    TOK_AND_ASSIGN,        // &&=
    TOK_OR_ASSIGN,         // ||=
    TOK_NULLISH_ASSIGN,    // ??=
    TOK_BITAND_ASSIGN,     // &=
    TOK_BITOR_ASSIGN,      // |=
    TOK_BITXOR_ASSIGN,     // ^=
    TOK_SHL_ASSIGN,        // <<=
    TOK_SHR_ASSIGN,        // >>=
    TOK_USHR_ASSIGN,       // >>>=

    // Increment/Decrement
    TOK_PLUSPLUS,          // ++
    TOK_MINUSMINUS,        // --

    // Structural
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT, TOK_SEMICOLON, TOK_COLON,
    TOK_ARROW,             // =>
    TOK_SPREAD,            // ...
    TOK_QUESTION,          // ?
    TOK_OPTIONAL_CHAIN,    // ?.
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    double number_value;
    // For template literal parts: holds the cooked string value
    char* template_str;
    int   template_str_len;
} Token;

typedef struct {
    const char* source;
    int cursor;
    bool last_was_value; // for regex disambiguation
} Lexer;

// ─── String escape processing ─────────────────────────────────────────────────
static char hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

// Write a UTF-8 encoded codepoint into buf, return bytes written
static int encode_utf8(uint32_t cp, char* buf) {
    if (cp < 0x80) { buf[0] = (char)cp; return 1; }
    if (cp < 0x800) { buf[0] = 0xC0|(cp>>6); buf[1] = 0x80|(cp&0x3F); return 2; }
    if (cp < 0x10000) { buf[0]=0xE0|(cp>>12); buf[1]=0x80|((cp>>6)&0x3F); buf[2]=0x80|(cp&0x3F); return 3; }
    buf[0]=0xF0|(cp>>18); buf[1]=0x80|((cp>>12)&0x3F); buf[2]=0x80|((cp>>6)&0x3F); buf[3]=0x80|(cp&0x3F); return 4;
}

// Process escape sequences in a raw string segment [start, end)
// Returns newly malloc'd string with escapes resolved.
static char* process_string_escapes(const char* start, int raw_len) {
    char* out = malloc(raw_len + 1);
    int wi = 0;
    const char* p = start;
    const char* end = start + raw_len;
    while (p < end) {
        if (*p != '\\') {
            out[wi++] = *p++;
            continue;
        }
        p++; // skip backslash
        if (p >= end) break;
        char e = *p++;
        switch (e) {
            case 'n':  out[wi++] = '\n'; break;
            case 'r':  out[wi++] = '\r'; break;
            case 't':  out[wi++] = '\t'; break;
            case 'b':  out[wi++] = '\b'; break;
            case 'f':  out[wi++] = '\f'; break;
            case 'v':  out[wi++] = '\v'; break;
            case '0':  out[wi++] = '\0'; break;
            case '\\': out[wi++] = '\\'; break;
            case '\'': out[wi++] = '\''; break;
            case '"':  out[wi++] = '"';  break;
            case '`':  out[wi++] = '`';  break;
            case 'x': {
                if (p + 2 <= end) {
                    out[wi++] = (char)((hex_val(p[0]) << 4) | hex_val(p[1]));
                    p += 2;
                }
                break;
            }
            case 'u': {
                if (p < end && *p == '{') {
                    // \u{XXXXXX}
                    p++;
                    uint32_t cp = 0;
                    while (p < end && *p != '}') {
                        cp = (cp << 4) | hex_val(*p++);
                    }
                    if (p < end) p++; // skip }
                    char ubuf[5];
                    int nb = encode_utf8(cp, ubuf);
                    for (int i = 0; i < nb; i++) out[wi++] = ubuf[i];
                } else if (p + 4 <= end) {
                    uint32_t cp = (hex_val(p[0])<<12)|(hex_val(p[1])<<8)|(hex_val(p[2])<<4)|hex_val(p[3]);
                    p += 4;
                    char ubuf[5];
                    int nb = encode_utf8(cp, ubuf);
                    for (int i = 0; i < nb; i++) out[wi++] = ubuf[i];
                }
                break;
            }
            default: out[wi++] = e; break;
        }
    }
    out[wi] = '\0';
    // realloc to exact size
    char* shrunk = realloc(out, wi + 1);
    return shrunk ? shrunk : out;
}

// ─── Lexer: scan_token ────────────────────────────────────────────────────────
static Token scan_token(Lexer* lex) {
    // Skip whitespace and comments
    while (lex->source[lex->cursor] != '\0') {
        char c = lex->source[lex->cursor];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lex->cursor++;
        } else if (c == '/' && lex->source[lex->cursor + 1] == '/') {
            lex->cursor += 2;
            while (lex->source[lex->cursor] != '\n' && lex->source[lex->cursor] != '\0')
                lex->cursor++;
        } else if (c == '/' && lex->source[lex->cursor + 1] == '*') {
            lex->cursor += 2;
            while (lex->source[lex->cursor] != '\0' &&
                   !(lex->source[lex->cursor] == '*' && lex->source[lex->cursor + 1] == '/'))
                lex->cursor++;
            if (lex->source[lex->cursor] != '\0') lex->cursor += 2;
        } else {
            break;
        }
    }

    char c = lex->source[lex->cursor];
    if (c == '\0') {
        lex->last_was_value = false;
        return (Token){TOK_EOF, &lex->source[lex->cursor], 0, 0.0, NULL, 0};
    }

    // ── Identifiers and keywords ─────────────────────────────────────────────
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
        int start = lex->cursor++;
        while ((lex->source[lex->cursor] >= 'a' && lex->source[lex->cursor] <= 'z') ||
               (lex->source[lex->cursor] >= 'A' && lex->source[lex->cursor] <= 'Z') ||
               (lex->source[lex->cursor] >= '0' && lex->source[lex->cursor] <= '9') ||
               lex->source[lex->cursor] == '_' || lex->source[lex->cursor] == '$')
            lex->cursor++;
        int len = lex->cursor - start;
        const char* kw = &lex->source[start];
        TokenType type = TOK_IDENTIFIER;
#define KW(s, t) if (len==(int)sizeof(s)-1 && memcmp(kw,s,len)==0) type=t
        KW("let",       TOK_LET);       else KW("const",     TOK_CONST);
        else KW("var",      TOK_VAR);       else KW("new",       TOK_NEW);
        else KW("if",       TOK_IF);        else KW("else",      TOK_ELSE);
        else KW("while",    TOK_WHILE);     else KW("for",       TOK_FOR);
        else KW("do",       TOK_DO);
        else KW("function", TOK_FUNCTION);  else KW("return",    TOK_RETURN);
        else KW("import",   TOK_IMPORT);    else KW("from",      TOK_FROM);
        else KW("with",     TOK_WITH);      else KW("export",    TOK_EXPORT);
        else KW("default",  TOK_DEFAULT);   else KW("as",        TOK_AS);
        else KW("true",     TOK_TRUE);      else KW("false",     TOK_FALSE);
        else KW("null",     TOK_NULL);      else KW("undefined", TOK_UNDEFINED);
        else KW("async",    TOK_ASYNC);     else KW("await",     TOK_AWAIT);
        else KW("using",    TOK_USING);     else KW("try",       TOK_TRY);
        else KW("catch",    TOK_CATCH);     else KW("finally",   TOK_FINALLY);
        else KW("throw",    TOK_THROW);
        else KW("switch",   TOK_SWITCH);    else KW("case",      TOK_CASE);
        else KW("break",    TOK_BREAK);     else KW("continue",  TOK_CONTINUE);
        else KW("class",    TOK_CLASS);     else KW("extends",   TOK_EXTENDS);
        else KW("super",    TOK_SUPER);     else KW("static",    TOK_STATIC);
        else KW("typeof",   TOK_TYPEOF);    else KW("instanceof",TOK_INSTANCEOF);
        else KW("in",       TOK_IN);        else KW("of",        TOK_OF);
        else KW("void",     TOK_VOID);      else KW("delete",    TOK_DELETE);
        else KW("yield",    TOK_YIELD);     else KW("get",       TOK_GET);
        else KW("set",      TOK_SET);       else KW("debugger",  TOK_DEBUGGER);
#undef KW
        // keywords that follow a value context are still value-like after
        bool is_val_kw = (type==TOK_TRUE||type==TOK_FALSE||type==TOK_NULL||
                          type==TOK_UNDEFINED||type==TOK_IDENTIFIER||
                          type==TOK_TYPEOF||type==TOK_VOID||type==TOK_DELETE||
                          type==TOK_NEW||type==TOK_SUPER||type==TOK_YIELD);
        lex->last_was_value = is_val_kw;
        return (Token){type, &lex->source[start], len, 0.0, NULL, 0};
    }

    // ── Numbers ──────────────────────────────────────────────────────────────
    if ((c >= '0' && c <= '9') ||
        (c == '.' && lex->source[lex->cursor+1] >= '0' && lex->source[lex->cursor+1] <= '9')) {
        int start = lex->cursor;
        // Hex: 0x...
        if (c == '0' && (lex->source[lex->cursor+1] == 'x' || lex->source[lex->cursor+1] == 'X')) {
            lex->cursor += 2;
            while ((lex->source[lex->cursor] >= '0' && lex->source[lex->cursor] <= '9') ||
                   (lex->source[lex->cursor] >= 'a' && lex->source[lex->cursor] <= 'f') ||
                   (lex->source[lex->cursor] >= 'A' && lex->source[lex->cursor] <= 'F') ||
                   lex->source[lex->cursor] == '_')
                lex->cursor++;
        } else if (c == '0' && (lex->source[lex->cursor+1] == 'b' || lex->source[lex->cursor+1] == 'B')) {
            // Binary: 0b...
            lex->cursor += 2;
            while (lex->source[lex->cursor] == '0' || lex->source[lex->cursor] == '1' || lex->source[lex->cursor] == '_')
                lex->cursor++;
        } else if (c == '0' && (lex->source[lex->cursor+1] == 'o' || lex->source[lex->cursor+1] == 'O')) {
            // Octal: 0o...
            lex->cursor += 2;
            while ((lex->source[lex->cursor] >= '0' && lex->source[lex->cursor] <= '7') || lex->source[lex->cursor] == '_')
                lex->cursor++;
        } else {
            char* endptr;
            double val = strtod(&lex->source[start], &endptr);
            int len = (int)(endptr - &lex->source[start]);
            lex->cursor = start + len;
            lex->last_was_value = true;
            return (Token){TOK_NUMBER, &lex->source[start], len, val, NULL, 0};
        }
        char tmp[128];
        int raw_len = lex->cursor - start;
        int copy_len = raw_len < 127 ? raw_len : 127;
        memcpy(tmp, &lex->source[start], copy_len);
        tmp[copy_len] = '\0';
        // Remove numeric separators for parsing
        char clean[128]; int ci = 0;
        for (int i = 0; i < copy_len; i++) if (tmp[i] != '_') clean[ci++] = tmp[i];
        clean[ci] = '\0';
        double val = strtod(clean, NULL);
        lex->last_was_value = true;
        return (Token){TOK_NUMBER, &lex->source[start], raw_len, val, NULL, 0};
    }

    // ── String literals (single/double quote) ────────────────────────────────
    if (c == '"' || c == '\'') {
        char quote = c;
        lex->cursor++;
        int start = lex->cursor;
        while (lex->source[lex->cursor] != '\0' && lex->source[lex->cursor] != quote) {
            if (lex->source[lex->cursor] == '\\') lex->cursor++; // skip escape char
            if (lex->source[lex->cursor] != '\0') lex->cursor++;
        }
        int raw_len = lex->cursor - start;
        const char* raw = &lex->source[start];
        if (lex->source[lex->cursor] == quote) lex->cursor++;
        // Process escape sequences
        char* cooked = process_string_escapes(raw, raw_len);
        Token tok = {TOK_STRING, raw, (int)strlen(cooked), 0.0, cooked, (int)strlen(cooked)};
        tok.length = (int)strlen(cooked); // use cooked length
        // Store raw start for source reference — but we need cooked value
        // We'll use template_str to carry the cooked value
        lex->last_was_value = true;
        return tok;
    }

    // ── Template literals ────────────────────────────────────────────────────
    if (c == '`') {
        lex->cursor++;
        int seg_start = lex->cursor;
        // Collect chars up to ${ or closing `
        char* buf = NULL;
        int buf_len = 0;
        while (lex->source[lex->cursor] != '\0') {
            char ch = lex->source[lex->cursor];
            if (ch == '`') {
                // End of template
                char* seg = process_string_escapes(&lex->source[seg_start], lex->cursor - seg_start);
                int seg_len = (int)strlen(seg);
                buf = realloc(buf, buf_len + seg_len + 1);
                memcpy(buf + buf_len, seg, seg_len + 1);
                buf_len += seg_len;
                free(seg);
                lex->cursor++;
                lex->last_was_value = true;
                return (Token){TOK_TEMPLATE_TAIL, &lex->source[seg_start], 0, 0.0, buf, buf_len};
            }
            if (ch == '$' && lex->source[lex->cursor+1] == '{') {
                char* seg = process_string_escapes(&lex->source[seg_start], lex->cursor - seg_start);
                int seg_len = (int)strlen(seg);
                buf = realloc(buf, buf_len + seg_len + 1);
                memcpy(buf + buf_len, seg, seg_len + 1);
                buf_len += seg_len;
                free(seg);
                lex->cursor += 2;
                lex->last_was_value = false;
                return (Token){TOK_TEMPLATE_HEAD, &lex->source[seg_start], 0, 0.0, buf, buf_len};
            }
            if (ch == '\\' && lex->source[lex->cursor+1] != '\0') lex->cursor++;
            lex->cursor++;
        }
        // Unterminated — return whatever we have
        Token t = {TOK_TEMPLATE_TAIL, &lex->source[seg_start], 0, 0.0, strdup(""), 0};
        lex->last_was_value = true;
        return t;
    }

    // ── Regex literals (context-sensitive) ───────────────────────────────────
    // A '/' after an operator, keyword, or start-of-expression starts a regex.
    if (c == '/' && !lex->last_was_value) {
        // Might be a regex
        int start = lex->cursor++;
        bool in_class = false;
        while (lex->source[lex->cursor] != '\0') {
            char ch = lex->source[lex->cursor];
            if (ch == '\\' && lex->source[lex->cursor+1] != '\0') { lex->cursor += 2; continue; }
            if (ch == '[') in_class = true;
            if (ch == ']') in_class = false;
            if (ch == '/' && !in_class) { lex->cursor++; break; }
            if (ch == '\n') break; // invalid regex
            lex->cursor++;
        }
        // Consume flags
        while ((lex->source[lex->cursor] >= 'a' && lex->source[lex->cursor] <= 'z') ||
               (lex->source[lex->cursor] >= 'A' && lex->source[lex->cursor] <= 'Z'))
            lex->cursor++;
        int len = lex->cursor - start;
        lex->last_was_value = true;
        return (Token){TOK_REGEX, &lex->source[start], len, 0.0, NULL, 0};
    }

    // ── Multi-char operators ─────────────────────────────────────────────────
    char c2 = lex->source[lex->cursor + 1];
    char c3 = lex->source[lex->cursor + 2];
    char c4 = lex->source[lex->cursor + 3];

#define ADV(n, t) do { lex->cursor += (n); lex->last_was_value = false; return (Token){(t), &lex->source[lex->cursor-(n)], (n), 0.0, NULL, 0}; } while(0)
#define ADV_VAL(n, t) do { lex->cursor += (n); lex->last_was_value = true; return (Token){(t), &lex->source[lex->cursor-(n)], (n), 0.0, NULL, 0}; } while(0)

    // Four-char: >>>=
    if (c=='>' && c2=='>' && c3=='>' && c4=='=') ADV(4, TOK_USHR_ASSIGN);
    // Three-char
    if (c=='=' && c2=='=' && c3=='=') ADV(3, TOK_EQ);
    if (c=='!' && c2=='=' && c3=='=') ADV(3, TOK_NE);
    if (c=='>' && c2=='>' && c3=='>') ADV(3, TOK_USHR);
    if (c=='<' && c2=='<' && c3=='=') ADV(3, TOK_SHL_ASSIGN);
    if (c=='>' && c2=='>' && c3=='=') ADV(3, TOK_SHR_ASSIGN);
    if (c=='*' && c2=='*' && c3=='=') ADV(3, TOK_STARSTAR_ASSIGN);
    if (c=='&' && c2=='&' && c3=='=') ADV(3, TOK_AND_ASSIGN);
    if (c=='|' && c2=='|' && c3=='=') ADV(3, TOK_OR_ASSIGN);
    if (c=='?' && c2=='?' && c3=='=') ADV(3, TOK_NULLISH_ASSIGN);
    if (c=='.' && c2=='.' && c3=='.') ADV(3, TOK_SPREAD);
    // Two-char
    if (c=='=' && c2=='=') ADV(2, TOK_EQ_LOOSE);
    if (c=='!' && c2=='=') ADV(2, TOK_NE_LOOSE);
    if (c=='<' && c2=='=') ADV(2, TOK_LE);
    if (c=='>' && c2=='=') ADV(2, TOK_GE);
    if (c=='&' && c2=='&') ADV(2, TOK_AND);
    if (c=='|' && c2=='|') ADV(2, TOK_OR);
    if (c=='?' && c2=='?') ADV(2, TOK_NULLISH);
    if (c=='?' && c2=='.') ADV(2, TOK_OPTIONAL_CHAIN);
    if (c=='=' && c2=='>') ADV(2, TOK_ARROW);
    if (c=='+' && c2=='+') ADV_VAL(2, TOK_PLUSPLUS);
    if (c=='-' && c2=='-') ADV_VAL(2, TOK_MINUSMINUS);
    if (c=='*' && c2=='*') ADV(2, TOK_STARSTAR);
    if (c=='+' && c2=='=') ADV(2, TOK_PLUS_ASSIGN);
    if (c=='-' && c2=='=') ADV(2, TOK_MINUS_ASSIGN);
    if (c=='*' && c2=='=') ADV(2, TOK_STAR_ASSIGN);
    if (c=='/' && c2=='=') ADV(2, TOK_SLASH_ASSIGN);
    if (c=='%' && c2=='=') ADV(2, TOK_PERCENT_ASSIGN);
    if (c=='&' && c2=='=') ADV(2, TOK_BITAND_ASSIGN);
    if (c=='|' && c2=='=') ADV(2, TOK_BITOR_ASSIGN);
    if (c=='^' && c2=='=') ADV(2, TOK_BITXOR_ASSIGN);
    if (c=='<' && c2=='<') ADV(2, TOK_SHL);
    if (c=='>' && c2=='>') ADV(2, TOK_SHR);

    // Single-char
    lex->cursor++;
    TokenType type;
    bool is_val = false;
    switch (c) {
        case '+': type = TOK_PLUS;      break;
        case '-': type = TOK_MINUS;     break;
        case '*': type = TOK_STAR;      break;
        case '/': type = TOK_SLASH;     break;
        case '%': type = TOK_PERCENT;   break;
        case '=': type = TOK_ASSIGN;    break;
        case '<': type = TOK_LT;        break;
        case '>': type = TOK_GT;        break;
        case '(': type = TOK_LPAREN;    break;
        case ')': type = TOK_RPAREN;    is_val = true; break;
        case '{': type = TOK_LBRACE;    break;
        case '}': type = TOK_RBRACE;    is_val = true; break;
        case '[': type = TOK_LBRACKET;  break;
        case ']': type = TOK_RBRACKET;  is_val = true; break;
        case ',': type = TOK_COMMA;     break;
        case '.': type = TOK_DOT;       break;
        case ';': type = TOK_SEMICOLON; break;
        case ':': type = TOK_COLON;     break;
        case '!': type = TOK_NOT;       break;
        case '&': type = TOK_AMPERSAND; break;
        case '|': type = TOK_PIPE;      break;
        case '^': type = TOK_CARET;     break;
        case '~': type = TOK_TILDE;     break;
        case '?': type = TOK_QUESTION;  break;
        default:
            lex->last_was_value = false;
            return (Token){TOK_ERROR, &lex->source[lex->cursor-1], 1, 0.0, NULL, 0};
    }
#undef ADV
#undef ADV_VAL
    lex->last_was_value = is_val;
    return (Token){type, &lex->source[lex->cursor-1], 1, 0.0, NULL, 0};
}

// Special: rescan after `}` in template interpolation context
static Token scan_template_continuation(Lexer* lex) {
    // We're past the `}`, now re-entering template mode
    int seg_start = lex->cursor;
    char* buf = NULL;
    int buf_len = 0;
    while (lex->source[lex->cursor] != '\0') {
        char ch = lex->source[lex->cursor];
        if (ch == '`') {
            char* seg = process_string_escapes(&lex->source[seg_start], lex->cursor - seg_start);
            int seg_len = (int)strlen(seg);
            buf = realloc(buf, buf_len + seg_len + 1);
            memcpy(buf + buf_len, seg, seg_len + 1);
            buf_len += seg_len;
            free(seg);
            lex->cursor++;
            lex->last_was_value = true;
            return (Token){TOK_TEMPLATE_TAIL, &lex->source[seg_start], 0, 0.0, buf, buf_len};
        }
        if (ch == '$' && lex->source[lex->cursor+1] == '{') {
            char* seg = process_string_escapes(&lex->source[seg_start], lex->cursor - seg_start);
            int seg_len = (int)strlen(seg);
            buf = realloc(buf, buf_len + seg_len + 1);
            memcpy(buf + buf_len, seg, seg_len + 1);
            buf_len += seg_len;
            free(seg);
            lex->cursor += 2;
            lex->last_was_value = false;
            return (Token){TOK_TEMPLATE_MIDDLE, &lex->source[seg_start], 0, 0.0, buf, buf_len};
        }
        if (ch == '\\' && lex->source[lex->cursor+1] != '\0') lex->cursor++;
        lex->cursor++;
    }
    return (Token){TOK_TEMPLATE_TAIL, &lex->source[seg_start], 0, 0.0, strdup(""), 0};
}

// ─── AST helpers ──────────────────────────────────────────────────────────────
struct CompilerScope;
typedef struct CompilerScope CompilerScope;
static void free_scope(CompilerScope* scope);

ASTNode* ast_new_node(ASTNodeType type) {
    ASTNode* node = calloc(1, sizeof(ASTNode));
    node->type = type;
    node->scope = NULL;
    return node;
}

void ast_free_node(ASTNode* node) {
    if (!node) return;
    if (node->scope) {
        if (node->type != AST_BLOCK || !node->as.block.is_inline)
            free_scope((CompilerScope*)node->scope);
    }
    switch (node->type) {
        case AST_LITERAL_STRING:
            if (node->as.string.value) free(node->as.string.value);
            break;
        case AST_LITERAL_REGEX:
            free(node->as.regex.pattern);
            free(node->as.regex.flags);
            break;
        case AST_IDENTIFIER:
            free(node->as.identifier.name);
            break;
        case AST_VAR_DECL:
            free(node->as.var_decl.name);
            ast_free_node(node->as.var_decl.init);
            for (int i = 0; i < node->as.var_decl.bind_count; i++) {
                free(node->as.var_decl.bindings[i].name);
                free(node->as.var_decl.bindings[i].key);
                ast_free_node(node->as.var_decl.bindings[i].default_val);
                ast_free_node(node->as.var_decl.bindings[i].pattern);
            }
            free(node->as.var_decl.bindings);
            break;
        case AST_ASSIGN:
            ast_free_node(node->as.assign.target);
            ast_free_node(node->as.assign.value);
            break;
        case AST_BINARY:
            ast_free_node(node->as.binary.left);
            ast_free_node(node->as.binary.right);
            break;
        case AST_UNARY:
            ast_free_node(node->as.unary.expr);
            break;
        case AST_POSTFIX:
            ast_free_node(node->as.postfix.expr);
            break;
        case AST_TERNARY:
            ast_free_node(node->as.ternary.cond);
            ast_free_node(node->as.ternary.then_expr);
            ast_free_node(node->as.ternary.else_expr);
            break;
        case AST_CALL:
        case AST_NEW_CALL:
            ast_free_node(node->as.call.callee);
            for (int i = 0; i < node->as.call.arg_count; i++)
                ast_free_node(node->as.call.args[i]);
            free(node->as.call.args);
            break;
        case AST_BLOCK:
            for (int i = 0; i < node->as.block.count; i++)
                ast_free_node(node->as.block.statements[i]);
            free(node->as.block.statements);
            break;
        case AST_IF:
            ast_free_node(node->as.if_stmt.cond);
            ast_free_node(node->as.if_stmt.then_branch);
            ast_free_node(node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
        case AST_DO_WHILE:
            ast_free_node(node->as.while_stmt.cond);
            ast_free_node(node->as.while_stmt.body);
            break;
        case AST_FOR:
            ast_free_node(node->as.for_stmt.init);
            ast_free_node(node->as.for_stmt.cond);
            ast_free_node(node->as.for_stmt.update);
            ast_free_node(node->as.for_stmt.body);
            break;
        case AST_FOR_OF:
            free(node->as.for_of.binding_name);
            ast_free_node(node->as.for_of.binding_decl);
            ast_free_node(node->as.for_of.iterable);
            ast_free_node(node->as.for_of.body);
            break;
        case AST_FOR_IN:
            free(node->as.for_in.binding_name);
            ast_free_node(node->as.for_in.binding_decl);
            ast_free_node(node->as.for_in.object);
            ast_free_node(node->as.for_in.body);
            break;
        case AST_BREAK:
            free(node->as.break_stmt.label);
            break;
        case AST_CONTINUE:
            free(node->as.continue_stmt.label);
            break;
        case AST_SWITCH:
            ast_free_node(node->as.switch_stmt.discriminant);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTSwitchCase* cs = node->as.switch_stmt.cases[i];
                ast_free_node(cs->test);
                for (int j = 0; j < cs->body_count; j++)
                    ast_free_node(cs->body[j]);
                free(cs->body);
                free(cs);
            }
            free(node->as.switch_stmt.cases);
            break;
        case AST_RETURN:
            ast_free_node(node->as.return_stmt.expr);
            break;
        case AST_FUNCTION:
            free(node->as.function.name);
            for (int i = 0; i < node->as.function.param_count; i++) {
                free(node->as.function.params[i].name);
                ast_free_node(node->as.function.params[i].default_val);
            }
            free(node->as.function.params);
            ast_free_node(node->as.function.body);
            break;
        case AST_OBJECT:
            for (int i = 0; i < node->as.object.count; i++) {
                free(node->as.object.keys[i]);
                ast_free_node(node->as.object.key_exprs[i]);
                ast_free_node(node->as.object.values[i]);
            }
            free(node->as.object.keys);
            free(node->as.object.key_exprs);
            free(node->as.object.values);
            free(node->as.object.prop_flags);
            break;
        case AST_ARRAY:
            for (int i = 0; i < node->as.array.count; i++)
                ast_free_node(node->as.array.elements[i]);
            free(node->as.array.elements);
            break;
        case AST_SPREAD:
            ast_free_node(node->as.spread_expr);
            break;
        case AST_TEMPLATE_LITERAL:
            for (int i = 0; i < node->as.tmpl.part_count; i++)
                ast_free_node(node->as.tmpl.parts[i]);
            free(node->as.tmpl.parts);
            break;
        case AST_PROP_ACCESS:
        case AST_OPTIONAL_CHAIN:
            ast_free_node(node->as.prop_access.obj);
            ast_free_node(node->as.prop_access.prop);
            break;
        case AST_EXPR_STMT:
            ast_free_node(node->as.expr_stmt);
            break;
        case AST_AWAIT:
            ast_free_node(node->as.await_expr.expr);
            break;
        case AST_YIELD:
            ast_free_node(node->as.yield_expr.expr);
            break;
        case AST_TRY:
            ast_free_node(node->as.try_stmt.try_block);
            free(node->as.try_stmt.catch_param);
            ast_free_node(node->as.try_stmt.catch_block);
            ast_free_node(node->as.try_stmt.finally_block);
            break;
        case AST_THROW:
            ast_free_node(node->as.throw_stmt.throw_stmt);
            break;
        case AST_SEQUENCE:
            for (int i = 0; i < node->as.sequence.count; i++)
                ast_free_node(node->as.sequence.exprs[i]);
            free(node->as.sequence.exprs);
            break;
        case AST_CLASS:
            free(node->as.class_decl.name);
            ast_free_node(node->as.class_decl.superclass);
            for (int i = 0; i < node->as.class_decl.method_count; i++) {
                ASTClassMethod* m = node->as.class_decl.methods[i];
                free(m->name);
                ast_free_node(m->name_expr);
                ast_free_node(m->func_node);
                free(m);
            }
            free(node->as.class_decl.methods);
            break;
        default:
            break;
    }
    free(node);
}

// ─── Parser State ─────────────────────────────────────────────────────────────
typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    bool has_error;
    // template nesting depth (for } disambiguation)
    int tmpl_depth;
    int* tmpl_brace_stack; // counts of { inside each template interpolation
    int  tmpl_brace_top;
} Parser;

static void advance(Parser* parser) {
    // Free any template string from the previous token
    // (handled by AST nodes that consume them)
    parser->previous = parser->current;
    while (1) {
        Token tok = scan_token(&parser->lexer);
        if (tok.type != TOK_ERROR) {
            parser->current = tok;
            break;
        }
        fprintf(stderr, "Lexical Error at '%.*s'\n", tok.length, tok.start);
        parser->has_error = true;
    }
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
    if (check(parser, type)) { advance(parser); return true; }
    return false;
}

static void consume(Parser* parser, TokenType type, const char* message) {
    if (parser->current.type == type) { advance(parser); return; }
    fprintf(stderr, "Parser Error: %s at '%.*s'\n", message, parser->current.length, parser->current.start);
    parser->has_error = true;
}

// ─── Forward Declarations ─────────────────────────────────────────────────────
static ASTNode* parse_statement(Parser* parser);
static ASTNode* parse_expr(Parser* parser);
static ASTNode* parse_assignment(Parser* parser);
static ASTNode* parse_ternary(Parser* parser);
static ASTFunction* parse_function_body(Parser* parser, bool is_arrow, bool is_async, bool is_generator);

// ─── Parameter list parsing ───────────────────────────────────────────────────
static void parse_param_list(Parser* parser, ASTParam** out_params, int* out_count) {
    ASTParam* params = NULL;
    int count = 0;
    if (!check(parser, TOK_RPAREN)) {
        do {
            params = realloc(params, (count + 1) * sizeof(ASTParam));
            ASTParam* p = &params[count++];
            memset(p, 0, sizeof(ASTParam));
            if (match(parser, TOK_SPREAD)) {
                p->is_rest = true;
                consume(parser, TOK_IDENTIFIER, "Expected parameter name after '...'");
                p->name = strndup(parser->previous.start, parser->previous.length);
                break; // rest must be last
            }
            consume(parser, TOK_IDENTIFIER, "Expected parameter name");
            p->name = strndup(parser->previous.start, parser->previous.length);
            if (match(parser, TOK_ASSIGN)) {
                p->default_val = parse_assignment(parser);
            }
        } while (match(parser, TOK_COMMA) && !check(parser, TOK_RPAREN));
    }
    *out_params = params;
    *out_count = count;
}

// ─── Primary expression parsing ───────────────────────────────────────────────
static ASTNode* parse_primary(Parser* parser) {
    // Number
    if (match(parser, TOK_NUMBER)) {
        ASTNode* n = ast_new_node(AST_LITERAL_NUMBER);
        n->as.number.value = parser->previous.number_value;
        return n;
    }
    // String
    if (match(parser, TOK_STRING)) {
        ASTNode* n = ast_new_node(AST_LITERAL_STRING);
        // Use cooked value if available (escape-processed)
        if (parser->previous.template_str) {
            n->as.string.value = parser->previous.template_str;
            n->as.string.length = parser->previous.template_str_len;
        } else {
            n->as.string.value = strndup(parser->previous.start, parser->previous.length);
            n->as.string.length = parser->previous.length;
        }
        return n;
    }
    // Booleans, null, undefined
    if (match(parser, TOK_TRUE))  { ASTNode* n = ast_new_node(AST_LITERAL_BOOL); n->as.boolean.value = true; return n; }
    if (match(parser, TOK_FALSE)) { ASTNode* n = ast_new_node(AST_LITERAL_BOOL); n->as.boolean.value = false; return n; }
    if (match(parser, TOK_NULL))      return ast_new_node(AST_LITERAL_NULL);
    if (match(parser, TOK_UNDEFINED)) return ast_new_node(AST_LITERAL_UNDEFINED);
    // Regex
    if (match(parser, TOK_REGEX)) {
        // Parse /pattern/flags from token
        const char* src = parser->previous.start + 1; // skip leading /
        int raw_len = parser->previous.length - 1;
        // find closing /
        int pi = 0;
        bool in_class = false;
        while (pi < raw_len) {
            if (src[pi] == '\\') { pi += 2; continue; }
            if (src[pi] == '[') in_class = true;
            if (src[pi] == ']') in_class = false;
            if (src[pi] == '/' && !in_class) break;
            pi++;
        }
        ASTNode* n = ast_new_node(AST_LITERAL_REGEX);
        n->as.regex.pattern = strndup(src, pi);
        n->as.regex.flags = strndup(src + pi + 1, raw_len - pi - 1);
        return n;
    }
    // Template literal
    if (match(parser, TOK_TEMPLATE_HEAD) || match(parser, TOK_TEMPLATE_TAIL)) {
        ASTNode* n = ast_new_node(AST_TEMPLATE_LITERAL);
        n->as.tmpl.parts = NULL;
        n->as.tmpl.part_count = 0;
        // First segment (already consumed as TOK_TEMPLATE_HEAD or TAIL)
        bool is_tail = (parser->previous.type == TOK_TEMPLATE_TAIL);
        // Add string part
        ASTNode* str_part = ast_new_node(AST_LITERAL_STRING);
        str_part->as.string.value = parser->previous.template_str
            ? parser->previous.template_str : strdup("");
        str_part->as.string.length = parser->previous.template_str_len;
        n->as.tmpl.parts = realloc(n->as.tmpl.parts, (n->as.tmpl.part_count+1)*sizeof(ASTNode*));
        n->as.tmpl.parts[n->as.tmpl.part_count++] = str_part;
        while (!is_tail) {
            // Parse expression
            ASTNode* expr = parse_expr(parser);
            n->as.tmpl.parts = realloc(n->as.tmpl.parts, (n->as.tmpl.part_count+1)*sizeof(ASTNode*));
            n->as.tmpl.parts[n->as.tmpl.part_count++] = expr;
            // After expression, expect } then rescan as template continuation
            if (parser->current.type == TOK_RBRACE) {
                parser->previous = parser->current;
                parser->lexer.cursor--; // back up (advance will rescan)
                // Actually: just rescan the template continuation directly
                parser->lexer.cursor++; // skip the }
                Token cont = scan_template_continuation(&parser->lexer);
                parser->previous = parser->current;
                parser->current = cont;
                advance(parser); // move forward
                // actually: the tok is now in previous after advance
            } else {
                // consume the rbrace
                consume(parser, TOK_RBRACE, "Expected '}' to close template expression");
                // rescan continuation
                Token cont = scan_template_continuation(&parser->lexer);
                parser->current = cont;
                advance(parser);
            }
            is_tail = (parser->previous.type == TOK_TEMPLATE_TAIL);
            // Add the string portion
            ASTNode* sp2 = ast_new_node(AST_LITERAL_STRING);
            sp2->as.string.value = parser->previous.template_str
                ? parser->previous.template_str : strdup("");
            sp2->as.string.length = parser->previous.template_str_len;
            n->as.tmpl.parts = realloc(n->as.tmpl.parts, (n->as.tmpl.part_count+1)*sizeof(ASTNode*));
            n->as.tmpl.parts[n->as.tmpl.part_count++] = sp2;
            if (parser->previous.type == TOK_TEMPLATE_TAIL) break;
        }
        return n;
    }
    // super
    if (match(parser, TOK_SUPER)) {
        ASTNode* n = ast_new_node(AST_IDENTIFIER);
        n->as.identifier.name = strdup("super");
        return n;
    }
    // Identifier (possibly arrow function)
    if (check(parser, TOK_IDENTIFIER)) {
        // Check for arrow: identifier =>
        // Save state to rewind
        Lexer saved_lex = parser->lexer;
        Token saved_cur = parser->current;
        Token saved_prev = parser->previous;
        advance(parser); // consume identifier
        if (check(parser, TOK_ARROW)) {
            // Single-param concise arrow: x => expr
            char* param_name = strndup(saved_cur.start, saved_cur.length);
            advance(parser); // consume =>
            ASTNode* n = ast_new_node(AST_FUNCTION);
            n->as.function.is_arrow = true;
            n->as.function.name = NULL;
            n->as.function.param_count = 1;
            n->as.function.params = calloc(1, sizeof(ASTParam));
            n->as.function.params[0].name = param_name;
            if (check(parser, TOK_LBRACE)) {
                n->as.function.body = parse_statement(parser);
                n->as.function.concise_body = false;
            } else {
                n->as.function.body = parse_assignment(parser);
                n->as.function.concise_body = true;
            }
            return n;
        }
        // Not arrow — it's just an identifier
        ASTNode* n = ast_new_node(AST_IDENTIFIER);
        n->as.identifier.name = strndup(saved_cur.start, saved_cur.length);
        return n;
    }
    // Grouping or arrow function with multiple params: (...)
    if (match(parser, TOK_LPAREN)) {
        // Check for arrow: () => or (params) =>
        // Save state
        Lexer saved_lex = parser->lexer;
        Token saved_cur = parser->current;
        Token saved_prev = parser->previous;
        // If immediately ) or has simple param list, check for =>
        // Strategy: try to parse as param list, peek for =>
        // Simple approach: collect balanced parens content, check for =>
        // We'll parse the expression first, then check for =>
        if (check(parser, TOK_RPAREN)) {
            // Possible ()=> 
            advance(parser); // consume )
            if (check(parser, TOK_ARROW)) {
                advance(parser); // consume =>
                ASTNode* n = ast_new_node(AST_FUNCTION);
                n->as.function.is_arrow = true;
                n->as.function.name = NULL;
                n->as.function.param_count = 0;
                n->as.function.params = NULL;
                if (check(parser, TOK_LBRACE)) {
                    n->as.function.body = parse_statement(parser);
                    n->as.function.concise_body = false;
                } else {
                    n->as.function.body = parse_assignment(parser);
                    n->as.function.concise_body = true;
                }
                return n;
            }
            // Empty parens, not arrow — unusual but treat as undefined
            return ast_new_node(AST_LITERAL_UNDEFINED);
        }
        // Try parsing as expression
        ASTNode* inner = parse_expr(parser);
        consume(parser, TOK_RPAREN, "Expected ')' after grouped expression");
        // Check for arrow
        if (check(parser, TOK_ARROW)) {
            // Convert the inner expr to a param list
            // Simple case: inner is an identifier or comma-sequence of identifiers
            advance(parser); // consume =>
            // Build params from inner (simplified: only handle identifier or sequence)
            ASTNode* n = ast_new_node(AST_FUNCTION);
            n->as.function.is_arrow = true;
            n->as.function.name = NULL;
            n->as.function.params = NULL;
            n->as.function.param_count = 0;
            // Extract param names from inner expression
            ASTNode** param_exprs = NULL;
            int pexpr_count = 0;
            if (inner->type == AST_SEQUENCE) {
                param_exprs = inner->as.sequence.exprs;
                pexpr_count = inner->as.sequence.count;
            } else {
                param_exprs = &inner;
                pexpr_count = 1;
            }
            n->as.function.params = calloc(pexpr_count, sizeof(ASTParam));
            n->as.function.param_count = pexpr_count;
            for (int i = 0; i < pexpr_count; i++) {
                ASTNode* pe = param_exprs[i];
                if (pe && pe->type == AST_IDENTIFIER) {
                    n->as.function.params[i].name = strdup(pe->as.identifier.name);
                } else if (pe && pe->type == AST_ASSIGN) {
                    // default param: x = default_val
                    if (pe->as.assign.target->type == AST_IDENTIFIER) {
                        n->as.function.params[i].name = strdup(pe->as.assign.target->as.identifier.name);
                        n->as.function.params[i].default_val = pe->as.assign.value;
                        pe->as.assign.value = NULL; // transfer ownership
                    }
                } else if (pe && pe->type == AST_SPREAD) {
                    // rest: ...name
                    ASTNode* rest_inner = pe->as.spread_expr;
                    if (rest_inner && rest_inner->type == AST_IDENTIFIER) {
                        n->as.function.params[i].name = strdup(rest_inner->as.identifier.name);
                        n->as.function.params[i].is_rest = true;
                    }
                } else {
                    n->as.function.params[i].name = strdup("_p");
                }
            }
            ast_free_node(inner);
            if (check(parser, TOK_LBRACE)) {
                n->as.function.body = parse_statement(parser);
                n->as.function.concise_body = false;
            } else {
                n->as.function.body = parse_assignment(parser);
                n->as.function.concise_body = true;
            }
            return n;
        }
        return inner;
    }
    // Array literal
    if (match(parser, TOK_LBRACKET)) {
        ASTNode* n = ast_new_node(AST_ARRAY);
        n->as.array.count = 0;
        n->as.array.elements = NULL;
        while (!check(parser, TOK_RBRACKET) && !check(parser, TOK_EOF)) {
            ASTNode* elem = NULL;
            if (check(parser, TOK_COMMA)) {
                // elision
            } else if (match(parser, TOK_SPREAD)) {
                ASTNode* sp = ast_new_node(AST_SPREAD);
                sp->as.spread_expr = parse_assignment(parser);
                elem = sp;
            } else {
                elem = parse_assignment(parser);
            }
            n->as.array.elements = realloc(n->as.array.elements, (n->as.array.count+1)*sizeof(ASTNode*));
            n->as.array.elements[n->as.array.count++] = elem;
            if (!match(parser, TOK_COMMA)) break;
        }
        consume(parser, TOK_RBRACKET, "Expected ']' after array literal");
        return n;
    }
    // Object literal
    if (match(parser, TOK_LBRACE)) {
        ASTNode* n = ast_new_node(AST_OBJECT);
        n->as.object.count = 0;
        n->as.object.keys = NULL;
        n->as.object.key_exprs = NULL;
        n->as.object.values = NULL;
        n->as.object.prop_flags = NULL;
        while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
            int flags = 0;
            char* key = NULL;
            ASTNode* key_expr = NULL;
            ASTNode* value = NULL;
            if (match(parser, TOK_SPREAD)) {
                flags |= (1<<4); // spread
                value = parse_assignment(parser);
            } else if (match(parser, TOK_LBRACKET)) {
                // computed key
                flags |= 1; // is_computed
                key_expr = parse_assignment(parser);
                consume(parser, TOK_RBRACKET, "Expected ']' after computed key");
                consume(parser, TOK_COLON, "Expected ':' after computed key");
                value = parse_assignment(parser);
            } else {
                // identifier or string or number key
                // Detect getter/setter
                bool is_get = false, is_set = false;
                if ((check(parser, TOK_GET) || check(parser, TOK_SET)) &&
                    parser->lexer.source[parser->lexer.cursor] != '(') {
                    // might be getter/setter
                    is_get = check(parser, TOK_GET);
                    is_set = check(parser, TOK_SET);
                    advance(parser); // consume get/set
                    // If next is ( or =, it's just a shorthand property named "get"/"set"
                    if (check(parser, TOK_LPAREN) || check(parser, TOK_COMMA) ||
                        check(parser, TOK_RBRACE) || check(parser, TOK_ASSIGN)) {
                        key = is_get ? strdup("get") : strdup("set");
                        is_get = false; is_set = false;
                    } else {
                        // It really is a getter/setter
                        if (is_get) flags |= (1<<2);
                        if (is_set) flags |= (1<<3);
                    }
                }
                if (!key) {
                    if (check(parser, TOK_IDENTIFIER) || check(parser, TOK_STRING) ||
                        check(parser, TOK_NUMBER) ||
                        (parser->current.type >= TOK_LET && parser->current.type <= TOK_DEBUGGER)) {
                        advance(parser);
                        key = strndup(parser->previous.start, parser->previous.length);
                    } else {
                        // unknown key type — skip
                        advance(parser);
                        key = strdup("__unknown__");
                    }
                }
                if (is_get || is_set) {
                    // getter/setter: key() { body }
                    consume(parser, TOK_LPAREN, "Expected '(' in getter/setter");
                    ASTParam* params = NULL; int pc = 0;
                    if (is_set) parse_param_list(parser, &params, &pc);
                    consume(parser, TOK_RPAREN, "Expected ')' in getter/setter");
                    ASTNode* body = parse_statement(parser);
                    ASTNode* fn = ast_new_node(AST_FUNCTION);
                    fn->as.function.name = strdup(key);
                    fn->as.function.params = params;
                    fn->as.function.param_count = pc;
                    fn->as.function.body = body;
                    value = fn;
                } else if (match(parser, TOK_COLON)) {
                    value = parse_assignment(parser);
                } else if (match(parser, TOK_LPAREN)) {
                    // method shorthand: key(...) { }
                    ASTParam* params = NULL; int pc = 0;
                    parse_param_list(parser, &params, &pc);
                    consume(parser, TOK_RPAREN, "Expected ')'");
                    ASTNode* body = parse_statement(parser);
                    ASTNode* fn = ast_new_node(AST_FUNCTION);
                    fn->as.function.params = params;
                    fn->as.function.param_count = pc;
                    fn->as.function.body = body;
                    fn->as.function.name = strdup(key);
                    value = fn;
                    flags |= (1<<1); // shorthand method
                } else {
                    // shorthand property: { x } => { x: x }
                    flags |= (1<<1);
                    ASTNode* id = ast_new_node(AST_IDENTIFIER);
                    id->as.identifier.name = strdup(key);
                    value = id;
                }
            }
            n->as.object.count++;
            n->as.object.keys = realloc(n->as.object.keys, n->as.object.count * sizeof(char*));
            n->as.object.key_exprs = realloc(n->as.object.key_exprs, n->as.object.count * sizeof(ASTNode*));
            n->as.object.values = realloc(n->as.object.values, n->as.object.count * sizeof(ASTNode*));
            n->as.object.prop_flags = realloc(n->as.object.prop_flags, n->as.object.count * sizeof(int));
            n->as.object.keys[n->as.object.count-1] = key;
            n->as.object.key_exprs[n->as.object.count-1] = key_expr;
            n->as.object.values[n->as.object.count-1] = value;
            n->as.object.prop_flags[n->as.object.count-1] = flags;
            if (!match(parser, TOK_COMMA)) break;
        }
        consume(parser, TOK_RBRACE, "Expected '}' at end of object literal");
        return n;
    }
    // async function / async arrow
    if (match(parser, TOK_ASYNC)) {
        bool is_gen = false;
        if (match(parser, TOK_FUNCTION)) {
            if (match(parser, TOK_STAR)) is_gen = true;
            ASTNode* n = ast_new_node(AST_FUNCTION);
            n->as.function.is_async = true;
            n->as.function.is_generator = is_gen;
            if (check(parser, TOK_IDENTIFIER)) {
                n->as.function.name = strndup(parser->current.start, parser->current.length);
                advance(parser);
            }
            consume(parser, TOK_LPAREN, "Expected '('");
            parse_param_list(parser, &n->as.function.params, &n->as.function.param_count);
            consume(parser, TOK_RPAREN, "Expected ')'");
            n->as.function.body = parse_statement(parser);
            return n;
        }
        // async arrow: async x => ... or async (params) => ...
        if (check(parser, TOK_IDENTIFIER) || check(parser, TOK_LPAREN)) {
            ASTNode* n = ast_new_node(AST_FUNCTION);
            n->as.function.is_async = true;
            n->as.function.is_arrow = true;
            if (match(parser, TOK_LPAREN)) {
                parse_param_list(parser, &n->as.function.params, &n->as.function.param_count);
                consume(parser, TOK_RPAREN, "Expected ')'");
            } else {
                n->as.function.params = calloc(1, sizeof(ASTParam));
                n->as.function.param_count = 1;
                n->as.function.params[0].name = strndup(parser->current.start, parser->current.length);
                advance(parser);
            }
            consume(parser, TOK_ARROW, "Expected '=>' after async arrow params");
            if (check(parser, TOK_LBRACE)) {
                n->as.function.body = parse_statement(parser);
                n->as.function.concise_body = false;
            } else {
                n->as.function.body = parse_assignment(parser);
                n->as.function.concise_body = true;
            }
            return n;
        }
        // standalone "async" identifier
        ASTNode* n = ast_new_node(AST_IDENTIFIER);
        n->as.identifier.name = strdup("async");
        return n;
    }
    // function expression (possibly generator)
    if (match(parser, TOK_FUNCTION)) {
        bool is_gen = match(parser, TOK_STAR);
        ASTNode* n = ast_new_node(AST_FUNCTION);
        n->as.function.is_generator = is_gen;
        if (check(parser, TOK_IDENTIFIER)) {
            n->as.function.name = strndup(parser->current.start, parser->current.length);
            advance(parser);
        }
        consume(parser, TOK_LPAREN, "Expected '(' after function");
        parse_param_list(parser, &n->as.function.params, &n->as.function.param_count);
        consume(parser, TOK_RPAREN, "Expected ')'");
        n->as.function.body = parse_statement(parser);
        return n;
    }
    // class expression
    if (match(parser, TOK_CLASS)) {
        ASTNode* n = ast_new_node(AST_CLASS);
        n->as.class_decl.name = NULL;
        n->as.class_decl.superclass = NULL;
        n->as.class_decl.methods = NULL;
        n->as.class_decl.method_count = 0;
        if (check(parser, TOK_IDENTIFIER)) {
            n->as.class_decl.name = strndup(parser->current.start, parser->current.length);
            advance(parser);
        }
        if (match(parser, TOK_EXTENDS)) {
            n->as.class_decl.superclass = parse_assignment(parser);
        }
        consume(parser, TOK_LBRACE, "Expected '{' after class");
        while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
            if (match(parser, TOK_SEMICOLON)) continue;
            ASTClassMethod* m = calloc(1, sizeof(ASTClassMethod));
            ASTNode* fn_node = ast_new_node(AST_FUNCTION);
            m->func_node = fn_node;
            if (match(parser, TOK_STATIC)) m->is_static = true;
            if (match(parser, TOK_ASYNC))  m->is_async  = true;
            if (match(parser, TOK_STAR))   m->is_generator = true;
            bool is_get_set = false;
            if (check(parser, TOK_GET) || check(parser, TOK_SET)) {
                const char* gs = parser->current.start;
                int gs_len = parser->current.length;
                advance(parser);
                if (!check(parser, TOK_LPAREN)) {
                    m->is_getter = (gs_len == 3 && memcmp(gs, "get", 3) == 0);
                    m->is_setter = (gs_len == 3 && memcmp(gs, "set", 3) == 0);
                    is_get_set = true;
                } else {
                    // "get"/"set" is the method name
                    m->name = strndup(gs, gs_len);
                }
            }
            if (!m->name) {
                if (match(parser, TOK_LBRACKET)) {
                    m->is_computed = true;
                    m->name_expr = parse_assignment(parser);
                    consume(parser, TOK_RBRACKET, "Expected ']'");
                } else if (check(parser, TOK_IDENTIFIER) || check(parser, TOK_STRING) ||
                           check(parser, TOK_NUMBER) ||
                           (parser->current.type >= TOK_LET && parser->current.type <= TOK_DEBUGGER)) {
                    m->name = strndup(parser->current.start, parser->current.length);
                    advance(parser);
                }
            }
            // Check for field (no parens) — just skip for now
            consume(parser, TOK_LPAREN, "Expected '(' in class method");
            parse_param_list(parser, &fn_node->as.function.params, &fn_node->as.function.param_count);
            consume(parser, TOK_RPAREN, "Expected ')'");
            fn_node->as.function.body = parse_statement(parser);
            fn_node->as.function.is_async = m->is_async;
            fn_node->as.function.is_generator = m->is_generator;
            fn_node->as.function.name = m->name ? strdup(m->name) : NULL;
            n->as.class_decl.methods = realloc(n->as.class_decl.methods, (n->as.class_decl.method_count+1)*sizeof(ASTClassMethod*));
            n->as.class_decl.methods[n->as.class_decl.method_count++] = m;
            (void)is_get_set;
        }
        consume(parser, TOK_RBRACE, "Expected '}' after class body");
        return n;
    }
    // yield expression (inside generators)
    if (match(parser, TOK_YIELD)) {
        ASTNode* n = ast_new_node(AST_YIELD);
        n->as.yield_expr.is_delegate = match(parser, TOK_STAR);
        if (!check(parser, TOK_SEMICOLON) && !check(parser, TOK_RBRACE) &&
            !check(parser, TOK_EOF) && !check(parser, TOK_RPAREN) &&
            !check(parser, TOK_RBRACKET) && !check(parser, TOK_COMMA)) {
            n->as.yield_expr.expr = parse_assignment(parser);
        }
        return n;
    }

    // Spread in argument context (already handled in call parsing, but cover here)
    if (match(parser, TOK_SPREAD)) {
        ASTNode* n = ast_new_node(AST_SPREAD);
        n->as.spread_expr = parse_assignment(parser);
        return n;
    }

    fprintf(stderr, "Parser Error: Expected expression, got '%.*s'\n",
            parser->current.length, parser->current.start);
    parser->has_error = true;
    advance(parser);
    return NULL;
}

// ─── Call / prop access chain ─────────────────────────────────────────────────
static ASTNode* parse_call_or_prop(Parser* parser) {
    if (match(parser, TOK_NEW)) {
        ASTNode* callee = parse_primary(parser);
        // Handle property chains
        while (check(parser, TOK_DOT)) {
            advance(parser);
            if (parser->current.type == TOK_IDENTIFIER ||
                (parser->current.type >= TOK_LET && parser->current.type <= TOK_DEBUGGER)) {
                advance(parser);
            }
            ASTNode* prop = ast_new_node(AST_LITERAL_STRING);
            prop->as.string.value = strndup(parser->previous.start, parser->previous.length);
            prop->as.string.length = parser->previous.length;
            ASTNode* access = ast_new_node(AST_PROP_ACCESS);
            access->as.prop_access.obj = callee;
            access->as.prop_access.prop = prop;
            access->as.prop_access.is_computed = false;
            callee = access;
        }
        ASTNode* n = ast_new_node(AST_NEW_CALL);
        n->as.call.callee = callee;
        n->as.call.arg_count = 0;
        n->as.call.args = NULL;
        if (match(parser, TOK_LPAREN)) {
            while (!check(parser, TOK_RPAREN) && !check(parser, TOK_EOF)) {
                ASTNode* arg;
                if (match(parser, TOK_SPREAD)) {
                    ASTNode* sp = ast_new_node(AST_SPREAD);
                    sp->as.spread_expr = parse_assignment(parser);
                    arg = sp;
                } else {
                    arg = parse_assignment(parser);
                }
                n->as.call.args = realloc(n->as.call.args, (n->as.call.arg_count+1)*sizeof(ASTNode*));
                n->as.call.args[n->as.call.arg_count++] = arg;
                if (!match(parser, TOK_COMMA)) break;
            }
            consume(parser, TOK_RPAREN, "Expected ')' after arguments");
        }
        return n;
    }

    ASTNode* expr = parse_primary(parser);
    while (1) {
        if (match(parser, TOK_LPAREN)) {
            ASTNode* n = ast_new_node(AST_CALL);
            n->as.call.callee = expr;
            n->as.call.arg_count = 0;
            n->as.call.args = NULL;
            while (!check(parser, TOK_RPAREN) && !check(parser, TOK_EOF)) {
                ASTNode* arg;
                if (match(parser, TOK_SPREAD)) {
                    ASTNode* sp = ast_new_node(AST_SPREAD);
                    sp->as.spread_expr = parse_assignment(parser);
                    arg = sp;
                } else {
                    arg = parse_assignment(parser);
                }
                n->as.call.args = realloc(n->as.call.args, (n->as.call.arg_count+1)*sizeof(ASTNode*));
                n->as.call.args[n->as.call.arg_count++] = arg;
                if (!match(parser, TOK_COMMA)) break;
            }
            consume(parser, TOK_RPAREN, "Expected ')' after arguments");
            expr = n;
        } else if (match(parser, TOK_DOT)) {
            if (parser->current.type == TOK_IDENTIFIER ||
                (parser->current.type >= TOK_LET && parser->current.type <= TOK_DEBUGGER)) {
                advance(parser);
            } else {
                fprintf(stderr, "Parser Error: Expected property name after '.'\n");
                parser->has_error = true;
            }
            ASTNode* prop = ast_new_node(AST_LITERAL_STRING);
            prop->as.string.value = strndup(parser->previous.start, parser->previous.length);
            prop->as.string.length = parser->previous.length;
            ASTNode* n = ast_new_node(AST_PROP_ACCESS);
            n->as.prop_access.obj = expr;
            n->as.prop_access.prop = prop;
            n->as.prop_access.is_computed = false;
            expr = n;
        } else if (match(parser, TOK_OPTIONAL_CHAIN)) {
            // ?.
            ASTNode* prop;
            bool is_computed = false;
            bool is_call = false;
            if (match(parser, TOK_LBRACKET)) {
                prop = parse_expr(parser);
                consume(parser, TOK_RBRACKET, "Expected ']'");
                is_computed = true;
            } else if (match(parser, TOK_LPAREN)) {
                // Optional call: fn?.()
                ASTNode* n = ast_new_node(AST_CALL);
                n->as.call.callee = expr;
                n->as.call.arg_count = 0;
                n->as.call.args = NULL;
                while (!check(parser, TOK_RPAREN) && !check(parser, TOK_EOF)) {
                    ASTNode* arg;
                    if (match(parser, TOK_SPREAD)) { ASTNode* sp = ast_new_node(AST_SPREAD); sp->as.spread_expr = parse_assignment(parser); arg = sp; }
                    else arg = parse_assignment(parser);
                    n->as.call.args = realloc(n->as.call.args, (n->as.call.arg_count+1)*sizeof(ASTNode*));
                    n->as.call.args[n->as.call.arg_count++] = arg;
                    if (!match(parser, TOK_COMMA)) break;
                }
                consume(parser, TOK_RPAREN, "Expected ')'");
                // Wrap in optional chain node
                ASTNode* opt = ast_new_node(AST_OPTIONAL_CHAIN);
                opt->as.prop_access.obj = n;
                opt->as.prop_access.prop = NULL;
                expr = opt;
                continue;
            } else {
                if (parser->current.type == TOK_IDENTIFIER ||
                    (parser->current.type >= TOK_LET && parser->current.type <= TOK_DEBUGGER)) {
                    advance(parser);
                }
                prop = ast_new_node(AST_LITERAL_STRING);
                prop->as.string.value = strndup(parser->previous.start, parser->previous.length);
                prop->as.string.length = parser->previous.length;
            }
            ASTNode* n = ast_new_node(AST_OPTIONAL_CHAIN);
            n->as.prop_access.obj = expr;
            n->as.prop_access.prop = prop;
            n->as.prop_access.is_computed = is_computed;
            n->as.prop_access.is_optional = true;
            expr = n;
            (void)is_call;
        } else if (match(parser, TOK_LBRACKET)) {
            ASTNode* prop = parse_expr(parser);
            consume(parser, TOK_RBRACKET, "Expected ']' after index");
            ASTNode* n = ast_new_node(AST_PROP_ACCESS);
            n->as.prop_access.obj = expr;
            n->as.prop_access.prop = prop;
            n->as.prop_access.is_computed = true;
            expr = n;
        } else {
            break;
        }
    }
    return expr;
}

// ─── Postfix ++ / -- ─────────────────────────────────────────────────────────
static ASTNode* parse_postfix(Parser* parser) {
    ASTNode* expr = parse_call_or_prop(parser);
    if (match(parser, TOK_PLUSPLUS) || match(parser, TOK_MINUSMINUS)) {
        ASTNode* n = ast_new_node(AST_POSTFIX);
        n->as.postfix.op = parser->previous.type;
        n->as.postfix.expr = expr;
        return n;
    }
    return expr;
}

// ─── Unary ────────────────────────────────────────────────────────────────────
static ASTNode* parse_unary(Parser* parser) {
    // Prefix ++/--
    if (match(parser, TOK_PLUSPLUS) || match(parser, TOK_MINUSMINUS)) {
        int op = parser->previous.type;
        ASTNode* n = ast_new_node(AST_UNARY);
        n->as.unary.op = op;
        n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser);
        return n;
    }
    if (match(parser, TOK_MINUS)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_MINUS; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_NOT)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_NOT; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_TILDE)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_TILDE; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_TYPEOF)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_TYPEOF; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_VOID)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_VOID; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_DELETE)) {
        ASTNode* n = ast_new_node(AST_UNARY); n->as.unary.op = TOK_DELETE; n->as.unary.is_prefix = true;
        n->as.unary.expr = parse_unary(parser); return n;
    }
    if (match(parser, TOK_AWAIT)) {
        ASTNode* n = ast_new_node(AST_AWAIT);
        n->as.await_expr.expr = parse_unary(parser);
        return n;
    }
    return parse_postfix(parser);
}

// ─── Binary expression levels ─────────────────────────────────────────────────
static ASTNode* parse_exponent(Parser* parser) {
    ASTNode* expr = parse_unary(parser);
    if (match(parser, TOK_STARSTAR)) {
        // Right-associative
        ASTNode* right = parse_exponent(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = TOK_STARSTAR;
        n->as.binary.left = expr;
        n->as.binary.right = right;
        return n;
    }
    return expr;
}

static ASTNode* parse_multiplicative(Parser* parser) {
    ASTNode* expr = parse_exponent(parser);
    while (match(parser, TOK_STAR) || match(parser, TOK_SLASH) || match(parser, TOK_PERCENT)) {
        int op = parser->previous.type;
        ASTNode* right = parse_exponent(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_additive(Parser* parser) {
    ASTNode* expr = parse_multiplicative(parser);
    while (match(parser, TOK_PLUS) || match(parser, TOK_MINUS)) {
        int op = parser->previous.type;
        ASTNode* right = parse_multiplicative(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_shift(Parser* parser) {
    ASTNode* expr = parse_additive(parser);
    while (match(parser, TOK_SHL) || match(parser, TOK_SHR) || match(parser, TOK_USHR)) {
        int op = parser->previous.type;
        ASTNode* right = parse_additive(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_relational(Parser* parser) {
    ASTNode* expr = parse_shift(parser);
    while (match(parser, TOK_LT) || match(parser, TOK_LE) || match(parser, TOK_GT) ||
           match(parser, TOK_GE) || match(parser, TOK_IN) || match(parser, TOK_INSTANCEOF)) {
        int op = parser->previous.type;
        ASTNode* right = parse_shift(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_equality(Parser* parser) {
    ASTNode* expr = parse_relational(parser);
    while (match(parser, TOK_EQ) || match(parser, TOK_NE) ||
           match(parser, TOK_EQ_LOOSE) || match(parser, TOK_NE_LOOSE)) {
        int op = parser->previous.type;
        ASTNode* right = parse_relational(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_bitwise_and(Parser* parser) {
    ASTNode* expr = parse_equality(parser);
    while (match(parser, TOK_AMPERSAND)) {
        ASTNode* right = parse_equality(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = TOK_AMPERSAND; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_bitwise_xor(Parser* parser) {
    ASTNode* expr = parse_bitwise_and(parser);
    while (match(parser, TOK_CARET)) {
        ASTNode* right = parse_bitwise_and(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = TOK_CARET; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_bitwise_or(Parser* parser) {
    ASTNode* expr = parse_bitwise_xor(parser);
    while (match(parser, TOK_PIPE)) {
        ASTNode* right = parse_bitwise_xor(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = TOK_PIPE; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_logical_and(Parser* parser) {
    ASTNode* expr = parse_bitwise_or(parser);
    while (match(parser, TOK_AND)) {
        ASTNode* right = parse_bitwise_or(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = TOK_AND; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_logical_or(Parser* parser) {
    ASTNode* expr = parse_logical_and(parser);
    while (match(parser, TOK_OR) || match(parser, TOK_NULLISH)) {
        int op = parser->previous.type;
        ASTNode* right = parse_logical_and(parser);
        ASTNode* n = ast_new_node(AST_BINARY);
        n->as.binary.op = op; n->as.binary.left = expr; n->as.binary.right = right;
        expr = n;
    }
    return expr;
}

static ASTNode* parse_ternary(Parser* parser) {
    ASTNode* cond = parse_logical_or(parser);
    if (match(parser, TOK_QUESTION)) {
        ASTNode* then_expr = parse_assignment(parser);
        consume(parser, TOK_COLON, "Expected ':' in ternary expression");
        ASTNode* else_expr = parse_ternary(parser);
        ASTNode* n = ast_new_node(AST_TERNARY);
        n->as.ternary.cond = cond;
        n->as.ternary.then_expr = then_expr;
        n->as.ternary.else_expr = else_expr;
        return n;
    }
    return cond;
}

static bool is_compound_assign(TokenType t) {
    return t == TOK_PLUS_ASSIGN || t == TOK_MINUS_ASSIGN || t == TOK_STAR_ASSIGN ||
           t == TOK_SLASH_ASSIGN || t == TOK_PERCENT_ASSIGN || t == TOK_STARSTAR_ASSIGN ||
           t == TOK_AND_ASSIGN || t == TOK_OR_ASSIGN || t == TOK_NULLISH_ASSIGN ||
           t == TOK_BITAND_ASSIGN || t == TOK_BITOR_ASSIGN || t == TOK_BITXOR_ASSIGN ||
           t == TOK_SHL_ASSIGN || t == TOK_SHR_ASSIGN || t == TOK_USHR_ASSIGN;
}

static ASTNode* parse_assignment(Parser* parser) {
    ASTNode* expr = parse_ternary(parser);
    if (match(parser, TOK_ASSIGN)) {
        ASTNode* value = parse_assignment(parser);
        ASTNode* n = ast_new_node(AST_ASSIGN);
        n->as.assign.target = expr;
        n->as.assign.value = value;
        n->as.assign.compound_op = 0;
        return n;
    }
    if (is_compound_assign(parser->current.type)) {
        int op = parser->current.type;
        advance(parser);
        ASTNode* value = parse_assignment(parser);
        ASTNode* n = ast_new_node(AST_ASSIGN);
        n->as.assign.target = expr;
        n->as.assign.value = value;
        n->as.assign.compound_op = op;
        return n;
    }
    return expr;
}

static ASTNode* parse_expr(Parser* parser) {
    ASTNode* expr = parse_assignment(parser);
    // Comma operator — produce a sequence
    if (check(parser, TOK_COMMA)) {
        ASTNode* seq = ast_new_node(AST_SEQUENCE);
        seq->as.sequence.exprs = malloc(sizeof(ASTNode*));
        seq->as.sequence.exprs[0] = expr;
        seq->as.sequence.count = 1;
        while (match(parser, TOK_COMMA)) {
            ASTNode* next = parse_assignment(parser);
            seq->as.sequence.exprs = realloc(seq->as.sequence.exprs, (seq->as.sequence.count+1)*sizeof(ASTNode*));
            seq->as.sequence.exprs[seq->as.sequence.count++] = next;
        }
        return seq;
    }
    return expr;
}

// ─── Statement parsing ────────────────────────────────────────────────────────
static ASTNode* parse_statement(Parser* parser) {
    // Empty statement
    if (match(parser, TOK_SEMICOLON)) return ast_new_node(AST_EMPTY);
    // Debugger
    if (match(parser, TOK_DEBUGGER)) { match(parser, TOK_SEMICOLON); return ast_new_node(AST_EMPTY); }

    // import
    if (match(parser, TOK_IMPORT)) {
        bool is_json_import = false;
        char* default_import_name = NULL;
        int count = 0;
        char** locals = NULL;
        char** exports = NULL;

        if (check(parser, TOK_IDENTIFIER)) {
            default_import_name = strndup(parser->current.start, parser->current.length);
            advance(parser);
            if (match(parser, TOK_COMMA)) consume(parser, TOK_LBRACE, "Expected '{' after default import");
        } else if (match(parser, TOK_LBRACE)) {
            // just {
        } else if (match(parser, TOK_STAR)) {
            // import * as name from 'mod'
            consume(parser, TOK_AS, "Expected 'as'");
            consume(parser, TOK_IDENTIFIER, "Expected identifier after 'as'");
            default_import_name = strndup(parser->previous.start, parser->previous.length);
            goto import_from;
        } else {
            fprintf(stderr, "Parser Error: Expected identifier or '{' after import\n");
            parser->has_error = true;
        }

        if (parser->previous.type == TOK_LBRACE) {
            if (!check(parser, TOK_RBRACE)) {
                do {
                    consume(parser, TOK_IDENTIFIER, "Expected identifier in import list");
                    char* ename = strndup(parser->previous.start, parser->previous.length);
                    char* lname = ename;
                    if (match(parser, TOK_AS)) {
                        consume(parser, TOK_IDENTIFIER, "Expected identifier after 'as'");
                        lname = strndup(parser->previous.start, parser->previous.length);
                    } else { lname = strdup(ename); }
                    count++;
                    locals  = realloc(locals,  count * sizeof(char*));
                    exports = realloc(exports, count * sizeof(char*));
                    locals[count-1]  = lname;
                    exports[count-1] = ename;
                } while (match(parser, TOK_COMMA));
            }
            consume(parser, TOK_RBRACE, "Expected '}'");
        }

        import_from:
        consume(parser, TOK_FROM, "Expected 'from'");
        consume(parser, TOK_STRING, "Expected module path string");
        char* path = parser->previous.template_str
            ? strndup(parser->previous.template_str, parser->previous.template_str_len)
            : strndup(parser->previous.start, parser->previous.length);

        if (match(parser, TOK_WITH)) {
            consume(parser, TOK_LBRACE, "Expected '{'");
            consume(parser, TOK_IDENTIFIER, "Expected 'type'");
            consume(parser, TOK_COLON, "Expected ':'");
            consume(parser, TOK_STRING, "Expected string");
            if (parser->previous.length == 4 && memcmp(parser->previous.start, "json", 4) == 0)
                is_json_import = true;
            consume(parser, TOK_RBRACE, "Expected '}'");
        }
        match(parser, TOK_SEMICOLON);

        if (is_json_import) {
            ASTNode* decl = ast_new_node(AST_VAR_DECL);
            decl->as.var_decl.name = default_import_name ? default_import_name : strdup("__unused_json");
            decl->as.var_decl.is_const = true;
            ASTNode* call = ast_new_node(AST_CALL);
            ASTNode* callee = ast_new_node(AST_IDENTIFIER);
            callee->as.identifier.name = strdup("import_module");
            call->as.call.callee = callee;
            call->as.call.arg_count = 2;
            call->as.call.args = malloc(2 * sizeof(ASTNode*));
            ASTNode* pa = ast_new_node(AST_LITERAL_STRING);
            pa->as.string.value = path; pa->as.string.length = (int)strlen(path);
            call->as.call.args[0] = pa;
            ASTNode* ta = ast_new_node(AST_LITERAL_STRING);
            ta->as.string.value = strdup("json"); ta->as.string.length = 4;
            call->as.call.args[1] = ta;
            decl->as.var_decl.init = call;
            if (locals) free(locals); if (exports) free(exports);
            return decl;
        }

        int stmt_count = 1 + count + (default_import_name ? 1 : 0);
        ASTNode* block = ast_new_node(AST_BLOCK);
        block->as.block.count = stmt_count;
        block->as.block.statements = malloc(stmt_count * sizeof(ASTNode*));
        block->as.block.is_inline = true;

        ASTNode* req_decl = ast_new_node(AST_VAR_DECL);
        req_decl->as.var_decl.name = strdup("__tmp");
        req_decl->as.var_decl.is_const = true;
        ASTNode* call = ast_new_node(AST_CALL);
        ASTNode* callee = ast_new_node(AST_IDENTIFIER);
        callee->as.identifier.name = strdup("require");
        call->as.call.callee = callee;
        call->as.call.arg_count = 1;
        call->as.call.args = malloc(sizeof(ASTNode*));
        ASTNode* pa = ast_new_node(AST_LITERAL_STRING);
        pa->as.string.value = path; pa->as.string.length = (int)strlen(path);
        call->as.call.args[0] = pa;
        ASTNode* await_req = ast_new_node(AST_AWAIT);
        await_req->as.await_expr.expr = call;
        req_decl->as.var_decl.init = await_req;
        block->as.block.statements[0] = req_decl;

        int s_idx = 1;
        if (default_import_name) {
            ASTNode* decl = ast_new_node(AST_VAR_DECL);
            decl->as.var_decl.name = default_import_name;
            decl->as.var_decl.is_const = true;
            ASTNode* acc = ast_new_node(AST_PROP_ACCESS);
            ASTNode* tmp = ast_new_node(AST_IDENTIFIER); tmp->as.identifier.name = strdup("__tmp");
            acc->as.prop_access.obj = tmp;
            ASTNode* prop = ast_new_node(AST_LITERAL_STRING);
            prop->as.string.value = strdup("default"); prop->as.string.length = 7;
            acc->as.prop_access.prop = prop;
            decl->as.var_decl.init = acc;
            block->as.block.statements[s_idx++] = decl;
        }
        for (int i = 0; i < count; i++) {
            ASTNode* decl = ast_new_node(AST_VAR_DECL);
            decl->as.var_decl.name = locals[i];
            decl->as.var_decl.is_const = true;
            ASTNode* acc = ast_new_node(AST_PROP_ACCESS);
            ASTNode* tmp = ast_new_node(AST_IDENTIFIER); tmp->as.identifier.name = strdup("__tmp");
            acc->as.prop_access.obj = tmp;
            ASTNode* prop = ast_new_node(AST_LITERAL_STRING);
            prop->as.string.value = exports[i]; prop->as.string.length = (int)strlen(exports[i]);
            acc->as.prop_access.prop = prop;
            decl->as.var_decl.init = acc;
            block->as.block.statements[s_idx++] = decl;
        }
        if (locals) free(locals); if (exports) free(exports);
        block->as.block.is_inline = true;
        return block;
    }

    // export
    if (match(parser, TOK_EXPORT)) {
        if (match(parser, TOK_DEFAULT)) {
            ASTNode* expr = parse_expr(parser);
            match(parser, TOK_SEMICOLON);
            ASTNode* n = ast_new_node(AST_EXPR_STMT);
            ASTNode* assign = ast_new_node(AST_ASSIGN);
            ASTNode* eobj = ast_new_node(AST_IDENTIFIER); eobj->as.identifier.name = strdup("exports");
            ASTNode* prop = ast_new_node(AST_LITERAL_STRING); prop->as.string.value = strdup("default"); prop->as.string.length = 7;
            ASTNode* acc = ast_new_node(AST_PROP_ACCESS); acc->as.prop_access.obj = eobj; acc->as.prop_access.prop = prop;
            assign->as.assign.target = acc; assign->as.assign.value = expr;
            n->as.expr_stmt = assign;
            return n;
        } else if (match(parser, TOK_STAR)) {
            // export * from 'mod' — skip
            match(parser, TOK_FROM);
            if (check(parser, TOK_STRING)) advance(parser);
            match(parser, TOK_SEMICOLON);
            return ast_new_node(AST_EMPTY);
        } else if (match(parser, TOK_LBRACE)) {
            ASTNode* block = ast_new_node(AST_BLOCK);
            block->as.block.count = 0; block->as.block.statements = NULL; block->as.block.is_inline = true;
            if (!check(parser, TOK_RBRACE)) {
                do {
                    consume(parser, TOK_IDENTIFIER, "Expected identifier in export list");
                    char* lname = strndup(parser->previous.start, parser->previous.length);
                    char* ename = lname;
                    if (match(parser, TOK_AS)) { consume(parser, TOK_IDENTIFIER, "Expected identifier"); ename = strndup(parser->previous.start, parser->previous.length); }
                    else { ename = strdup(lname); }
                    ASTNode* eobj = ast_new_node(AST_IDENTIFIER); eobj->as.identifier.name = strdup("exports");
                    ASTNode* prop = ast_new_node(AST_LITERAL_STRING); prop->as.string.value = ename; prop->as.string.length = (int)strlen(ename);
                    ASTNode* acc = ast_new_node(AST_PROP_ACCESS); acc->as.prop_access.obj = eobj; acc->as.prop_access.prop = prop;
                    ASTNode* val = ast_new_node(AST_IDENTIFIER); val->as.identifier.name = lname;
                    ASTNode* asgn = ast_new_node(AST_ASSIGN); asgn->as.assign.target = acc; asgn->as.assign.value = val;
                    ASTNode* stmt = ast_new_node(AST_EXPR_STMT); stmt->as.expr_stmt = asgn;
                    block->as.block.count++;
                    block->as.block.statements = realloc(block->as.block.statements, block->as.block.count * sizeof(ASTNode*));
                    block->as.block.statements[block->as.block.count-1] = stmt;
                } while (match(parser, TOK_COMMA));
            }
            consume(parser, TOK_RBRACE, "Expected '}'"); match(parser, TOK_FROM); if (check(parser, TOK_STRING)) advance(parser); match(parser, TOK_SEMICOLON);
            return block;
        } else {
            ASTNode* decl = parse_statement(parser);
            char* name = NULL;
            if (decl->type == AST_VAR_DECL) name = decl->as.var_decl.name;
            else if (decl->type == AST_FUNCTION) name = decl->as.function.name;
            else if (decl->type == AST_CLASS) name = decl->as.class_decl.name;
            ASTNode* block = ast_new_node(AST_BLOCK);
            block->as.block.count = name ? 2 : 1;
            block->as.block.statements = malloc(block->as.block.count * sizeof(ASTNode*));
            block->as.block.is_inline = true;
            block->as.block.statements[0] = decl;
            if (name) {
                ASTNode* eobj = ast_new_node(AST_IDENTIFIER); eobj->as.identifier.name = strdup("exports");
                ASTNode* prop = ast_new_node(AST_LITERAL_STRING); prop->as.string.value = strdup(name); prop->as.string.length = (int)strlen(name);
                ASTNode* acc = ast_new_node(AST_PROP_ACCESS); acc->as.prop_access.obj = eobj; acc->as.prop_access.prop = prop;
                ASTNode* val = ast_new_node(AST_IDENTIFIER); val->as.identifier.name = strdup(name);
                ASTNode* asgn = ast_new_node(AST_ASSIGN); asgn->as.assign.target = acc; asgn->as.assign.value = val;
                ASTNode* stmt = ast_new_node(AST_EXPR_STMT); stmt->as.expr_stmt = asgn;
                block->as.block.statements[1] = stmt;
            }
            return block;
        }
    }

    // Block
    if (match(parser, TOK_LBRACE)) {
        ASTNode* n = ast_new_node(AST_BLOCK);
        n->as.block.count = 0; n->as.block.statements = NULL;
        while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
            n->as.block.count++;
            n->as.block.statements = realloc(n->as.block.statements, n->as.block.count * sizeof(ASTNode*));
            n->as.block.statements[n->as.block.count-1] = parse_statement(parser);
        }
        consume(parser, TOK_RBRACE, "Expected '}'");
        return n;
    }

    // try/catch/finally
    if (match(parser, TOK_TRY)) {
        ASTNode* n = ast_new_node(AST_TRY);
        n->as.try_stmt.try_block = parse_statement(parser);
        n->as.try_stmt.catch_param = NULL;
        n->as.try_stmt.catch_block = NULL;
        n->as.try_stmt.finally_block = NULL;
        if (match(parser, TOK_CATCH)) {
            if (match(parser, TOK_LPAREN)) {
                consume(parser, TOK_IDENTIFIER, "Expected catch param");
                n->as.try_stmt.catch_param = strndup(parser->previous.start, parser->previous.length);
                consume(parser, TOK_RPAREN, "Expected ')'");
            }
            n->as.try_stmt.catch_block = parse_statement(parser);
        }
        if (match(parser, TOK_FINALLY)) n->as.try_stmt.finally_block = parse_statement(parser);
        return n;
    }

    // await using / using
    bool is_await_using = false;
    if (check(parser, TOK_AWAIT)) {
        Lexer sl = parser->lexer; Token sc = parser->current; Token sp = parser->previous;
        advance(parser);
        if (match(parser, TOK_USING)) { is_await_using = true; }
        else { parser->lexer = sl; parser->current = sc; parser->previous = sp; }
    }

    // Variable declarations (let/const/var/using)
    if (is_await_using || match(parser, TOK_LET) || match(parser, TOK_CONST) ||
        match(parser, TOK_VAR) || match(parser, TOK_USING)) {
        bool is_const = parser->previous.type == TOK_CONST;
        bool is_using = parser->previous.type == TOK_USING || is_await_using;
        ASTNode* n = ast_new_node(AST_VAR_DECL);
        n->as.var_decl.is_const = is_const;
        n->as.var_decl.is_using = is_using;
        n->as.var_decl.is_await_using = is_await_using;

        // Check for destructuring
        if (match(parser, TOK_LBRACE) || match(parser, TOK_LBRACKET)) {
            bool is_arr = (parser->previous.type == TOK_LBRACKET);
            n->as.var_decl.is_array_pattern = is_arr;
            n->as.var_decl.bindings = NULL;
            n->as.var_decl.bind_count = 0;
            n->as.var_decl.name = NULL;
            TokenType close = is_arr ? TOK_RBRACKET : TOK_RBRACE;
            while (!check(parser, close) && !check(parser, TOK_EOF)) {
                if (check(parser, TOK_COMMA) && is_arr) {
                    // elision
                    ASTBindingElem elem = {0};
                    n->as.var_decl.bindings = realloc(n->as.var_decl.bindings, (n->as.var_decl.bind_count+1)*sizeof(ASTBindingElem));
                    n->as.var_decl.bindings[n->as.var_decl.bind_count++] = elem;
                } else {
                    ASTBindingElem elem = {0};
                    if (match(parser, TOK_SPREAD)) {
                        elem.is_rest = true;
                        consume(parser, TOK_IDENTIFIER, "Expected name after '...'");
                        elem.name = strndup(parser->previous.start, parser->previous.length);
                        n->as.var_decl.bindings = realloc(n->as.var_decl.bindings, (n->as.var_decl.bind_count+1)*sizeof(ASTBindingElem));
                        n->as.var_decl.bindings[n->as.var_decl.bind_count++] = elem;
                        break;
                    }
                    if (!is_arr) {
                        // object pattern: key [: name] [= default]
                        consume(parser, TOK_IDENTIFIER, "Expected property name");
                        char* pkey = strndup(parser->previous.start, parser->previous.length);
                        if (match(parser, TOK_COLON)) {
                            elem.key = pkey;
                            consume(parser, TOK_IDENTIFIER, "Expected name");
                            elem.name = strndup(parser->previous.start, parser->previous.length);
                        } else {
                            elem.name = strdup(pkey);
                            elem.key = pkey;
                        }
                    } else {
                        consume(parser, TOK_IDENTIFIER, "Expected name");
                        elem.name = strndup(parser->previous.start, parser->previous.length);
                    }
                    if (match(parser, TOK_ASSIGN)) elem.default_val = parse_assignment(parser);
                    n->as.var_decl.bindings = realloc(n->as.var_decl.bindings, (n->as.var_decl.bind_count+1)*sizeof(ASTBindingElem));
                    n->as.var_decl.bindings[n->as.var_decl.bind_count++] = elem;
                }
                if (!match(parser, TOK_COMMA)) break;
            }
            consume(parser, close, "Expected closing bracket in destructuring");
        } else {
            consume(parser, TOK_IDENTIFIER, "Expected variable name");
            n->as.var_decl.name = strndup(parser->previous.start, parser->previous.length);
            n->as.var_decl.bind_count = 0;
        }

        if (match(parser, TOK_ASSIGN)) n->as.var_decl.init = parse_expr(parser);
        // Handle multiple declarations: let a = 1, b = 2
        // (simplified: only handle first; full implementation would need a block)
        match(parser, TOK_SEMICOLON);
        return n;
    }

    // if
    if (match(parser, TOK_IF)) {
        consume(parser, TOK_LPAREN, "Expected '(' after if");
        ASTNode* cond = parse_expr(parser);
        consume(parser, TOK_RPAREN, "Expected ')' after if condition");
        ASTNode* then_branch = parse_statement(parser);
        ASTNode* else_branch = NULL;
        if (match(parser, TOK_ELSE)) else_branch = parse_statement(parser);
        ASTNode* n = ast_new_node(AST_IF);
        n->as.if_stmt.cond = cond; n->as.if_stmt.then_branch = then_branch; n->as.if_stmt.else_branch = else_branch;
        return n;
    }

    // while
    if (match(parser, TOK_WHILE)) {
        consume(parser, TOK_LPAREN, "Expected '(' after while");
        ASTNode* cond = parse_expr(parser);
        consume(parser, TOK_RPAREN, "Expected ')' after while condition");
        ASTNode* body = parse_statement(parser);
        ASTNode* n = ast_new_node(AST_WHILE);
        n->as.while_stmt.cond = cond; n->as.while_stmt.body = body;
        return n;
    }

    // do...while
    if (match(parser, TOK_DO)) {
        ASTNode* body = parse_statement(parser);
        consume(parser, TOK_WHILE, "Expected 'while' after do body");
        consume(parser, TOK_LPAREN, "Expected '('");
        ASTNode* cond = parse_expr(parser);
        consume(parser, TOK_RPAREN, "Expected ')'");
        match(parser, TOK_SEMICOLON);
        ASTNode* n = ast_new_node(AST_DO_WHILE);
        n->as.while_stmt.cond = cond; n->as.while_stmt.body = body;
        return n;
    }

    // for / for...of / for...in
    if (match(parser, TOK_FOR)) {
        // TODO: for await (... of ...) — skip 'await' if present
        bool for_await = false;
        if (check(parser, TOK_AWAIT)) { advance(parser); for_await = true; }
        consume(parser, TOK_LPAREN, "Expected '(' after for");
        // Detect for...of / for...in
        // Save state
        bool has_decl = check(parser, TOK_LET) || check(parser, TOK_CONST) || check(parser, TOK_VAR);
        Lexer saved_lex = parser->lexer;
        Token saved_cur = parser->current;
        Token saved_prev = parser->previous;

        // If it has a decl, try: for (let/const/var name of/in ...)
        if (has_decl) {
            ASTNode* decl = parse_statement(parser);
            if (match(parser, TOK_OF) || match(parser, TOK_IN)) {
                bool is_of = (parser->previous.type == TOK_OF);
                ASTNode* iterable = parse_expr(parser);
                consume(parser, TOK_RPAREN, "Expected ')'");
                ASTNode* body = parse_statement(parser);
                if (is_of) {
                    ASTNode* n = ast_new_node(AST_FOR_OF);
                    n->as.for_of.binding_name = NULL;
                    n->as.for_of.binding_decl = NULL;
                    if (!decl->as.var_decl.is_array_pattern && decl->as.var_decl.bind_count == 0 && decl->as.var_decl.name) {
                        n->as.for_of.binding_name = strdup(decl->as.var_decl.name);
                        ast_free_node(decl);
                    } else {
                        n->as.for_of.binding_decl = decl;
                    }
                    n->as.for_of.is_const = decl ? decl->as.var_decl.is_const : false;
                    n->as.for_of.iterable = iterable;
                    n->as.for_of.body = body;
                    n->as.for_of.is_await = for_await;
                    return n;
                } else {
                    ASTNode* n = ast_new_node(AST_FOR_IN);
                    n->as.for_in.binding_name = NULL;
                    n->as.for_in.binding_decl = NULL;
                    if (!decl->as.var_decl.is_array_pattern && decl->as.var_decl.bind_count == 0 && decl->as.var_decl.name) {
                        n->as.for_in.binding_name = strdup(decl->as.var_decl.name);
                        ast_free_node(decl);
                    } else {
                        n->as.for_in.binding_decl = decl;
                    }
                    n->as.for_in.is_const = decl ? decl->as.var_decl.is_const : false;
                    n->as.for_in.object = iterable;
                    n->as.for_in.body = body;
                    return n;
                }
            }
            // Rewind
            parser->lexer = saved_lex; parser->current = saved_cur; parser->previous = saved_prev;
            ast_free_node(decl);
        }
        // C-style for loop
        ASTNode* init = NULL;
        if (!match(parser, TOK_SEMICOLON)) {
            if (has_decl) init = parse_statement(parser);
            else { init = parse_expr(parser); consume(parser, TOK_SEMICOLON, "Expected ';'"); }
        }
        ASTNode* cond = NULL;
        if (!match(parser, TOK_SEMICOLON)) {
            cond = parse_expr(parser);
            consume(parser, TOK_SEMICOLON, "Expected ';' after for condition");
        }
        ASTNode* update = NULL;
        if (!match(parser, TOK_RPAREN)) {
            update = parse_expr(parser);
            consume(parser, TOK_RPAREN, "Expected ')' after for update");
        }
        ASTNode* body = parse_statement(parser);
        ASTNode* n = ast_new_node(AST_FOR);
        n->as.for_stmt.init = init; n->as.for_stmt.cond = cond; n->as.for_stmt.update = update; n->as.for_stmt.body = body;
        return n;
    }

    // switch
    if (match(parser, TOK_SWITCH)) {
        consume(parser, TOK_LPAREN, "Expected '(' after switch");
        ASTNode* disc = parse_expr(parser);
        consume(parser, TOK_RPAREN, "Expected ')'");
        consume(parser, TOK_LBRACE, "Expected '{'");
        ASTNode* n = ast_new_node(AST_SWITCH);
        n->as.switch_stmt.discriminant = disc;
        n->as.switch_stmt.cases = NULL;
        n->as.switch_stmt.case_count = 0;
        while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
            ASTSwitchCase* cs = calloc(1, sizeof(ASTSwitchCase));
            if (match(parser, TOK_CASE)) {
                cs->test = parse_expr(parser);
                consume(parser, TOK_COLON, "Expected ':' after case");
            } else if (match(parser, TOK_DEFAULT)) {
                cs->test = NULL;
                consume(parser, TOK_COLON, "Expected ':' after default");
            } else {
                advance(parser); // skip unknown
                free(cs);
                continue;
            }
            cs->body = NULL; cs->body_count = 0;
            while (!check(parser, TOK_CASE) && !check(parser, TOK_DEFAULT) &&
                   !check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
                ASTNode* stmt = parse_statement(parser);
                if (stmt) {
                    cs->body = realloc(cs->body, (cs->body_count+1)*sizeof(ASTNode*));
                    cs->body[cs->body_count++] = stmt;
                }
                if (parser->previous.type == TOK_BREAK) break;
            }
            n->as.switch_stmt.cases = realloc(n->as.switch_stmt.cases, (n->as.switch_stmt.case_count+1)*sizeof(ASTSwitchCase*));
            n->as.switch_stmt.cases[n->as.switch_stmt.case_count++] = cs;
        }
        consume(parser, TOK_RBRACE, "Expected '}' after switch");
        return n;
    }

    // break
    if (match(parser, TOK_BREAK)) {
        ASTNode* n = ast_new_node(AST_BREAK);
        n->as.break_stmt.label = NULL;
        if (!check(parser, TOK_SEMICOLON) && !check(parser, TOK_RBRACE) && check(parser, TOK_IDENTIFIER)) {
            n->as.break_stmt.label = strndup(parser->current.start, parser->current.length);
            advance(parser);
        }
        match(parser, TOK_SEMICOLON);
        return n;
    }

    // continue
    if (match(parser, TOK_CONTINUE)) {
        ASTNode* n = ast_new_node(AST_CONTINUE);
        n->as.continue_stmt.label = NULL;
        if (!check(parser, TOK_SEMICOLON) && !check(parser, TOK_RBRACE) && check(parser, TOK_IDENTIFIER)) {
            n->as.continue_stmt.label = strndup(parser->current.start, parser->current.length);
            advance(parser);
        }
        match(parser, TOK_SEMICOLON);
        return n;
    }

    // return
    if (match(parser, TOK_RETURN)) {
        ASTNode* expr = NULL;
        if (!check(parser, TOK_SEMICOLON) && !check(parser, TOK_RBRACE) && !check(parser, TOK_EOF))
            expr = parse_expr(parser);
        match(parser, TOK_SEMICOLON);
        ASTNode* n = ast_new_node(AST_RETURN);
        n->as.return_stmt.expr = expr;
        return n;
    }

    // throw
    if (match(parser, TOK_THROW)) {
        ASTNode* expr = parse_expr(parser);
        match(parser, TOK_SEMICOLON);
        ASTNode* n = ast_new_node(AST_THROW);
        n->as.throw_stmt.throw_stmt = expr;
        return n;
    }

    // class declaration
    if (match(parser, TOK_CLASS)) {
        ASTNode* n = ast_new_node(AST_CLASS);
        n->as.class_decl.name = NULL;
        n->as.class_decl.superclass = NULL;
        n->as.class_decl.methods = NULL;
        n->as.class_decl.method_count = 0;
        if (check(parser, TOK_IDENTIFIER)) {
            n->as.class_decl.name = strndup(parser->current.start, parser->current.length);
            advance(parser);
        }
        if (match(parser, TOK_EXTENDS)) n->as.class_decl.superclass = parse_assignment(parser);
        consume(parser, TOK_LBRACE, "Expected '{' after class");
        while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
            if (match(parser, TOK_SEMICOLON)) continue;
            ASTClassMethod* m = calloc(1, sizeof(ASTClassMethod));
            ASTNode* fn_node = ast_new_node(AST_FUNCTION);
            m->func_node = fn_node;
            if (match(parser, TOK_STATIC))  m->is_static = true;
            if (match(parser, TOK_ASYNC))   m->is_async  = true;
            if (match(parser, TOK_STAR))    m->is_generator = true;
            if (check(parser, TOK_GET) || check(parser, TOK_SET)) {
                bool is_g = check(parser, TOK_GET);
                const char* gs = parser->current.start; int gs_len = parser->current.length;
                advance(parser);
                if (!check(parser, TOK_LPAREN)) {
                    m->is_getter = is_g; m->is_setter = !is_g;
                } else {
                    m->name = strndup(gs, gs_len);
                }
            }
            if (!m->name) {
                if (match(parser, TOK_LBRACKET)) {
                    m->is_computed = true; m->name_expr = parse_assignment(parser);
                    consume(parser, TOK_RBRACKET, "Expected ']'");
                } else {
                    m->name = strndup(parser->current.start, parser->current.length);
                    advance(parser);
                }
            }
            consume(parser, TOK_LPAREN, "Expected '(' in class method");
            parse_param_list(parser, &fn_node->as.function.params, &fn_node->as.function.param_count);
            consume(parser, TOK_RPAREN, "Expected ')'");
            fn_node->as.function.body = parse_statement(parser);
            fn_node->as.function.is_async = m->is_async; fn_node->as.function.is_generator = m->is_generator;
            fn_node->as.function.name = m->name ? strdup(m->name) : NULL;
            n->as.class_decl.methods = realloc(n->as.class_decl.methods, (n->as.class_decl.method_count+1)*sizeof(ASTClassMethod*));
            n->as.class_decl.methods[n->as.class_decl.method_count++] = m;
        }
        consume(parser, TOK_RBRACE, "Expected '}' after class body");
        return n;
    }

    // async function declaration / async arrow at statement level
    bool is_async_decl = match(parser, TOK_ASYNC);
    bool has_fn_kw = false;
    if (is_async_decl) {
        if (!match(parser, TOK_FUNCTION)) {
            // Might be async as identifier or async arrow expression statement
            // Put it back as identifier and fall through to expression statement
            // (parse_primary handles async arrow)
            ASTNode* id = ast_new_node(AST_IDENTIFIER);
            id->as.identifier.name = strdup("async");
            // Now parse the rest as expression with async as LHS
            // ... just parse as expression stmt
            // This is tricky without backtracking. For now, emit as identifier.
            ASTNode* expr_node = parse_assignment(parser);
            // We need to merge id with expr — complex. Simplified: ignore id.
            (void)id;
            ast_free_node(id);
            match(parser, TOK_SEMICOLON);
            ASTNode* n = ast_new_node(AST_EXPR_STMT);
            n->as.expr_stmt = expr_node;
            return n;
        }
        has_fn_kw = true;
    } else {
        has_fn_kw = match(parser, TOK_FUNCTION);
    }

    if (has_fn_kw) {
        bool is_gen = match(parser, TOK_STAR);
        consume(parser, TOK_IDENTIFIER, "Expected function name");
        char* name = strndup(parser->previous.start, parser->previous.length);
        consume(parser, TOK_LPAREN, "Expected '('");
        ASTNode* n = ast_new_node(AST_FUNCTION);
        n->as.function.name = name;
        n->as.function.is_async = is_async_decl;
        n->as.function.is_generator = is_gen;
        parse_param_list(parser, &n->as.function.params, &n->as.function.param_count);
        consume(parser, TOK_RPAREN, "Expected ')'");
        n->as.function.body = parse_statement(parser);
        return n;
    }

    // Expression statement
    ASTNode* expr = parse_expr(parser);
    if (!expr) return NULL;
    match(parser, TOK_SEMICOLON);
    ASTNode* n = ast_new_node(AST_EXPR_STMT);
    n->as.expr_stmt = expr;
    return n;
}

// ─── Scope system (unchanged from original) ───────────────────────────────────
typedef struct {
    char* name;
    int reg;
    int env_index;
    bool is_captured;
} Symbol;

typedef struct CompilerScope {
    Symbol* symbols;
    int count;
    int capacity;
    struct CompilerScope* parent;
    bool is_function_scope;
    int num_captured_vars;
} CompilerScope;

static CompilerScope* create_scope(CompilerScope* parent, bool is_function) {
    CompilerScope* s = malloc(sizeof(CompilerScope));
    s->symbols = NULL; s->count = 0; s->capacity = 0;
    s->parent = parent; s->is_function_scope = is_function; s->num_captured_vars = 0;
    return s;
}

static void free_scope(CompilerScope* scope) {
    if (!scope) return;
    for (int i = 0; i < scope->count; i++) free(scope->symbols[i].name);
    free(scope->symbols); free(scope);
}

static Symbol* add_symbol(CompilerScope* scope, const char* name) {
    for (int i = 0; i < scope->count; i++)
        if (strcmp(scope->symbols[i].name, name) == 0) return &scope->symbols[i];
    if (scope->count >= scope->capacity) {
        scope->capacity = scope->capacity == 0 ? 8 : scope->capacity * 2;
        scope->symbols = realloc(scope->symbols, scope->capacity * sizeof(Symbol));
    }
    Symbol* s = &scope->symbols[scope->count++];
    s->name = strdup(name); s->reg = -1; s->env_index = -1; s->is_captured = false;
    return s;
}

static CompilerScope* get_enclosing_function_scope(CompilerScope* scope) {
    CompilerScope* c = scope;
    while (c) { if (c->is_function_scope || c->parent == NULL) return c; c = c->parent; }
    return NULL;
}

static Symbol* lookup_symbol_resolve(CompilerScope* scope, const char* name, int* out_depth) {
    CompilerScope* c = scope; int depth = 0;
    while (c) {
        for (int i = 0; i < c->count; i++)
            if (strcmp(c->symbols[i].name, name) == 0) { *out_depth = depth; return &c->symbols[i]; }
        if (c->is_function_scope) depth++;
        c = c->parent;
    }
    return NULL;
}

static Symbol* lookup_symbol_codegen(CompilerScope* scope, const char* name, int* out_depth) {
    CompilerScope* c = scope; int depth = 0;
    while (c) {
        for (int i = 0; i < c->count; i++)
            if (strcmp(c->symbols[i].name, name) == 0) { *out_depth = depth; return &c->symbols[i]; }
        if (c->num_captured_vars > 0) depth++;
        c = c->parent;
    }
    return NULL;
}

// ─── Scope resolution pass (expanded for new node types) ─────────────────────
static void resolve_identifiers(ASTNode* node, CompilerScope* scope) {
    if (!node) return;
    switch (node->type) {
        case AST_IDENTIFIER: {
            int depth = 0;
            Symbol* sym = lookup_symbol_resolve(scope, node->as.identifier.name, &depth);
            if (sym && depth > 0 && !sym->is_captured) {
                sym->is_captured = true;
                CompilerScope* decl_scope = scope;
                while (decl_scope) {
                    bool found = false;
                    for (int i = 0; i < decl_scope->count; i++)
                        if (&decl_scope->symbols[i] == sym) { found = true; break; }
                    if (found) break;
                    decl_scope = decl_scope->parent;
                }
                if (decl_scope) {
                    CompilerScope* f = get_enclosing_function_scope(decl_scope);
                    if (f) sym->env_index = f->num_captured_vars++;
                }
            }
            break;
        }
        case AST_VAR_DECL:
            if (node->as.var_decl.name) add_symbol(scope, node->as.var_decl.name);
            for (int i = 0; i < node->as.var_decl.bind_count; i++) {
                if (node->as.var_decl.bindings[i].name)
                    add_symbol(scope, node->as.var_decl.bindings[i].name);
                resolve_identifiers(node->as.var_decl.bindings[i].default_val, scope);
            }
            resolve_identifiers(node->as.var_decl.init, scope);
            break;
        case AST_ASSIGN:
            resolve_identifiers(node->as.assign.target, scope);
            resolve_identifiers(node->as.assign.value, scope);
            break;
        case AST_BINARY:
            resolve_identifiers(node->as.binary.left, scope);
            resolve_identifiers(node->as.binary.right, scope);
            break;
        case AST_UNARY:
            resolve_identifiers(node->as.unary.expr, scope);
            break;
        case AST_POSTFIX:
            resolve_identifiers(node->as.postfix.expr, scope);
            break;
        case AST_TERNARY:
            resolve_identifiers(node->as.ternary.cond, scope);
            resolve_identifiers(node->as.ternary.then_expr, scope);
            resolve_identifiers(node->as.ternary.else_expr, scope);
            break;
        case AST_AWAIT:
            resolve_identifiers(node->as.await_expr.expr, scope);
            break;
        case AST_YIELD:
            resolve_identifiers(node->as.yield_expr.expr, scope);
            break;
        case AST_CALL:
        case AST_NEW_CALL:
            resolve_identifiers(node->as.call.callee, scope);
            for (int i = 0; i < node->as.call.arg_count; i++)
                resolve_identifiers(node->as.call.args[i], scope);
            break;
        case AST_BLOCK: {
            CompilerScope* inner = node->as.block.is_inline ? scope : create_scope(scope, false);
            node->scope = inner;
            for (int i = 0; i < node->as.block.count; i++)
                resolve_identifiers(node->as.block.statements[i], inner);
            break;
        }
        case AST_IF:
            resolve_identifiers(node->as.if_stmt.cond, scope);
            resolve_identifiers(node->as.if_stmt.then_branch, scope);
            resolve_identifiers(node->as.if_stmt.else_branch, scope);
            break;
        case AST_WHILE:
        case AST_DO_WHILE:
            resolve_identifiers(node->as.while_stmt.cond, scope);
            resolve_identifiers(node->as.while_stmt.body, scope);
            break;
        case AST_FOR: {
            CompilerScope* inner = create_scope(scope, false);
            node->scope = inner;
            resolve_identifiers(node->as.for_stmt.init, inner);
            resolve_identifiers(node->as.for_stmt.cond, inner);
            resolve_identifiers(node->as.for_stmt.update, inner);
            resolve_identifiers(node->as.for_stmt.body, inner);
            break;
        }
        case AST_FOR_OF: {
            CompilerScope* inner = create_scope(scope, false);
            node->scope = inner;
            if (node->as.for_of.binding_name) add_symbol(inner, node->as.for_of.binding_name);
            else if (node->as.for_of.binding_decl) resolve_identifiers(node->as.for_of.binding_decl, inner);
            resolve_identifiers(node->as.for_of.iterable, scope);
            resolve_identifiers(node->as.for_of.body, inner);
            break;
        }
        case AST_FOR_IN: {
            CompilerScope* inner = create_scope(scope, false);
            node->scope = inner;
            if (node->as.for_in.binding_name) add_symbol(inner, node->as.for_in.binding_name);
            else if (node->as.for_in.binding_decl) resolve_identifiers(node->as.for_in.binding_decl, inner);
            resolve_identifiers(node->as.for_in.object, scope);
            resolve_identifiers(node->as.for_in.body, inner);
            break;
        }
        case AST_SWITCH: {
            resolve_identifiers(node->as.switch_stmt.discriminant, scope);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTSwitchCase* cs = node->as.switch_stmt.cases[i];
                resolve_identifiers(cs->test, scope);
                for (int j = 0; j < cs->body_count; j++)
                    resolve_identifiers(cs->body[j], scope);
            }
            break;
        }
        case AST_RETURN: resolve_identifiers(node->as.return_stmt.expr, scope); break;
        case AST_FUNCTION: {
            CompilerScope* inner = create_scope(scope, true);
            node->scope = inner;
            for (int i = 0; i < node->as.function.param_count; i++) {
                if (node->as.function.params[i].name)
                    add_symbol(inner, node->as.function.params[i].name);
                resolve_identifiers(node->as.function.params[i].default_val, inner);
            }
            if (node->as.function.name) add_symbol(scope, node->as.function.name);
            resolve_identifiers(node->as.function.body, inner);
            break;
        }
        case AST_CLASS: {
            if (node->as.class_decl.name) add_symbol(scope, node->as.class_decl.name);
            resolve_identifiers(node->as.class_decl.superclass, scope);
            for (int i = 0; i < node->as.class_decl.method_count; i++) {
                ASTClassMethod* m = node->as.class_decl.methods[i];
                resolve_identifiers(m->func_node, scope);
            }
            break;
        }
        case AST_OBJECT:
            for (int i = 0; i < node->as.object.count; i++) {
                resolve_identifiers(node->as.object.key_exprs[i], scope);
                resolve_identifiers(node->as.object.values[i], scope);
            }
            break;
        case AST_ARRAY:
            for (int i = 0; i < node->as.array.count; i++)
                resolve_identifiers(node->as.array.elements[i], scope);
            break;
        case AST_SPREAD: resolve_identifiers(node->as.spread_expr, scope); break;
        case AST_TEMPLATE_LITERAL:
            for (int i = 0; i < node->as.tmpl.part_count; i++)
                resolve_identifiers(node->as.tmpl.parts[i], scope);
            break;
        case AST_TRY:
            resolve_identifiers(node->as.try_stmt.try_block, scope);
            if (node->as.try_stmt.catch_block) {
                if (node->as.try_stmt.catch_param && node->as.try_stmt.catch_block->type == AST_BLOCK) {
                    CompilerScope* cs = create_scope(scope, false);
                    node->as.try_stmt.catch_block->scope = cs;
                    add_symbol(cs, node->as.try_stmt.catch_param);
                    for (int i = 0; i < node->as.try_stmt.catch_block->as.block.count; i++)
                        resolve_identifiers(node->as.try_stmt.catch_block->as.block.statements[i], cs);
                } else resolve_identifiers(node->as.try_stmt.catch_block, scope);
            }
            resolve_identifiers(node->as.try_stmt.finally_block, scope);
            break;
        case AST_THROW:
            resolve_identifiers(node->as.throw_stmt.throw_stmt, scope);
            break;
        case AST_PROP_ACCESS:
        case AST_OPTIONAL_CHAIN:
            resolve_identifiers(node->as.prop_access.obj, scope);
            resolve_identifiers(node->as.prop_access.prop, scope);
            break;
        case AST_EXPR_STMT: resolve_identifiers(node->as.expr_stmt, scope); break;
        case AST_SEQUENCE:
            for (int i = 0; i < node->as.sequence.count; i++)
                resolve_identifiers(node->as.sequence.exprs[i], scope);
            break;
        default: break;
    }
}

// ─── Emitter types ────────────────────────────────────────────────────────────
typedef struct {
    CompiledProgram* program;
    int next_reg;
    int max_reg;
    // break/continue target stacks
    int* break_targets;
    int  break_top;
    int  break_cap;
    int* continue_targets;
    int  continue_top;
    int  continue_cap;
} Emitter;

static int add_constant_val(CompiledProgram* prog, Value val) {
    for (uint32_t i = 0; i < prog->const_pool_size; i++)
        if (prog->const_pool[i] == val) return i;
    prog->const_pool = realloc(prog->const_pool, (prog->const_pool_size + 1) * sizeof(Value));
    prog->const_pool[prog->const_pool_size] = val;
    return prog->const_pool_size++;
}

static int add_constant_double(CompiledProgram* prog, double d) {
    union { double d; uint64_t u; } cast; cast.d = d;
    return add_constant_val(prog, cast.u);
}

static int add_constant_string(CompiledProgram* prog, const char* str, int len) {
    for (uint32_t i = 0; i < prog->const_pool_size; i++) {
        Value val = prog->const_pool[i];
        if (IS_POINTER(val)) {
            BlockHeader* hdr = (BlockHeader*)((char*)get_pointer(val) - sizeof(BlockHeader));
            if (hdr->obj_type == OBJ_STRING) {
                JSString* s = (JSString*)get_pointer(val);
                if (s->length == (uint32_t)len && memcmp(s->data, str, len) == 0) return i;
            }
        }
    }
    char* block = malloc(sizeof(BlockHeader) + sizeof(JSString) + len + 1);
    BlockHeader* hdr = (BlockHeader*)block;
    hdr->size = sizeof(BlockHeader) + sizeof(JSString) + len + 1;
    hdr->is_free = 0; hdr->gc_mark = 0; hdr->obj_type = OBJ_STRING;
    JSString* s = (JSString*)(block + sizeof(BlockHeader));
    s->length = len; s->hash = hash_string(str, len);
    memcpy(s->data, str, len); s->data[len] = '\0';
    return add_constant_val(prog, make_pointer(s));
}

static void emit_instruction(CompiledProgram* prog, uint32_t inst) {
    prog->bytecode = realloc(prog->bytecode, (prog->bytecode_size + 1) * sizeof(uint32_t));
    prog->bytecode[prog->bytecode_size++] = inst;
}

static int alloc_register(Emitter* emit) {
    int r = emit->next_reg++;
    if (emit->next_reg > emit->max_reg) emit->max_reg = emit->next_reg;
    return r;
}

static void free_registers(Emitter* emit, int to_reg) { emit->next_reg = to_reg; }

// Break/continue stack helpers
static void push_break_target(Emitter* emit, int target_patch_idx) {
    if (emit->break_top >= emit->break_cap) {
        emit->break_cap = emit->break_cap ? emit->break_cap * 2 : 8;
        emit->break_targets = realloc(emit->break_targets, emit->break_cap * sizeof(int));
    }
    emit->break_targets[emit->break_top++] = target_patch_idx;
}
static void push_continue_target(Emitter* emit, int target_patch_idx) {
    if (emit->continue_top >= emit->continue_cap) {
        emit->continue_cap = emit->continue_cap ? emit->continue_cap * 2 : 8;
        emit->continue_targets = realloc(emit->continue_targets, emit->continue_cap * sizeof(int));
    }
    emit->continue_targets[emit->continue_top++] = target_patch_idx;
}

// Forward declaration
static int compile_node(ASTNode* node, CompiledProgram* prog, CompilerScope* scope, Emitter* emit, int dest_reg);

// Emit a store-to-lvalue (for assignments and ++/--)
static void emit_store_lvalue(ASTNode* target, int val_reg, CompiledProgram* prog, CompilerScope* scope, Emitter* emit) {
    if (target->type == AST_IDENTIFIER) {
        int depth = 0;
        Symbol* sym = lookup_symbol_codegen(scope, target->as.identifier.name, &depth);
        if (sym) {
            if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, val_reg, sym->env_index, depth));
            else emit_instruction(prog, make_abc(OP_MOVE, sym->reg, val_reg, 0));
        } else {
            int ci = add_constant_string(prog, target->as.identifier.name, (int)strlen(target->as.identifier.name));
            emit_instruction(prog, make_abx(OP_STORE_GLOBAL, val_reg, ci));
        }
    } else if (target->type == AST_PROP_ACCESS || target->type == AST_OPTIONAL_CHAIN) {
        int obj_r = alloc_register(emit);
        compile_node(target->as.prop_access.obj, prog, scope, emit, obj_r);
        int prop_r = alloc_register(emit);
        compile_node(target->as.prop_access.prop, prog, scope, emit, prop_r);
        emit_instruction(prog, make_abc(OP_STORE_PROP, obj_r, prop_r, val_reg));
        free_registers(emit, prop_r);
        free_registers(emit, obj_r);
    }
}

// ─── Compound assignment opcode helper ───────────────────────────────────────
static Opcode compound_op_to_opcode(int compound_op) {
    switch (compound_op) {
        case TOK_PLUS_ASSIGN:    return OP_ADD;
        case TOK_MINUS_ASSIGN:   return OP_SUB;
        case TOK_STAR_ASSIGN:    return OP_MUL;
        case TOK_SLASH_ASSIGN:   return OP_DIV;
        case TOK_PERCENT_ASSIGN: return OP_MOD;
        case TOK_STARSTAR_ASSIGN:return OP_POW;
        case TOK_BITAND_ASSIGN:  return OP_BITAND;
        case TOK_BITOR_ASSIGN:   return OP_BITOR;
        case TOK_BITXOR_ASSIGN:  return OP_BITXOR;
        case TOK_SHL_ASSIGN:     return OP_SHL;
        case TOK_SHR_ASSIGN:     return OP_SHR;
        case TOK_USHR_ASSIGN:    return OP_USHR;
        default:                 return OP_ADD;
    }
}

// ─── Main bytecode emission ────────────────────────────────────────────────────
static int compile_node(ASTNode* node, CompiledProgram* prog, CompilerScope* scope, Emitter* emit, int dest_reg) {
    if (!node) {
        emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
        return dest_reg;
    }
    switch (node->type) {
        case AST_EMPTY:
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;

        case AST_LITERAL_NUMBER: {
            double v = node->as.number.value;
            if (v == (int)v && v >= -32768 && v <= 32767) {
                emit_instruction(prog, make_asbx(OP_LOAD_INT, dest_reg, (int16_t)(int)v));
            } else {
                int ci = add_constant_double(prog, v);
                emit_instruction(prog, make_abx(OP_LOAD_CONST, dest_reg, ci));
            }
            return dest_reg;
        }
        case AST_LITERAL_STRING: {
            int ci = add_constant_string(prog, node->as.string.value, node->as.string.length);
            emit_instruction(prog, make_abx(OP_LOAD_CONST, dest_reg, ci));
            return dest_reg;
        }
        case AST_LITERAL_BOOL:
            emit_instruction(prog, make_abc(OP_LOAD_BOOL, dest_reg, node->as.boolean.value ? 1 : 0, 0));
            return dest_reg;
        case AST_LITERAL_NULL:
            emit_instruction(prog, make_abc(OP_LOAD_NULL, dest_reg, 0, 0));
            return dest_reg;
        case AST_LITERAL_UNDEFINED:
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        case AST_LITERAL_REGEX: {
            // Emit new RegExp(pattern, flags)
            int ctor_r = alloc_register(emit);
            int ci = add_constant_string(prog, "RegExp", 6);
            emit_instruction(prog, make_abx(OP_LOAD_GLOBAL, ctor_r, ci));
            int pat_r = alloc_register(emit);
            int pci = add_constant_string(prog, node->as.regex.pattern, (int)strlen(node->as.regex.pattern));
            emit_instruction(prog, make_abx(OP_LOAD_CONST, pat_r, pci));
            int flg_r = alloc_register(emit);
            int fci = add_constant_string(prog, node->as.regex.flags, (int)strlen(node->as.regex.flags));
            emit_instruction(prog, make_abx(OP_LOAD_CONST, flg_r, fci));
            emit_instruction(prog, make_abc(OP_NEW_CALL, dest_reg, ctor_r, 2));
            free_registers(emit, ctor_r);
            return dest_reg;
        }
        case AST_IDENTIFIER: {
            int depth = 0;
            Symbol* sym = lookup_symbol_codegen(scope, node->as.identifier.name, &depth);
            if (sym) {
                if (sym->is_captured) emit_instruction(prog, make_abc(OP_LOAD_ENV, dest_reg, sym->env_index, depth));
                else {
                    if (sym->reg == -1) sym->reg = alloc_register(emit);
                    emit_instruction(prog, make_abc(OP_MOVE, dest_reg, sym->reg, 0));
                }
            } else {
                int ci = add_constant_string(prog, node->as.identifier.name, (int)strlen(node->as.identifier.name));
                emit_instruction(prog, make_abx(OP_LOAD_GLOBAL, dest_reg, ci));
            }
            return dest_reg;
        }
        case AST_VAR_DECL: {
            if (node->as.var_decl.bind_count > 0) {
                // Destructuring: compile the RHS init, then extract bindings
                int rhs_r = alloc_register(emit);
                if (node->as.var_decl.init) compile_node(node->as.var_decl.init, prog, scope, emit, rhs_r);
                else emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, rhs_r, 0, 0));
                for (int i = 0; i < node->as.var_decl.bind_count; i++) {
                    ASTBindingElem* b = &node->as.var_decl.bindings[i];
                    if (!b->name) continue;
                    int depth = 0;
                    Symbol* sym = lookup_symbol_codegen(scope, b->name, &depth);
                    if (!sym) continue;
                    if (sym->reg == -1) sym->reg = alloc_register(emit);
                    // Load the property from rhs
                    int key_r = alloc_register(emit);
                    if (node->as.var_decl.is_array_pattern) {
                        emit_instruction(prog, make_asbx(OP_LOAD_INT, key_r, (int16_t)i));
                    } else {
                        int kci = add_constant_string(prog, b->key ? b->key : b->name, (int)strlen(b->key ? b->key : b->name));
                        emit_instruction(prog, make_abx(OP_LOAD_CONST, key_r, kci));
                    }
                    int val_r = alloc_register(emit);
                    emit_instruction(prog, make_abc(OP_LOAD_PROP, val_r, rhs_r, key_r));
                    free_registers(emit, key_r);
                    // Apply default if val is undefined
                    if (b->default_val) {
                        int def_r = alloc_register(emit);
                        int jmp_idx = (int)prog->bytecode_size;
                        emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, val_r, 0));
                        int skip_idx = (int)prog->bytecode_size;
                        emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                        int def_offset = (int)prog->bytecode_size - jmp_idx - 1;
                        prog->bytecode[jmp_idx] = make_asbx(OP_JUMP_IF_NULLISH, val_r, (int16_t)def_offset);
                        compile_node(b->default_val, prog, scope, emit, def_r);
                        emit_instruction(prog, make_abc(OP_MOVE, val_r, def_r, 0));
                        int skip_offset = (int)prog->bytecode_size - skip_idx - 1;
                        prog->bytecode[skip_idx] = make_asbx(OP_JUMP, 0, (int16_t)skip_offset);
                        free_registers(emit, def_r);
                    }
                    if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, val_r, sym->env_index, depth));
                    else emit_instruction(prog, make_abc(OP_MOVE, sym->reg, val_r, 0));
                    free_registers(emit, val_r);
                }
                free_registers(emit, rhs_r);
            } else {
                // Simple binding
                int depth = 0;
                Symbol* sym = lookup_symbol_codegen(scope, node->as.var_decl.name, &depth);
                if (sym && !sym->is_captured && sym->reg == -1) sym->reg = alloc_register(emit);
                int temp_r = alloc_register(emit);
                if (node->as.var_decl.init) compile_node(node->as.var_decl.init, prog, scope, emit, temp_r);
                else emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, temp_r, 0, 0));
                if (sym) {
                    if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, temp_r, sym->env_index, 0));
                    else emit_instruction(prog, make_abc(OP_MOVE, sym->reg, temp_r, 0));
                } else {
                    int ci = add_constant_string(prog, node->as.var_decl.name, (int)strlen(node->as.var_decl.name));
                    emit_instruction(prog, make_abx(OP_STORE_GLOBAL, temp_r, ci));
                }
                free_registers(emit, temp_r);
            }
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_ASSIGN: {
            int val_r = alloc_register(emit);
            if (node->as.assign.compound_op == 0) {
                // Plain assignment
                compile_node(node->as.assign.value, prog, scope, emit, val_r);
            } else if (node->as.assign.compound_op == TOK_AND_ASSIGN) {
                // &&= : only assign if lhs is truthy
                compile_node(node->as.assign.target, prog, scope, emit, val_r);
                int jmp_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, val_r, 0));
                compile_node(node->as.assign.value, prog, scope, emit, val_r);
                emit_store_lvalue(node->as.assign.target, val_r, prog, scope, emit);
                int offset = (int)prog->bytecode_size - jmp_idx - 1;
                prog->bytecode[jmp_idx] = make_asbx(OP_JUMP_IF_FALSE, val_r, (int16_t)offset);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, val_r, 0));
                free_registers(emit, val_r);
                return dest_reg;
            } else if (node->as.assign.compound_op == TOK_OR_ASSIGN) {
                // ||= : only assign if lhs is falsy
                compile_node(node->as.assign.target, prog, scope, emit, val_r);
                int jmp_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, val_r, 0));
                compile_node(node->as.assign.value, prog, scope, emit, val_r);
                emit_store_lvalue(node->as.assign.target, val_r, prog, scope, emit);
                int offset = (int)prog->bytecode_size - jmp_idx - 1;
                prog->bytecode[jmp_idx] = make_asbx(OP_JUMP_IF_TRUE, val_r, (int16_t)offset);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, val_r, 0));
                free_registers(emit, val_r);
                return dest_reg;
            } else if (node->as.assign.compound_op == TOK_NULLISH_ASSIGN) {
                // ??= : only assign if lhs is null/undefined
                compile_node(node->as.assign.target, prog, scope, emit, val_r);
                int jmp_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, val_r, 0));
                int skip_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                int null_off = (int)prog->bytecode_size - jmp_idx - 1;
                prog->bytecode[jmp_idx] = make_asbx(OP_JUMP_IF_NULLISH, val_r, (int16_t)null_off);
                compile_node(node->as.assign.value, prog, scope, emit, val_r);
                emit_store_lvalue(node->as.assign.target, val_r, prog, scope, emit);
                int skip_off = (int)prog->bytecode_size - skip_idx - 1;
                prog->bytecode[skip_idx] = make_asbx(OP_JUMP, 0, (int16_t)skip_off);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, val_r, 0));
                free_registers(emit, val_r);
                return dest_reg;
            } else {
                // Arithmetic compound: lhs op= rhs
                int lhs_r = alloc_register(emit);
                compile_node(node->as.assign.target, prog, scope, emit, lhs_r);
                int rhs_r = alloc_register(emit);
                compile_node(node->as.assign.value, prog, scope, emit, rhs_r);
                Opcode op = compound_op_to_opcode(node->as.assign.compound_op);
                emit_instruction(prog, make_abc(op, val_r, lhs_r, rhs_r));
                free_registers(emit, rhs_r);
                free_registers(emit, lhs_r);
            }
            emit_store_lvalue(node->as.assign.target, val_r, prog, scope, emit);
            emit_instruction(prog, make_abc(OP_MOVE, dest_reg, val_r, 0));
            free_registers(emit, val_r);
            return dest_reg;
        }
        case AST_BINARY: {
            int op = node->as.binary.op;
            // Short-circuit logical operators
            if (op == TOK_OR) {
                int l = alloc_register(emit);
                compile_node(node->as.binary.left, prog, scope, emit, l);
                int jt_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, l, 0));
                int r = alloc_register(emit);
                compile_node(node->as.binary.right, prog, scope, emit, r);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, r, 0));
                free_registers(emit, r);
                int je_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                prog->bytecode[jt_idx] = make_asbx(OP_JUMP_IF_TRUE, l, (int16_t)((int)prog->bytecode_size - jt_idx - 1));
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, l, 0));
                free_registers(emit, l);
                prog->bytecode[je_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - je_idx - 1));
                return dest_reg;
            }
            if (op == TOK_AND) {
                int l = alloc_register(emit);
                compile_node(node->as.binary.left, prog, scope, emit, l);
                int jf_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, l, 0));
                int r = alloc_register(emit);
                compile_node(node->as.binary.right, prog, scope, emit, r);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, r, 0));
                free_registers(emit, r);
                int je_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_FALSE, l, (int16_t)((int)prog->bytecode_size - jf_idx - 1));
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, l, 0));
                free_registers(emit, l);
                prog->bytecode[je_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - je_idx - 1));
                return dest_reg;
            }
            if (op == TOK_NULLISH) {
                int l = alloc_register(emit);
                compile_node(node->as.binary.left, prog, scope, emit, l);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, l, 0));
                int jn_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, l, 0));
                int je_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                prog->bytecode[jn_idx] = make_asbx(OP_JUMP_IF_NULLISH, l, (int16_t)((int)prog->bytecode_size - jn_idx - 1));
                int r = alloc_register(emit);
                compile_node(node->as.binary.right, prog, scope, emit, r);
                emit_instruction(prog, make_abc(OP_MOVE, dest_reg, r, 0));
                free_registers(emit, r);
                free_registers(emit, l);
                prog->bytecode[je_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - je_idx - 1));
                return dest_reg;
            }
            int l = alloc_register(emit);
            compile_node(node->as.binary.left, prog, scope, emit, l);
            int r = alloc_register(emit);
            compile_node(node->as.binary.right, prog, scope, emit, r);
            Opcode opc;
            switch (op) {
                case TOK_PLUS:         opc = OP_ADD; break;
                case TOK_MINUS:        opc = OP_SUB; break;
                case TOK_STAR:         opc = OP_MUL; break;
                case TOK_SLASH:        opc = OP_DIV; break;
                case TOK_PERCENT:      opc = OP_MOD; break;
                case TOK_STARSTAR:     opc = OP_POW; break;
                case TOK_LT:           opc = OP_LT; break;
                case TOK_LE:           opc = OP_LE; break;
                case TOK_GT:           opc = OP_GT; break;
                case TOK_GE:           opc = OP_GE; break;
                case TOK_EQ:           opc = OP_EQ; break;
                case TOK_NE:           opc = OP_NE; break;
                case TOK_EQ_LOOSE:     opc = OP_EQ_LOOSE; break;
                case TOK_NE_LOOSE:     opc = OP_NE_LOOSE; break;
                case TOK_AMPERSAND:    opc = OP_BITAND; break;
                case TOK_PIPE:         opc = OP_BITOR; break;
                case TOK_CARET:        opc = OP_BITXOR; break;
                case TOK_SHL:          opc = OP_SHL; break;
                case TOK_SHR:          opc = OP_SHR; break;
                case TOK_USHR:         opc = OP_USHR; break;
                case TOK_IN:           opc = OP_IN; break;
                case TOK_INSTANCEOF:   opc = OP_INSTANCEOF; break;
                default:               opc = OP_ADD; break;
            }
            emit_instruction(prog, make_abc(opc, dest_reg, l, r));
            free_registers(emit, r);
            free_registers(emit, l);
            return dest_reg;
        }
        case AST_UNARY: {
            int e = alloc_register(emit);
            compile_node(node->as.unary.expr, prog, scope, emit, e);
            switch (node->as.unary.op) {
                case TOK_MINUS: emit_instruction(prog, make_abc(OP_NEG, dest_reg, e, 0)); break;
                case TOK_NOT: emit_instruction(prog, make_abc(OP_NOT, dest_reg, e, 0)); break;
                case TOK_TILDE: emit_instruction(prog, make_abc(OP_BITNOT, dest_reg, e, 0)); break;
                case TOK_TYPEOF: emit_instruction(prog, make_abc(OP_TYPEOF, dest_reg, e, 0)); break;
                case TOK_VOID:   emit_instruction(prog, make_abc(OP_VOID, dest_reg, e, 0)); break;
                case TOK_DELETE:
                    if (node->as.unary.expr->type == AST_PROP_ACCESS) {
                        // Recompile obj and prop
                        free_registers(emit, e);
                        int obj_r = alloc_register(emit);
                        compile_node(node->as.unary.expr->as.prop_access.obj, prog, scope, emit, obj_r);
                        int prop_r = alloc_register(emit);
                        compile_node(node->as.unary.expr->as.prop_access.prop, prog, scope, emit, prop_r);
                        emit_instruction(prog, make_abc(OP_DELETE_PROP, dest_reg, obj_r, prop_r));
                        free_registers(emit, prop_r);
                        free_registers(emit, obj_r);
                        return dest_reg;
                    }
                    emit_instruction(prog, make_abc(OP_LOAD_BOOL, dest_reg, 1, 0)); // delete returns true
                    break;
                case TOK_PLUSPLUS: {
                    // Prefix increment
                    int one_r = alloc_register(emit);
                    emit_instruction(prog, make_asbx(OP_LOAD_INT, one_r, 1));
                    emit_instruction(prog, make_abc(OP_ADD, dest_reg, e, one_r));
                    free_registers(emit, one_r);
                    emit_store_lvalue(node->as.unary.expr, dest_reg, prog, scope, emit);
                    break;
                }
                case TOK_MINUSMINUS: {
                    int one_r = alloc_register(emit);
                    emit_instruction(prog, make_asbx(OP_LOAD_INT, one_r, 1));
                    emit_instruction(prog, make_abc(OP_SUB, dest_reg, e, one_r));
                    free_registers(emit, one_r);
                    emit_store_lvalue(node->as.unary.expr, dest_reg, prog, scope, emit);
                    break;
                }
                default: emit_instruction(prog, make_abc(OP_MOVE, dest_reg, e, 0)); break;
            }
            free_registers(emit, e);
            return dest_reg;
        }
        case AST_POSTFIX: {
            // Return old value, store incremented/decremented
            int e = alloc_register(emit);
            compile_node(node->as.postfix.expr, prog, scope, emit, e);
            emit_instruction(prog, make_abc(OP_MOVE, dest_reg, e, 0)); // dest = old value
            int one_r = alloc_register(emit);
            emit_instruction(prog, make_asbx(OP_LOAD_INT, one_r, 1));
            int new_r = alloc_register(emit);
            if (node->as.postfix.op == TOK_PLUSPLUS)
                emit_instruction(prog, make_abc(OP_ADD, new_r, e, one_r));
            else
                emit_instruction(prog, make_abc(OP_SUB, new_r, e, one_r));
            free_registers(emit, one_r);
            emit_store_lvalue(node->as.postfix.expr, new_r, prog, scope, emit);
            free_registers(emit, new_r);
            free_registers(emit, e);
            return dest_reg;
        }
        case AST_TERNARY: {
            int cond_r = alloc_register(emit);
            compile_node(node->as.ternary.cond, prog, scope, emit, cond_r);
            int jf_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, cond_r, 0));
            free_registers(emit, cond_r);
            compile_node(node->as.ternary.then_expr, prog, scope, emit, dest_reg);
            int je_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_FALSE, cond_r, (int16_t)((int)prog->bytecode_size - jf_idx - 1));
            compile_node(node->as.ternary.else_expr, prog, scope, emit, dest_reg);
            prog->bytecode[je_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - je_idx - 1));
            return dest_reg;
        }
        case AST_AWAIT: {
            int e = alloc_register(emit);
            compile_node(node->as.await_expr.expr, prog, scope, emit, e);
            emit_instruction(prog, make_abc(OP_AWAIT, dest_reg, e, 0));
            free_registers(emit, e);
            return dest_reg;
        }
        case AST_YIELD: {
            int e = alloc_register(emit);
            if (node->as.yield_expr.expr)
                compile_node(node->as.yield_expr.expr, prog, scope, emit, e);
            else
                emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, e, 0, 0));
            emit_instruction(prog, make_abc(OP_YIELD, dest_reg, e, 0));
            free_registers(emit, e);
            return dest_reg;
        }
        case AST_CALL: {
            int callee_r = alloc_register(emit);
            compile_node(node->as.call.callee, prog, scope, emit, callee_r);
            int arg_count = 0;
            for (int i = 0; i < node->as.call.arg_count; i++) {
                ASTNode* arg = node->as.call.args[i];
                if (arg && arg->type == AST_SPREAD) {
                    // For spread args: emit them and use OP_ARRAY_SPREAD logic
                    // Simplified: just emit the spread value (VM handles it)
                    int ar = alloc_register(emit);
                    compile_node(arg->as.spread_expr, prog, scope, emit, ar);
                    arg_count++;
                } else {
                    int ar = alloc_register(emit);
                    compile_node(arg, prog, scope, emit, ar);
                    arg_count++;
                }
            }
            emit_instruction(prog, make_abc(OP_CALL, dest_reg, callee_r, (uint8_t)arg_count));
            free_registers(emit, callee_r);
            return dest_reg;
        }
        case AST_NEW_CALL: {
            int callee_r = alloc_register(emit);
            compile_node(node->as.call.callee, prog, scope, emit, callee_r);
            for (int i = 0; i < node->as.call.arg_count; i++) {
                ASTNode* arg = node->as.call.args[i];
                int ar = alloc_register(emit);
                compile_node(arg && arg->type == AST_SPREAD ? arg->as.spread_expr : arg, prog, scope, emit, ar);
            }
            emit_instruction(prog, make_abc(OP_NEW_CALL, dest_reg, callee_r, (uint8_t)node->as.call.arg_count));
            free_registers(emit, callee_r);
            return dest_reg;
        }
        case AST_BLOCK: {
            CompilerScope* inner = (CompilerScope*)node->scope;
            int base_r = emit->next_reg;

            int* using_regs = NULL; bool* using_is_await = NULL; int* try_begin_idxs = NULL; int using_count = 0;

            for (int i = 0; i < node->as.block.count; i++) {
                int r_dis = alloc_register(emit);
                int r_state = emit->next_reg;
                ASTNode* stmt = node->as.block.statements[i];
                compile_node(stmt, prog, inner, emit, r_dis);
                if (stmt && stmt->type == AST_VAR_DECL && (stmt->as.var_decl.is_using || stmt->as.var_decl.is_await_using)) {
                    using_count++;
                    using_regs = realloc(using_regs, using_count * sizeof(int));
                    using_is_await = realloc(using_is_await, using_count * sizeof(bool));
                    try_begin_idxs = realloc(try_begin_idxs, using_count * sizeof(int));
                    int depth = 0;
                    Symbol* sym = stmt->as.var_decl.name ? lookup_symbol_codegen(inner, stmt->as.var_decl.name, &depth) : NULL;
                    using_regs[using_count-1] = sym ? sym->reg : r_dis;
                    using_is_await[using_count-1] = stmt->as.var_decl.is_await_using;
                    try_begin_idxs[using_count-1] = (int)prog->bytecode_size;
                    emit_instruction(prog, make_asbx(OP_TRY_BEGIN, 0, 0));
                }
                int var_boundary = r_state;
                if (inner) {
                    for (int j = 0; j < inner->count; j++)
                        if (inner->symbols[j].reg >= var_boundary) var_boundary = inner->symbols[j].reg + 1;
                }
                emit->next_reg = var_boundary;
            }
            // Emit using disposals (same as original)
            for (int i = using_count - 1; i >= 0; i--) {
                emit_instruction(prog, make_abc(OP_TRY_END, 0, 0, 0));
                int joc_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                int catch_off = (int)prog->bytecode_size - try_begin_idxs[i] - 1;
                prog->bytecode[try_begin_idxs[i]] = make_asbx(OP_TRY_BEGIN, 0, (int16_t)catch_off);
                int err_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_CATCH_BEGIN, err_r, 0, 0));
                // Call dispose
                int sym_r = alloc_register(emit);
                int meth_r = alloc_register(emit);
                int sni = add_constant_string(prog, "Symbol", 6);
                emit_instruction(prog, make_abx(OP_LOAD_GLOBAL, sym_r, sni));
                const char* mn = using_is_await[i] ? "asyncDispose" : "dispose";
                int dsi = add_constant_string(prog, mn, (int)strlen(mn));
                int dsr = alloc_register(emit);
                emit_instruction(prog, make_abx(OP_LOAD_CONST, dsr, dsi));
                emit_instruction(prog, make_abc(OP_LOAD_PROP, meth_r, sym_r, dsr));
                free_registers(emit, dsr); free_registers(emit, sym_r);
                int fn_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_LOAD_PROP, fn_r, using_regs[i], meth_r));
                free_registers(emit, meth_r);
                int sd_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, fn_r, 0));
                int cr = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_CALL, cr, fn_r, 0));
                if (using_is_await[i]) emit_instruction(prog, make_abc(OP_AWAIT, cr, cr, 0));
                free_registers(emit, cr);
                int sd_off = (int)prog->bytecode_size - sd_idx - 1;
                prog->bytecode[sd_idx] = make_asbx(OP_JUMP_IF_NULLISH, fn_r, (int16_t)sd_off);
                free_registers(emit, fn_r);
                emit_instruction(prog, make_abc(OP_THROW, err_r, 0, 0));
                free_registers(emit, err_r);
                int ce_off = (int)prog->bytecode_size - joc_idx - 1;
                prog->bytecode[joc_idx] = make_asbx(OP_JUMP, 0, (int16_t)ce_off);
            }
            if (using_regs) { free(using_regs); free(using_is_await); free(try_begin_idxs); }
            emit->next_reg = base_r;
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_IF: {
            int cond_r = alloc_register(emit);
            compile_node(node->as.if_stmt.cond, prog, scope, emit, cond_r);
            int jf_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, cond_r, 0));
            free_registers(emit, cond_r);
            int br = alloc_register(emit);
            compile_node(node->as.if_stmt.then_branch, prog, scope, emit, br);
            free_registers(emit, br);
            int je_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_FALSE, cond_r, (int16_t)((int)prog->bytecode_size - jf_idx - 1));
            if (node->as.if_stmt.else_branch) {
                int er = alloc_register(emit);
                compile_node(node->as.if_stmt.else_branch, prog, scope, emit, er);
                free_registers(emit, er);
            }
            prog->bytecode[je_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - je_idx - 1));
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_WHILE: {
            int loop_start = (int)prog->bytecode_size;
            int cond_r = alloc_register(emit);
            compile_node(node->as.while_stmt.cond, prog, scope, emit, cond_r);
            int jf_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, cond_r, 0));
            free_registers(emit, cond_r);
            int br = alloc_register(emit);
            compile_node(node->as.while_stmt.body, prog, scope, emit, br);
            free_registers(emit, br);
            int back_off = loop_start - (int)prog->bytecode_size - 1;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, (int16_t)back_off));
            int exit_off = (int)prog->bytecode_size - jf_idx - 1;
            prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_FALSE, cond_r, (int16_t)exit_off);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_DO_WHILE: {
            int loop_start = (int)prog->bytecode_size;
            int br = alloc_register(emit);
            compile_node(node->as.while_stmt.body, prog, scope, emit, br);
            free_registers(emit, br);
            int cond_r = alloc_register(emit);
            compile_node(node->as.while_stmt.cond, prog, scope, emit, cond_r);
            int back_off = loop_start - (int)prog->bytecode_size - 1;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, cond_r, (int16_t)back_off));
            free_registers(emit, cond_r);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_FOR: {
            CompilerScope* inner = (CompilerScope*)node->scope;
            int base_r = emit->next_reg;
            int init_dis = alloc_register(emit);
            int init_r = emit->next_reg;
            if (node->as.for_stmt.init) compile_node(node->as.for_stmt.init, prog, inner, emit, init_dis);
            int var_boundary = init_r;
            if (inner) for (int j = 0; j < inner->count; j++)
                if (inner->symbols[j].reg >= var_boundary) var_boundary = inner->symbols[j].reg + 1;
            emit->next_reg = var_boundary;
            int loop_start = (int)prog->bytecode_size;
            int cond_r = emit->next_reg; int jf_idx = -1;
            if (node->as.for_stmt.cond) {
                emit->next_reg = cond_r + 1;
                compile_node(node->as.for_stmt.cond, prog, inner, emit, cond_r);
                jf_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, cond_r, 0));
            }
            emit->next_reg = var_boundary;
            int body_r = emit->next_reg; emit->next_reg = body_r + 1;
            compile_node(node->as.for_stmt.body, prog, inner, emit, body_r);
            emit->next_reg = var_boundary;
            int upd_r = emit->next_reg;
            if (node->as.for_stmt.update) { emit->next_reg = upd_r + 1; compile_node(node->as.for_stmt.update, prog, inner, emit, upd_r); }
            emit->next_reg = var_boundary;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, (int16_t)(loop_start - (int)prog->bytecode_size - 1)));
            if (jf_idx != -1) prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_FALSE, cond_r, (int16_t)((int)prog->bytecode_size - jf_idx - 1));
            emit->next_reg = base_r;
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_FOR_OF: {
            CompilerScope* inner = (CompilerScope*)node->scope;
            int base_r = emit->next_reg;
            // Evaluate iterable
            int iter_src_r = alloc_register(emit);
            compile_node(node->as.for_of.iterable, prog, scope, emit, iter_src_r);
            // Get iterator: iter = iterable[Symbol.iterator]()
            int iter_r = alloc_register(emit);
            int iter_idx_r = alloc_register(emit);
            emit_instruction(prog, make_asbx(OP_LOAD_INT, iter_idx_r, 0));
            if (node->as.for_of.is_await) {
                emit_instruction(prog, make_abc(OP_GET_ASYNC_ITER, iter_r, iter_src_r, 0));
            } else {
                emit_instruction(prog, make_abc(OP_GET_ITER, iter_r, iter_src_r, 0));
            }
            // Cannot free iter_src_r here because free_registers truncates the stack!
            // We just let it be reused at the end of the loop when base_r is restored.
            // Allocate binding var
            int bind_r = alloc_register(emit);
            if (node->as.for_of.binding_name && inner) {
                int depth = 0;
                Symbol* sym = lookup_symbol_codegen(inner, node->as.for_of.binding_name, &depth);
                if (sym) {
                    if (sym->reg == -1) sym->reg = alloc_register(emit);
                    bind_r = sym->reg;
                }
            }
            // Loop: (val, done) = iter.next(); if done break; binding = val
            int result_r = alloc_register(emit);
            int done_r = alloc_register(emit);
            int loop_start = (int)prog->bytecode_size;
            if (node->as.for_of.is_await) {
                int prom_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_ASYNC_ITER_NEXT, prom_r, iter_r, 0));
                emit_instruction(prog, make_abc(OP_AWAIT, prom_r, prom_r, 0));
                emit_instruction(prog, make_abc(OP_UNPACK_ITER_RES, result_r, done_r, prom_r));
                free_registers(emit, prom_r);
            } else {
                emit_instruction(prog, make_abc(OP_ITER_NEXT, result_r, done_r, iter_r));
            }
            int jf_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, done_r, 0));
            
            // Load result directly into binding
            int val_r = bind_r;
            if (node->as.for_of.binding_decl) val_r = alloc_register(emit);
            if (val_r != result_r) {
                emit_instruction(prog, make_abc(OP_MOVE, val_r, result_r, 0));
            }
            if (node->as.for_of.binding_decl) {
                ASTNode* decl = node->as.for_of.binding_decl;
                for (int i = 0; i < decl->as.var_decl.bind_count; i++) {
                    ASTBindingElem* b = &decl->as.var_decl.bindings[i];
                    if (!b->name) continue;
                    int depth = 0;
                    Symbol* sym = lookup_symbol_codegen(inner ? inner : scope, b->name, &depth);
                    if (!sym) continue;
                    if (sym->reg == -1) sym->reg = alloc_register(emit);
                    int key_r = alloc_register(emit);
                    if (decl->as.var_decl.is_array_pattern) {
                        emit_instruction(prog, make_asbx(OP_LOAD_INT, key_r, (int16_t)i));
                    } else {
                        int kci = add_constant_string(prog, b->key ? b->key : b->name, (int)strlen(b->key ? b->key : b->name));
                        emit_instruction(prog, make_abx(OP_LOAD_CONST, key_r, kci));
                    }
                    int bval_r = alloc_register(emit);
                    emit_instruction(prog, make_abc(OP_LOAD_PROP, bval_r, val_r, key_r));
                    free_registers(emit, key_r);
                    if (b->default_val) {
                        int def_r = alloc_register(emit);
                        int jmp_idx = (int)prog->bytecode_size;
                        emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, bval_r, 0));
                        int skip_idx = (int)prog->bytecode_size;
                        emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                        int def_offset = (int)prog->bytecode_size - jmp_idx - 1;
                        prog->bytecode[jmp_idx] = make_asbx(OP_JUMP_IF_NULLISH, bval_r, (int16_t)def_offset);
                        compile_node(b->default_val, prog, inner ? inner : scope, emit, def_r);
                        emit_instruction(prog, make_abc(OP_MOVE, bval_r, def_r, 0));
                        int skip_offset = (int)prog->bytecode_size - skip_idx - 1;
                        prog->bytecode[skip_idx] = make_asbx(OP_JUMP, 0, (int16_t)skip_offset);
                        free_registers(emit, def_r);
                    }
                    if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, bval_r, sym->env_index, depth));
                    else emit_instruction(prog, make_abc(OP_MOVE, sym->reg, bval_r, 0));
                    free_registers(emit, bval_r);
                }
                free_registers(emit, val_r);
            }
            // Body
            int body_r = alloc_register(emit);
            compile_node(node->as.for_of.body, prog, inner ? inner : scope, emit, body_r);
            free_registers(emit, body_r);
            // Loop back
            emit_instruction(prog, make_asbx(OP_JUMP, 0, (int16_t)(loop_start - (int)prog->bytecode_size - 1)));
            prog->bytecode[jf_idx] = make_asbx(OP_JUMP_IF_TRUE, done_r, (int16_t)((int)prog->bytecode_size - jf_idx - 1));
            free_registers(emit, result_r);
            free_registers(emit, iter_r);
            emit->next_reg = base_r;
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_FOR_IN: {
            CompilerScope* inner = (CompilerScope*)node->scope;
            int base_r = emit->next_reg;
            // Evaluate object
            int obj_r = alloc_register(emit);
            compile_node(node->as.for_in.object, prog, scope, emit, obj_r);
            // Binding register
            int bind_r = alloc_register(emit);
            if (node->as.for_in.binding_name && inner) {
                int depth = 0;
                Symbol* sym = lookup_symbol_codegen(inner, node->as.for_in.binding_name, &depth);
                if (sym) { if (sym->reg == -1) sym->reg = alloc_register(emit); bind_r = sym->reg; }
            }
            // Key index register (counts keys during iteration)
            int key_idx_r = alloc_register(emit);
            emit_instruction(prog, make_asbx(OP_LOAD_INT, key_idx_r, 0));
            int done_r = alloc_register(emit);
            int loop_start = (int)prog->bytecode_size;
            emit_instruction(prog, make_abc(OP_FOR_IN_NEXT, bind_r, done_r, key_idx_r));
            int jt_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, done_r, 0));
            int body_r = alloc_register(emit);
            compile_node(node->as.for_in.body, prog, inner ? inner : scope, emit, body_r);
            free_registers(emit, body_r);
            emit_instruction(prog, make_asbx(OP_JUMP, 0, (int16_t)(loop_start - (int)prog->bytecode_size - 1)));
            prog->bytecode[jt_idx] = make_asbx(OP_JUMP_IF_TRUE, done_r, (int16_t)((int)prog->bytecode_size - jt_idx - 1));
            free_registers(emit, done_r);
            free_registers(emit, key_idx_r);
            free_registers(emit, obj_r);
            emit->next_reg = base_r;
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_BREAK: {
            // Emit a jump placeholder; record it for later patching
            int jmp_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            push_break_target(emit, jmp_idx);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_CONTINUE: {
            int jmp_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            push_continue_target(emit, jmp_idx);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_SWITCH: {
            int disc_r = alloc_register(emit);
            compile_node(node->as.switch_stmt.discriminant, prog, scope, emit, disc_r);
            // For each case, emit a comparison + conditional jump
            // Collect jump-to-body indices and jump-over indices
            int* body_jump_idxs = malloc(node->as.switch_stmt.case_count * sizeof(int));
            int default_case_idx = -1;
            // Emit tests
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTSwitchCase* cs = node->as.switch_stmt.cases[i];
                if (!cs->test) { default_case_idx = i; body_jump_idxs[i] = -1; continue; }
                int test_r = alloc_register(emit);
                compile_node(cs->test, prog, scope, emit, test_r);
                int eq_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_EQ, eq_r, disc_r, test_r));
                free_registers(emit, test_r);
                body_jump_idxs[i] = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_TRUE, eq_r, 0));
                free_registers(emit, eq_r);
            }
            // Jump to default or past all
            int default_jmp_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            // Emit bodies
            int* body_starts = malloc(node->as.switch_stmt.case_count * sizeof(int));
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                body_starts[i] = (int)prog->bytecode_size;
                if (body_jump_idxs[i] != -1) {
                    prog->bytecode[body_jump_idxs[i]] = make_asbx(OP_JUMP_IF_TRUE,
                        INST_A(prog->bytecode[body_jump_idxs[i]]),
                        (int16_t)((int)prog->bytecode_size - body_jump_idxs[i] - 1));
                }
                ASTSwitchCase* cs = node->as.switch_stmt.cases[i];
                int saved_break_top = emit->break_top;
                for (int j = 0; j < cs->body_count; j++) {
                    int sr = alloc_register(emit);
                    compile_node(cs->body[j], prog, scope, emit, sr);
                    free_registers(emit, sr);
                }
                // Patch breaks within this case
                int after_switch_placeholder = (int)prog->bytecode_size;
                while (emit->break_top > saved_break_top) {
                    int bi = emit->break_targets[--emit->break_top];
                    prog->bytecode[bi] = make_asbx(OP_JUMP, 0, (int16_t)(after_switch_placeholder - bi - 1));
                }
            }
            // Patch default jump
            if (default_case_idx >= 0) {
                prog->bytecode[default_jmp_idx] = make_asbx(OP_JUMP, 0, (int16_t)(body_starts[default_case_idx] - default_jmp_idx - 1));
            } else {
                prog->bytecode[default_jmp_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - default_jmp_idx - 1));
            }
            free(body_jump_idxs);
            free(body_starts);
            free_registers(emit, disc_r);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_TRY: {
            // Same logic as original
            int outer_try_idx = -1;
            if (node->as.try_stmt.finally_block) {
                outer_try_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_TRY_BEGIN, 0, 0));
            }
            int inner_try_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_TRY_BEGIN, 0, 0));
            int try_r = alloc_register(emit);
            compile_node(node->as.try_stmt.try_block, prog, scope, emit, try_r);
            free_registers(emit, try_r);
            emit_instruction(prog, make_abc(OP_TRY_END, 0, 0, 0));
            int joc_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            prog->bytecode[inner_try_idx] = make_asbx(OP_TRY_BEGIN, 0, (int16_t)((int)prog->bytecode_size - inner_try_idx - 1));
            int err_r = alloc_register(emit);
            emit_instruction(prog, make_abc(OP_CATCH_BEGIN, err_r, 0, 0));
            if (node->as.try_stmt.catch_block) {
                if (node->as.try_stmt.catch_param) {
                    CompilerScope* cs = (CompilerScope*)node->as.try_stmt.catch_block->scope;
                    int depth = 0;
                    Symbol* sym = cs ? lookup_symbol_codegen(cs, node->as.try_stmt.catch_param, &depth) : NULL;
                    if (sym) {
                        if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, err_r, sym->env_index, depth));
                        else { if (sym->reg == -1) sym->reg = alloc_register(emit); emit_instruction(prog, make_abc(OP_MOVE, sym->reg, err_r, 0)); }
                    } else {
                        int ni = add_constant_string(prog, node->as.try_stmt.catch_param, (int)strlen(node->as.try_stmt.catch_param));
                        emit_instruction(prog, make_abx(OP_STORE_GLOBAL, err_r, ni));
                    }
                }
                int cr = alloc_register(emit);
                compile_node(node->as.try_stmt.catch_block, prog, scope, emit, cr);
                free_registers(emit, cr);
            } else {
                emit_instruction(prog, make_abc(OP_THROW, err_r, 0, 0));
            }
            free_registers(emit, err_r);
            prog->bytecode[joc_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - joc_idx - 1));
            if (node->as.try_stmt.finally_block) {
                emit_instruction(prog, make_abc(OP_TRY_END, 0, 0, 0));
                int jof_idx = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                prog->bytecode[outer_try_idx] = make_asbx(OP_TRY_BEGIN, 0, (int16_t)((int)prog->bytecode_size - outer_try_idx - 1));
                int oe_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_CATCH_BEGIN, oe_r, 0, 0));
                int fr = alloc_register(emit);
                compile_node(node->as.try_stmt.finally_block, prog, scope, emit, fr);
                free_registers(emit, fr);
                emit_instruction(prog, make_abc(OP_THROW, oe_r, 0, 0));
                free_registers(emit, oe_r);
                prog->bytecode[jof_idx] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - jof_idx - 1));
                fr = alloc_register(emit);
                compile_node(node->as.try_stmt.finally_block, prog, scope, emit, fr);
                free_registers(emit, fr);
            }
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_RETURN: {
            int r = alloc_register(emit);
            if (node->as.return_stmt.expr) compile_node(node->as.return_stmt.expr, prog, scope, emit, r);
            else emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, r, 0, 0));
            emit_instruction(prog, make_abc(OP_RETURN, r, 0, 0));
            free_registers(emit, r);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_THROW: {
            int r = alloc_register(emit);
            compile_node(node->as.throw_stmt.throw_stmt, prog, scope, emit, r);
            emit_instruction(prog, make_abc(OP_THROW, r, 0, 0));
            free_registers(emit, r);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
        }
        case AST_FUNCTION: {
            int jump_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
            int outer_nr = emit->next_reg; int outer_mr = emit->max_reg;
            int func_idx = prog->function_count++;
            prog->functions = realloc(prog->functions, prog->function_count * sizeof(CompilerFuncInfo));
            emit->next_reg = 0; emit->max_reg = 0;
            CompilerScope* f_scope = (CompilerScope*)node->scope;
            // Allocate param registers
            for (int i = 0; i < node->as.function.param_count; i++) {
                if (!node->as.function.params[i].name) continue;
                int depth = 0;
                Symbol* ps = lookup_symbol_codegen(f_scope, node->as.function.params[i].name, &depth);
                if (ps) ps->reg = alloc_register(emit);
            }
            prog->functions[func_idx].bytecode_offset = (int)prog->bytecode_size;
            prog->functions[func_idx].register_count = 0;
            prog->functions[func_idx].param_count = node->as.function.param_count;
            prog->functions[func_idx].is_async = node->as.function.is_async;
            // Handle default params
            for (int i = 0; i < node->as.function.param_count; i++) {
                if (!node->as.function.params[i].default_val) continue;
                int depth = 0;
                Symbol* ps = f_scope ? lookup_symbol_codegen(f_scope, node->as.function.params[i].name, &depth) : NULL;
                if (!ps || ps->reg < 0) continue;
                // if (param == undefined) param = default_val
                int undef_test_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, undef_test_r, 0, 0));
                int eq_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_EQ, eq_r, ps->reg, undef_test_r));
                free_registers(emit, undef_test_r);
                int jf_i = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP_IF_FALSE, eq_r, 0));
                free_registers(emit, eq_r);
                int def_r = alloc_register(emit);
                compile_node(node->as.function.params[i].default_val, prog, f_scope, emit, def_r);
                emit_instruction(prog, make_abc(OP_MOVE, ps->reg, def_r, 0));
                free_registers(emit, def_r);
                prog->bytecode[jf_i] = make_asbx(OP_JUMP_IF_FALSE, INST_A(prog->bytecode[jf_i]),
                    (int16_t)((int)prog->bytecode_size - jf_i - 1));
            }
            // Environment for closures
            if (f_scope && f_scope->num_captured_vars > 0) {
                int env_r = alloc_register(emit);
                emit_instruction(prog, make_abc(OP_NEW_ENV, env_r, f_scope->num_captured_vars, 0));
                for (int i = 0; i < node->as.function.param_count; i++) {
                    if (!node->as.function.params[i].name) continue;
                    int depth = 0;
                    Symbol* ps = lookup_symbol_codegen(f_scope, node->as.function.params[i].name, &depth);
                    if (ps && ps->is_captured)
                        emit_instruction(prog, make_abc(OP_STORE_ENV, i, ps->env_index, 0));
                }
            }
            // Arrow functions with concise body get an implicit return
            int body_r = alloc_register(emit);
            if (node->as.function.concise_body && node->as.function.is_arrow) {
                compile_node(node->as.function.body, prog, f_scope, emit, body_r);
                emit_instruction(prog, make_abc(OP_RETURN, body_r, 0, 0));
            } else {
                compile_node(node->as.function.body, prog, f_scope, emit, body_r);
                emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, body_r, 0, 0));
                emit_instruction(prog, make_abc(OP_RETURN, body_r, 0, 0));
            }
            prog->functions[func_idx].register_count = emit->max_reg;
            emit->next_reg = outer_nr; emit->max_reg = outer_mr;
            int skip_target = (int)prog->bytecode_size;
            emit_instruction(prog, make_abx(OP_NEW_FUNCTION, dest_reg, (uint16_t)func_idx));
            if (node->as.function.name) {
                int depth = 0;
                Symbol* sym = lookup_symbol_codegen(scope, node->as.function.name, &depth);
                if (sym) {
                    if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, dest_reg, sym->env_index, depth));
                    else {
                        if (sym->reg == -1) sym->reg = alloc_register(emit);
                        emit_instruction(prog, make_abc(OP_MOVE, sym->reg, dest_reg, 0));
                    }
                } else {
                    int ci = add_constant_string(prog, node->as.function.name, (int)strlen(node->as.function.name));
                    emit_instruction(prog, make_abx(OP_STORE_GLOBAL, dest_reg, ci));
                }
            }
            prog->bytecode[jump_idx] = make_asbx(OP_JUMP, 0, (int16_t)(skip_target - jump_idx - 1));
            return dest_reg;
        }
        case AST_CLASS: {
            // Lower class to prototype-chain setup
            // 1. Create constructor function
            // 2. Set prototype properties
            // For simplicity: create a function object from the constructor method,
            // then attach all methods to its .prototype
            int ctor_r = dest_reg;
            ASTClassMethod* ctor_method = NULL;
            for (int i = 0; i < node->as.class_decl.method_count; i++) {
                if (node->as.class_decl.methods[i]->name &&
                    strcmp(node->as.class_decl.methods[i]->name, "constructor") == 0) {
                    ctor_method = node->as.class_decl.methods[i];
                    break;
                }
            }
            if (ctor_method && ctor_method->func_node) {
                compile_node(ctor_method->func_node, prog, scope, emit, ctor_r);
            } else {
                // Default constructor
                int jump_idx2 = (int)prog->bytecode_size;
                emit_instruction(prog, make_asbx(OP_JUMP, 0, 0));
                int fn_idx = prog->function_count++;
                prog->functions = realloc(prog->functions, prog->function_count * sizeof(CompilerFuncInfo));
                prog->functions[fn_idx].bytecode_offset = (int)prog->bytecode_size;
                prog->functions[fn_idx].param_count = 0;
                prog->functions[fn_idx].register_count = 1;
                prog->functions[fn_idx].is_async = false;
                // empty constructor: return undefined
                int br2 = 0;
                emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, br2, 0, 0));
                emit_instruction(prog, make_abc(OP_RETURN, br2, 0, 0));
                prog->bytecode[jump_idx2] = make_asbx(OP_JUMP, 0, (int16_t)((int)prog->bytecode_size - jump_idx2 - 1));
                emit_instruction(prog, make_abx(OP_NEW_FUNCTION, ctor_r, (uint16_t)fn_idx));
            }
            // Handle extends: set up prototype chain via Object.setPrototypeOf
            if (node->as.class_decl.superclass) {
                int super_r = alloc_register(emit);
                compile_node(node->as.class_decl.superclass, prog, scope, emit, super_r);
                // ctor.prototype = Object.create(super.prototype)
                // Simplified: just call Object.setPrototypeOf(ctor.prototype, super.prototype)
                int obj_r = alloc_register(emit);
                int spof_ci = add_constant_string(prog, "Object", 6);
                emit_instruction(prog, make_abx(OP_LOAD_GLOBAL, obj_r, spof_ci));
                // ... (full implementation would call Object.setPrototypeOf)
                free_registers(emit, obj_r);
                free_registers(emit, super_r);
            }
            // Attach methods to ctor.prototype
            int proto_key_r = alloc_register(emit);
            int pki = add_constant_string(prog, "prototype", 9);
            emit_instruction(prog, make_abx(OP_LOAD_CONST, proto_key_r, pki));
            int proto_r = alloc_register(emit);
            emit_instruction(prog, make_abc(OP_LOAD_PROP, proto_r, ctor_r, proto_key_r));
            free_registers(emit, proto_key_r);
            for (int i = 0; i < node->as.class_decl.method_count; i++) {
                ASTClassMethod* m = node->as.class_decl.methods[i];
                if (m->name && strcmp(m->name, "constructor") == 0) continue;
                if (!m->func_node) continue;
                int mfn_r = alloc_register(emit);
                compile_node(m->func_node, prog, scope, emit, mfn_r);
                int mk_r = alloc_register(emit);
                if (m->is_computed && m->name_expr) {
                    compile_node(m->name_expr, prog, scope, emit, mk_r);
                } else if (m->name) {
                    int mki = add_constant_string(prog, m->name, (int)strlen(m->name));
                    emit_instruction(prog, make_abx(OP_LOAD_CONST, mk_r, mki));
                }
                int target_r = m->is_static ? ctor_r : proto_r;
                emit_instruction(prog, make_abc(OP_STORE_PROP, target_r, mk_r, mfn_r));
                free_registers(emit, mk_r);
                free_registers(emit, mfn_r);
            }
            free_registers(emit, proto_r);
            // Bind class name in scope
            if (node->as.class_decl.name) {
                int depth = 0;
                Symbol* sym = lookup_symbol_codegen(scope, node->as.class_decl.name, &depth);
                if (sym) {
                    if (sym->is_captured) emit_instruction(prog, make_abc(OP_STORE_ENV, ctor_r, sym->env_index, depth));
                    else { if (sym->reg == -1) sym->reg = alloc_register(emit); emit_instruction(prog, make_abc(OP_MOVE, sym->reg, ctor_r, 0)); }
                } else {
                    int ci = add_constant_string(prog, node->as.class_decl.name, (int)strlen(node->as.class_decl.name));
                    emit_instruction(prog, make_abx(OP_STORE_GLOBAL, ctor_r, ci));
                }
            }
            return dest_reg;
        }
        case AST_OBJECT: {
            emit_instruction(prog, make_abc(OP_NEW_OBJECT, dest_reg, 0, 0));
            int key_r = alloc_register(emit);
            int val_r = alloc_register(emit);
            for (int i = 0; i < node->as.object.count; i++) {
                int flags = node->as.object.prop_flags[i];
                if (flags & (1<<4)) {
                    // spread: Object.assign(dest, value)
                    compile_node(node->as.object.values[i], prog, scope, emit, val_r);
                    emit_instruction(prog, make_abc(OP_OBJ_SPREAD, dest_reg, val_r, 0));
                    continue;
                }
                if (flags & 1) {
                    // computed key
                    compile_node(node->as.object.key_exprs[i], prog, scope, emit, key_r);
                } else if (node->as.object.keys[i]) {
                    int ci = add_constant_string(prog, node->as.object.keys[i], (int)strlen(node->as.object.keys[i]));
                    emit_instruction(prog, make_abx(OP_LOAD_CONST, key_r, ci));
                }
                compile_node(node->as.object.values[i], prog, scope, emit, val_r);
                emit_instruction(prog, make_abc(OP_STORE_PROP, dest_reg, key_r, val_r));
            }
            free_registers(emit, key_r);
            return dest_reg;
        }
        case AST_ARRAY: {
            emit_instruction(prog, make_abc(OP_NEW_ARRAY, dest_reg, 0, 0));
            int val_r = alloc_register(emit);
            int effective_idx = 0;
            for (int i = 0; i < node->as.array.count; i++) {
                ASTNode* elem = node->as.array.elements[i];
                if (!elem) { effective_idx++; continue; }
                if (elem->type == AST_SPREAD) {
                    compile_node(elem->as.spread_expr, prog, scope, emit, val_r);
                    emit_instruction(prog, make_abc(OP_ARRAY_SPREAD, dest_reg, val_r, 0));
                } else {
                    compile_node(elem, prog, scope, emit, val_r);
                    int idx_r = alloc_register(emit);
                    emit_instruction(prog, make_asbx(OP_LOAD_INT, idx_r, (int16_t)effective_idx));
                    emit_instruction(prog, make_abc(OP_STORE_PROP, dest_reg, idx_r, val_r));
                    free_registers(emit, idx_r);
                    effective_idx++;
                }
            }
            free_registers(emit, val_r);
            return dest_reg;
        }
        case AST_SPREAD: {
            // Spread in expression context — just emit the inner value
            compile_node(node->as.spread_expr, prog, scope, emit, dest_reg);
            return dest_reg;
        }
        case AST_TEMPLATE_LITERAL: {
            // Concatenate all parts
            if (node->as.tmpl.part_count == 0) {
                int ci = add_constant_string(prog, "", 0);
                emit_instruction(prog, make_abx(OP_LOAD_CONST, dest_reg, ci));
                return dest_reg;
            }
            compile_node(node->as.tmpl.parts[0], prog, scope, emit, dest_reg);
            for (int i = 1; i < node->as.tmpl.part_count; i++) {
                int r = alloc_register(emit);
                compile_node(node->as.tmpl.parts[i], prog, scope, emit, r);
                emit_instruction(prog, make_abc(OP_ADD, dest_reg, dest_reg, r));
                free_registers(emit, r);
            }
            return dest_reg;
        }
        case AST_PROP_ACCESS: {
            int obj_r = alloc_register(emit);
            compile_node(node->as.prop_access.obj, prog, scope, emit, obj_r);
            int prop_r = alloc_register(emit);
            compile_node(node->as.prop_access.prop, prog, scope, emit, prop_r);
            emit_instruction(prog, make_abc(OP_LOAD_PROP, dest_reg, obj_r, prop_r));
            free_registers(emit, prop_r);
            free_registers(emit, obj_r);
            return dest_reg;
        }
        case AST_OPTIONAL_CHAIN: {
            // obj?.prop: if obj is null/undefined, return undefined; else load prop
            int obj_r = alloc_register(emit);
            compile_node(node->as.prop_access.obj, prog, scope, emit, obj_r);
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            int jn_idx = (int)prog->bytecode_size;
            emit_instruction(prog, make_asbx(OP_JUMP_IF_NULLISH, obj_r, 0));
            // Not null — load prop
            if (node->as.prop_access.prop) {
                int prop_r = alloc_register(emit);
                compile_node(node->as.prop_access.prop, prog, scope, emit, prop_r);
                emit_instruction(prog, make_abc(OP_LOAD_PROP, dest_reg, obj_r, prop_r));
                free_registers(emit, prop_r);
            } else {
                // optional call — callee is already in obj_r
                emit_instruction(prog, make_abc(OP_CALL, dest_reg, obj_r, 0));
            }
            prog->bytecode[jn_idx] = make_asbx(OP_JUMP_IF_NULLISH, obj_r, (int16_t)((int)prog->bytecode_size - jn_idx - 1));
            free_registers(emit, obj_r);
            return dest_reg;
        }
        case AST_EXPR_STMT:
            compile_node(node->as.expr_stmt, prog, scope, emit, dest_reg);
            return dest_reg;
        case AST_SEQUENCE: {
            for (int i = 0; i < node->as.sequence.count; i++) {
                int r = (i == node->as.sequence.count - 1) ? dest_reg : alloc_register(emit);
                compile_node(node->as.sequence.exprs[i], prog, scope, emit, r);
                if (i < node->as.sequence.count - 1) free_registers(emit, r);
            }
            return dest_reg;
        }
        default:
            emit_instruction(prog, make_abc(OP_LOAD_UNDEFINED, dest_reg, 0, 0));
            return dest_reg;
    }
}

// ─── Compilation entry point ──────────────────────────────────────────────────
CompiledProgram* compile_source(const char* source) {
    Parser parser;
    parser.lexer.source = source;
    parser.lexer.cursor = 0;
    parser.lexer.last_was_value = false;
    parser.has_error = false;
    parser.tmpl_depth = 0;
    parser.tmpl_brace_stack = NULL;
    parser.tmpl_brace_top = 0;
    parser.current.type = TOK_EOF;
    parser.current.length = 0;
    parser.current.start = source;
    parser.current.number_value = 0.0;
    parser.current.template_str = NULL;
    parser.current.template_str_len = 0;
    parser.previous = parser.current;
    advance(&parser);

    ASTNode* root = ast_new_node(AST_BLOCK);
    root->as.block.count = 0; root->as.block.statements = NULL;
    while (!check(&parser, TOK_EOF)) {
        ASTNode* stmt = parse_statement(&parser);
        if (stmt) {
            root->as.block.count++;
            root->as.block.statements = realloc(root->as.block.statements, root->as.block.count * sizeof(ASTNode*));
            root->as.block.statements[root->as.block.count-1] = stmt;
        } else {
            advance(&parser);
        }
    }

    if (parser.tmpl_brace_stack) free(parser.tmpl_brace_stack);

    if (parser.has_error) { ast_free_node(root); return NULL; }

    CompiledProgram* prog = malloc(sizeof(CompiledProgram));
    prog->const_pool = NULL; prog->const_pool_size = 0;
    prog->bytecode = NULL; prog->bytecode_size = 0;
    prog->functions = NULL; prog->function_count = 0;
    prog->function_count = 1;
    prog->functions = malloc(sizeof(CompilerFuncInfo));

    CompilerScope* global_scope = create_scope(NULL, true);
    resolve_identifiers(root, global_scope);

    Emitter emit;
    emit.program = prog;
    emit.next_reg = 0; emit.max_reg = 0;
    emit.break_targets = NULL; emit.break_top = 0; emit.break_cap = 0;
    emit.continue_targets = NULL; emit.continue_top = 0; emit.continue_cap = 0;

    prog->functions[0].bytecode_offset = 0;
    prog->functions[0].param_count = 0;
    prog->functions[0].is_async = false;

    if (global_scope->num_captured_vars > 0) {
        int er = alloc_register(&emit);
        emit_instruction(prog, make_abc(OP_NEW_ENV, er, global_scope->num_captured_vars, 0));
        free_registers(&emit, er);
    }

    int result_reg = alloc_register(&emit);
    compile_node(root, prog, global_scope, &emit, result_reg);
    emit_instruction(prog, make_abc(OP_RETURN, result_reg, 0, 0));

    prog->functions[0].register_count = emit.max_reg;

    if (prog->bytecode_size > 0) {
        prog->ic_table = malloc(prog->bytecode_size * sizeof(uint32_t));
        for (uint32_t i = 0; i < prog->bytecode_size; i++) prog->ic_table[i] = 0xFFFFFFFF;
    } else {
        prog->ic_table = NULL;
    }

    free(emit.break_targets);
    free(emit.continue_targets);
    free_scope(global_scope);
    ast_free_node(root);
    return prog;
}

void free_compiled_program(CompiledProgram* prog) {
    if (!prog) return;
    for (uint32_t i = 0; i < prog->const_pool_size; i++) {
        Value val = prog->const_pool[i];
        if (IS_POINTER(val)) {
            void* ptr = get_pointer(val);
            free((char*)ptr - sizeof(BlockHeader));
        }
    }
    free(prog->const_pool);
    free(prog->bytecode);
    free(prog->ic_table);
    free(prog->functions);
    free(prog);
}

// ─── Serialization (unchanged from original) ──────────────────────────────────
typedef struct { char magic[4]; uint32_t version; uint32_t const_count; uint32_t inst_count; uint32_t func_count; } CbcFileHeader;
typedef struct { uint32_t offset; uint32_t register_count; uint32_t param_count; } CbcFileFunc;

uint8_t* serialize_program(const CompiledProgram* prog, uint32_t* out_size) {
    uint32_t total = sizeof(CbcFileHeader);
    for (uint32_t i = 0; i < prog->const_pool_size; i++) {
        Value v = prog->const_pool[i]; total += 1;
        if (IS_POINTER(v)) { JSString* s = (JSString*)get_pointer(v); total += 4 + s->length; }
        else total += 8;
    }
    total += prog->bytecode_size * sizeof(uint32_t);
    total += prog->function_count * sizeof(CbcFileFunc);
    uint8_t* buf = malloc(total); uint8_t* p = buf;
    CbcFileHeader hdr; memcpy(hdr.magic,"CBC\0",4); hdr.version=1; hdr.const_count=prog->const_pool_size; hdr.inst_count=prog->bytecode_size; hdr.func_count=prog->function_count;
    memcpy(p,&hdr,sizeof(hdr)); p+=sizeof(hdr);
    for (uint32_t i=0;i<prog->const_pool_size;i++) {
        Value v=prog->const_pool[i];
        if (IS_POINTER(v)) { JSString* s=(JSString*)get_pointer(v); *p++=1; memcpy(p,&s->length,4); p+=4; memcpy(p,s->data,s->length); p+=s->length; }
        else { *p++=0; memcpy(p,&v,8); p+=8; }
    }
    memcpy(p,prog->bytecode,prog->bytecode_size*sizeof(uint32_t)); p+=prog->bytecode_size*sizeof(uint32_t);
    for (uint32_t i=0;i<prog->function_count;i++) {
        CbcFileFunc f; f.offset=prog->functions[i].bytecode_offset; f.register_count=prog->functions[i].register_count; f.param_count=prog->functions[i].param_count;
        memcpy(p,&f,sizeof(f)); p+=sizeof(f);
    }
    *out_size=total; return buf;
}

CompiledProgram* deserialize_program(const uint8_t* data, uint32_t size) {
    if (size < sizeof(CbcFileHeader)) return NULL;
    const uint8_t* p = data;
    CbcFileHeader hdr; memcpy(&hdr,p,sizeof(hdr)); p+=sizeof(hdr);
    if (memcmp(hdr.magic,"CBC\0",4)!=0||hdr.version!=1) return NULL;
    CompiledProgram* prog=malloc(sizeof(CompiledProgram));
    prog->const_pool_size=hdr.const_count; prog->const_pool=malloc(prog->const_pool_size*sizeof(Value));
    for (uint32_t i=0;i<prog->const_pool_size;i++) {
        uint8_t tag=*p++;
        if (tag==1) {
            uint32_t len; memcpy(&len,p,4); p+=4;
            char* block=malloc(sizeof(BlockHeader)+sizeof(JSString)+len+1);
            BlockHeader* bh=(BlockHeader*)block; bh->size=sizeof(BlockHeader)+sizeof(JSString)+len+1; bh->is_free=0; bh->gc_mark=0; bh->obj_type=OBJ_STRING;
            JSString* s=(JSString*)(block+sizeof(BlockHeader)); s->length=len; s->hash=hash_string((const char*)p,len); memcpy(s->data,p,len); s->data[len]='\0'; p+=len;
            prog->const_pool[i]=make_pointer(s);
        } else { uint64_t u; memcpy(&u,p,8); p+=8; prog->const_pool[i]=u; }
    }
    prog->bytecode_size=hdr.inst_count; prog->bytecode=malloc(prog->bytecode_size*sizeof(uint32_t));
    memcpy(prog->bytecode,p,prog->bytecode_size*sizeof(uint32_t)); p+=prog->bytecode_size*sizeof(uint32_t);
    prog->function_count=hdr.func_count; prog->functions=malloc(prog->function_count*sizeof(CompilerFuncInfo));
    for (uint32_t i=0;i<prog->function_count;i++) {
        CbcFileFunc f; memcpy(&f,p,sizeof(f)); p+=sizeof(f);
        prog->functions[i].bytecode_offset=f.offset; prog->functions[i].register_count=f.register_count; prog->functions[i].param_count=f.param_count; prog->functions[i].is_async=false;
    }
    if (prog->bytecode_size>0) {
        prog->ic_table=malloc(prog->bytecode_size*sizeof(uint32_t));
        for (uint32_t i=0;i<prog->bytecode_size;i++) prog->ic_table[i]=0xFFFFFFFF;
    } else prog->ic_table=NULL;
    return prog;
}
