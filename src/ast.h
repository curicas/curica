#ifndef AST_H
#define AST_H

#include <stdint.h>
#include <stdbool.h>

// AST Node Types
typedef enum {
    AST_LITERAL_NUMBER,
    AST_LITERAL_STRING,
    AST_LITERAL_BOOL,
    AST_LITERAL_NULL,
    AST_LITERAL_UNDEFINED,
    AST_LITERAL_REGEX,       // /pattern/flags
    AST_IDENTIFIER,

    AST_VAR_DECL,            // Var/Let/Const declaration (single or destructuring)
    AST_ASSIGN,              // Assignment: Target = Value (includes compound +=, etc.)
    AST_BINARY,              // Left op Right
    AST_UNARY,               // op Expr (prefix: !, -, ~, typeof, void, delete, ++)
    AST_POSTFIX,             // Expr op  (postfix: expr++, expr--)
    AST_TERNARY,             // Cond ? Then : Else
    AST_CALL,                // Callee(Args...)
    AST_NEW_CALL,            // new Callee(Args...)
    AST_BLOCK,               // { Statements... }
    AST_IF,                  // If (Cond) Body Else Body
    AST_WHILE,               // While (Cond) Body
    AST_DO_WHILE,            // Do Body While (Cond)
    AST_FOR,                 // For (Init; Cond; Update) Body
    AST_FOR_OF,              // For (binding of iterable) Body
    AST_FOR_IN,              // For (binding in object) Body
    AST_BREAK,               // break [label]
    AST_CONTINUE,            // continue [label]
    AST_SWITCH,              // switch (discriminant) { cases }
    AST_RETURN,              // Return Expr
    AST_FUNCTION,            // Function(Params) { Body }
    AST_OBJECT,              // { Key: Value, ... }
    AST_ARRAY,               // [ Expr, ... ]
    AST_SPREAD,              // ...Expr  (in calls / array / object literals)
    AST_TEMPLATE_LITERAL,    // `strings${expr}...`
    AST_PROP_ACCESS,         // Obj.Prop or Obj[Prop]
    AST_OPTIONAL_CHAIN,      // Obj?.Prop or Obj?.[Prop] or Obj?.()
    AST_EXPR_STMT,           // Expr;
    AST_AWAIT,               // await Expr
    AST_YIELD,               // yield [Expr]
    AST_TRY,                 // try { ... } catch (e) { ... } finally { ... }
    AST_THROW,               // throw Expr
    AST_SEQUENCE,            // a, b, c  (comma operator)
    AST_CLASS,               // class Name [extends Super] { methods }
    AST_EMPTY,               // empty statement ;
} ASTNodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    double value;
} ASTLiteralNumber;

typedef struct {
    char* value;
    int length;
} ASTLiteralString;

typedef struct {
    bool value;
} ASTLiteralBool;

typedef struct {
    char* pattern;
    char* flags;
} ASTLiteralRegex;

typedef struct {
    char* name;
} ASTIdentifier;

// A single destructuring binding target (simplified: just name or nested pattern)
typedef struct {
    char* name;             // NULL if pattern-based
    ASTNode* pattern;       // Non-NULL for nested [a, b] = ... or { x } = ...
    ASTNode* default_val;   // default value expression (optional)
    char* key;              // for object patterns: the property key
    bool is_rest;           // rest element
} ASTBindingElem;

typedef struct {
    // name is set for simple: var x = ...
    // For destructuring, name == NULL and pattern_kind is set
    char* name;
    ASTNode* init;          // Optional initial value
    bool is_const;
    bool is_using;
    bool is_await_using;
    // Destructuring
    int bind_count;         // 0 = simple name binding
    ASTBindingElem* bindings;   // array of binding elements
    bool is_array_pattern;  // true = [...], false = {...}
} ASTVarDecl;

typedef struct {
    ASTNode* target;        // Identifier, PropAccess, or destructuring pattern
    ASTNode* value;
    int compound_op;        // 0 = plain '=', else token type of operator (TOK_PLUS_ASSIGN etc.)
} ASTAssign;

typedef struct {
    int op;                 // Token type
    ASTNode* left;
    ASTNode* right;
} ASTBinary;

typedef struct {
    int op;                 // Operator token type or char
    ASTNode* expr;
    bool is_prefix;
} ASTUnary;

typedef struct {
    int op;
    ASTNode* expr;
} ASTPostfix;

typedef struct {
    ASTNode* cond;
    ASTNode* then_expr;
    ASTNode* else_expr;
} ASTTernary;

typedef struct {
    ASTNode* callee;
    ASTNode** args;
    int arg_count;
} ASTCall;

typedef struct {
    int count;
    ASTNode** statements;
    bool is_inline;
} ASTBlock;

typedef struct {
    ASTNode* cond;
    ASTNode* then_branch;
    ASTNode* else_branch;   // Optional
} ASTIf;

typedef struct {
    ASTNode* cond;
    ASTNode* body;
} ASTWhile;

typedef struct {
    ASTNode* init;          // Optional var decl or expr
    ASTNode* cond;          // Optional condition
    ASTNode* update;        // Optional update expr
    ASTNode* body;
} ASTFor;

typedef struct {
    char* binding_name;     // simple: for (let x of ...)
    ASTNode* binding_decl;  // non-NULL if it's a var/let/const decl node
    ASTNode* iterable;
    ASTNode* body;
    bool is_const;
    bool is_await;
} ASTForOf;

typedef struct {
    char* binding_name;
    ASTNode* binding_decl;
    ASTNode* object;
    ASTNode* body;
    bool is_const;
} ASTForIn;

typedef struct {
    char* label;            // NULL for unlabelled
} ASTBreak;

typedef struct {
    char* label;
} ASTContinue;

typedef struct {
    ASTNode* test;          // NULL for default:
    ASTNode** body;
    int body_count;
    bool has_break;
} ASTSwitchCase;

typedef struct {
    ASTNode* discriminant;
    ASTSwitchCase** cases;
    int case_count;
} ASTSwitch;

typedef struct {
    ASTNode* expr;          // Optional
} ASTReturn;

typedef struct {
    ASTNode* expr;
} ASTAwait;

typedef struct {
    ASTNode* expr;          // NULL for bare yield
    bool is_delegate;       // yield*
} ASTYield;

typedef struct {
    ASTNode* throw_stmt;
} ASTThrowStmt;

// Function parameter descriptor
typedef struct {
    char* name;
    ASTNode* default_val;   // Optional default value
    bool is_rest;           // rest parameter ...name
    // Destructuring params (simplified: treated as regular if complex)
} ASTParam;

typedef struct {
    char* name;             // Optional (NULL for anonymous / arrow)
    ASTParam* params;       // array of params
    int param_count;
    ASTNode* body;          // Must be AST_BLOCK (or expression for arrow concise)
    bool is_async;
    bool is_generator;
    bool is_arrow;
    bool concise_body;      // arrow with expression body, no braces
} ASTFunction;

typedef struct {
    char** keys;            // Array of property names (NULL for computed or shorthand)
    ASTNode** key_exprs;    // Non-NULL for computed keys
    ASTNode** values;       // Array of property values (NULL for shorthand methods)
    int* prop_flags;        // bit 0: is_computed, bit 1: is_shorthand, bit 2: is_getter, bit 3: is_setter, bit 4: is_spread
    int count;
} ASTObject;

typedef struct {
    ASTNode** elements;     // NULL entry = elision; AST_SPREAD for ...x
    int count;
} ASTArray;

typedef struct {
    ASTNode* obj;
    ASTNode* prop;          // String literal or Expression
    bool is_computed;
    bool is_optional;       // ?.
} ASTPropAccess;

typedef struct {
    // array of alternating: string segments (ASTLiteralString) and expressions
    ASTNode** parts;
    int part_count;         // always odd: string, expr, string, expr, ..., string
} ASTTemplateLiteral;

typedef struct {
    ASTNode* try_block;
    char* catch_param;      // Optional
    ASTNode* catch_block;   // Optional
    ASTNode* finally_block; // Optional
} ASTTry;

typedef struct {
    ASTNode** exprs;
    int count;
} ASTSequence;

// Method in a class body
typedef struct {
    char* name;
    ASTNode* name_expr;     // computed key
    bool is_computed;
    bool is_static;
    bool is_getter;
    bool is_setter;
    bool is_async;
    bool is_generator;
    ASTNode* func_node;
} ASTClassMethod;

typedef struct {
    char* name;             // NULL for anonymous
    ASTNode* superclass;    // NULL if no extends
    ASTClassMethod** methods;
    int method_count;
} ASTClass;

struct ASTNode {
    ASTNodeType type;
    void* scope;
    union {
        ASTLiteralNumber  number;
        ASTLiteralString  string;
        ASTLiteralBool    boolean;
        ASTLiteralRegex   regex;
        ASTIdentifier     identifier;
        ASTVarDecl        var_decl;
        ASTAssign         assign;
        ASTBinary         binary;
        ASTUnary          unary;
        ASTPostfix        postfix;
        ASTTernary        ternary;
        ASTCall           call;
        ASTBlock          block;
        ASTIf             if_stmt;
        ASTWhile          while_stmt;
        ASTFor            for_stmt;
        ASTForOf          for_of;
        ASTForIn          for_in;
        ASTBreak          break_stmt;
        ASTContinue       continue_stmt;
        ASTSwitch         switch_stmt;
        ASTReturn         return_stmt;
        ASTFunction       function;
        ASTObject         object;
        ASTArray          array;
        ASTPropAccess     prop_access;
        ASTTemplateLiteral tmpl;
        ASTNode*          expr_stmt;
        ASTNode*          spread_expr;
        ASTAwait          await_expr;
        ASTYield          yield_expr;
        ASTTry            try_stmt;
        ASTThrowStmt      throw_stmt;
        ASTSequence       sequence;
        ASTClass          class_decl;
    } as;
};

#endif // AST_H
