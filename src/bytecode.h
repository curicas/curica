#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdint.h>

// Opcode definitions for the register-based VM
typedef enum {
    OP_LOAD_CONST,       // A Bx   regs[A] = const_pool[Bx]
    OP_LOAD_INT,         // A sBx  regs[A] = make_integer(sBx)
    OP_LOAD_BOOL,        // A B    regs[A] = make_boolean(B)
    OP_LOAD_NULL,        // A      regs[A] = VAL_NULL
    OP_LOAD_UNDEFINED,   // A      regs[A] = VAL_UNDEFINED

    OP_LOAD_GLOBAL,      // A Bx   regs[A] = get_global(const_pool[Bx])
    OP_STORE_GLOBAL,     // A Bx   set_global(const_pool[Bx], regs[A])

    OP_LOAD_ENV,         // A B C  regs[A] = env[C_depth][B_index]
    OP_STORE_ENV,        // A B C  env[C_depth][B_index] = regs[A]

    OP_LOAD_PROP,        // A B C  regs[A] = regs[B][regs[C]]
    OP_STORE_PROP,       // A B C  regs[A][regs[B]] = regs[C]
    OP_DELETE_PROP,      // A B C  regs[A] = delete regs[B][regs[C]]

    OP_MOVE,             // A B    regs[A] = regs[B]

    OP_ADD,              // A B C  regs[A] = regs[B] + regs[C]  (numeric or concat)
    OP_SUB,              // A B C  regs[A] = regs[B] - regs[C]
    OP_MUL,              // A B C  regs[A] = regs[B] * regs[C]
    OP_DIV,              // A B C  regs[A] = regs[B] / regs[C]
    OP_MOD,              // A B C  regs[A] = regs[B] % regs[C]
    OP_POW,              // A B C  regs[A] = regs[B] ** regs[C]
    OP_CONCAT,           // A B C  regs[A] = ToString(regs[B]) + ToString(regs[C])

    OP_LT,               // A B C  regs[A] = regs[B] < regs[C]
    OP_LE,               // A B C  regs[A] = regs[B] <= regs[C]
    OP_GT,               // A B C  regs[A] = regs[B] > regs[C]
    OP_GE,               // A B C  regs[A] = regs[B] >= regs[C]
    OP_EQ,               // A B C  regs[A] = regs[B] === regs[C]  (strict)
    OP_NE,               // A B C  regs[A] = regs[B] !== regs[C]  (strict)
    OP_EQ_LOOSE,         // A B C  regs[A] = regs[B] == regs[C]   (abstract)
    OP_NE_LOOSE,         // A B C  regs[A] = regs[B] != regs[C]   (abstract)
    OP_IN,               // A B C  regs[A] = regs[B] in regs[C]
    OP_INSTANCEOF,       // A B C  regs[A] = regs[B] instanceof regs[C]

    OP_NOT,              // A B    regs[A] = !regs[B]
    OP_NEG,              // A B    regs[A] = -regs[B]
    OP_TYPEOF,           // A B    regs[A] = typeof regs[B]  (string result)
    OP_VOID,             // A B    regs[A] = void regs[B]    (always undefined)

    OP_BITAND,           // A B C  regs[A] = regs[B] & regs[C]
    OP_BITOR,            // A B C  regs[A] = regs[B] | regs[C]
    OP_BITXOR,           // A B C  regs[A] = regs[B] ^ regs[C]
    OP_BITNOT,           // A B    regs[A] = ~regs[B]
    OP_SHL,              // A B C  regs[A] = regs[B] << regs[C]
    OP_SHR,              // A B C  regs[A] = regs[B] >> regs[C]
    OP_USHR,             // A B C  regs[A] = regs[B] >>> regs[C]

    OP_INC,              // A B    regs[A] = regs[B] + 1
    OP_DEC,              // A B    regs[A] = regs[B] - 1

    OP_JUMP,             // sBx    ip += sBx
    OP_JUMP_IF_FALSE,    // A sBx  if (!is_truthy(regs[A])) ip += sBx
    OP_JUMP_IF_TRUE,     // A sBx  if (is_truthy(regs[A]))  ip += sBx
    OP_JUMP_IF_NULLISH,  // A sBx  if (null||undefined) ip += sBx else continue

    OP_CALL,             // A B C  regs[A] = call(regs[B], args: regs[B+1]...regs[B+C])
    OP_NEW_CALL,         // A B C  regs[A] = new regs[B](args: regs[B+1]...regs[B+C])
    OP_RETURN,           // A      return regs[A]

    OP_THROW,            // A      throw regs[A]
    OP_TRY_BEGIN,        // sBx    push error handler; on error jump to ip+sBx
    OP_TRY_END,          // -      pop error handler
    OP_CATCH_BEGIN,      // A      regs[A] = caught error; pop error handler

    OP_NEW_OBJECT,       // A      regs[A] = new_object()
    OP_NEW_ARRAY,        // A      regs[A] = new_array()
    OP_NEW_FUNCTION,     // A Bx   regs[A] = new_function_closure(func_table[Bx], current_env)
    OP_NEW_ENV,          // A B    regs[A] = new_env(parent_env, size = B)
    OP_NEW_REGEX,        // A Bx   regs[A] = new RegExp(const_pool[Bx].pattern, const_pool[Bx+1].flags)

    // Array/object construction helpers
    OP_ARRAY_PUSH,       // A B    regs[A].push(regs[B])  (append to array)
    OP_ARRAY_SPREAD,     // A B    spread regs[B] into array regs[A]
    OP_OBJ_SPREAD,       // A B    Object.assign(regs[A], regs[B])

    OP_ITER_NEXT,        // A B    regs[A] = regs[B].next()  (iterator protocol)
    OP_FOR_IN_NEXT,      // A B C  regs[A]=key, regs[B]=done; C=obj key index register
    OP_GET_ITER,         // A B    regs[A] = regs[B][Symbol.iterator]()

    OP_PRINT,            // A      print(regs[A])
    OP_AWAIT,            // A B    await regs[B], result into regs[A]
    OP_YIELD,            // A B    yield regs[B], result (sent value) into regs[A]

    OP_COUNT             // Sentinel: total number of opcodes
} Opcode;

// Instruction Decoding Macros
// An instruction is a 32-bit value:
// Layout 1 (ABC):  [Opcode: 8] [A: 8] [B: 8] [C: 8]
// Layout 2 (ABx):  [Opcode: 8] [A: 8] [Bx: 16]
// Layout 3 (AsBx): [Opcode: 8] [A: 8] [sBx: 16 signed]

#define INST_OP(i)   ((i) & 0xFF)
#define INST_A(i)    (((i) >> 8) & 0xFF)
#define INST_B(i)    (((i) >> 16) & 0xFF)
#define INST_C(i)    (((i) >> 24) & 0xFF)
#define INST_BX(i)   (((i) >> 16) & 0xFFFF)
#define INST_SBX(i)  ((int16_t)(((i) >> 16) & 0xFFFF))

static inline uint32_t make_abc(Opcode op, uint8_t a, uint8_t b, uint8_t c) {
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)b << 16) | ((uint32_t)c << 24);
}

static inline uint32_t make_abx(Opcode op, uint8_t a, uint16_t bx) {
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)bx << 16);
}

static inline uint32_t make_asbx(Opcode op, uint8_t a, int16_t sbx) {
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)(uint16_t)sbx << 16);
}

#endif // BYTECODE_H
