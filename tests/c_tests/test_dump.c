#include <stdio.h>
#include "bytecode.h"
#include "compiler.h"
#include "alloc.h"

int main() {
    arena_init(16);
    CompiledProgram* prog = compile_source("try { throw 1; } catch (e) { print(e); }");
    if (!prog) return 1;
    for(uint32_t i=0; i<prog->bytecode_size; i++) {
        uint32_t inst = prog->bytecode[i];
        printf("%d: OP=%d A=%d B=%d C=%d sBx=%d\n", i, INST_OP(inst), INST_A(inst), INST_B(inst), INST_C(inst), INST_SBX(inst));
    }
    return 0;
}
